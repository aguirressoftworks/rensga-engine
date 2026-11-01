// MVP-20/21: shared cooker core. Scene JSON -> reflected component blobs -> pack v1.
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/cooker/input_cook.h"

#include "rensga/cooker/cooker.h"
#include "rensga/cooker/model3d.h"
#include "rensga/cooker/resample.h"  // F5.17 (A6)
#include "prefab_cook.h"  // F6 A1
#include "physics_cook.h"
#include "resources_cook.h"  // F6 a3
#include "rensga/cooker/prefab_expand.h"
#include "rensga/core/components.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/reflect_method.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace {

constexpr const char* kCookerVersion = "rensga-cooker-6";

struct Ctx {
  CookResult result;
  CookSink sink;
  void msg(CookMessage::Severity s, const std::string& text, const std::string& asset = "", const std::string& guid = "") {
    CookMessage m;
    m.severity = s;
    m.text = text;
    m.asset = asset;
    m.assetGuid = guid;
    if (s == CookMessage::Error) result.errors.push_back(m);
    if (sink) sink(m);
  }
  void error(const std::string& text, const std::string& asset = "", const std::string& guid = "") {
    msg(CookMessage::Error, text, asset, guid);
  }
  void info(const std::string& text) { msg(CookMessage::Info, text); }
};

bool readText(const fs::path& p, std::string& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

uint64_t fnv64(const std::string& s, uint64_t h = 1469598103934665603ull) {
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}
std::string hex64(uint64_t v) {
  char b[17];
  snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
  return b;
}

std::string guidText(const Guid& g) {
  char t[Guid::kTextLength + 1];
  g.format(t);
  return t;
}

struct Asset {
  fs::path path;       // absolute
  std::string rel;     // project-relative, '/' separators
  std::string guid;
  std::string importSettings;  // JSON text of the .meta importSettings object
};

struct ObjectDef {
  Guid guid;
  std::string name;
  std::string parentGuid;
  int parent = -1;
  bool active = true;
  std::string tag;  // empty = Untagged
  int layer = 0;
  PackTransform xf = {};
  struct Comp { const TypeInfo* type; std::vector<uint8_t> blob; json fields; json srcFields; };
  std::vector<Comp> comps;
};

// Aligned scratch instance of a reflected type.
struct Instance {
  void* mem;
  const TypeInfo* t;
  explicit Instance(const TypeInfo* type) : t(type) {
    size_t sz = (type->size + 15) & ~size_t(15);
    mem = aligned_alloc(16, sz ? sz : 16);
    type->construct(mem);
  }
  ~Instance() {
    t->destruct(mem);
    free(mem);
  }
};

std::string relPath(const fs::path& root, const fs::path& p) { return fs::relative(p, root).generic_string(); }

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
bool isGltfRel(const std::string& rel) { return endsWith(rel, ".gltf") || endsWith(rel, ".glb"); }

// F5: one cooked material (MATL record in the making).
struct MatEntry { Guid guid; MaterialDef def; std::string rel; };
// F5: one skin with its clips (SKIN + CLP3 records in the making).
struct SkinEntry { Guid guid; std::string rel; std::vector<GltfJoint> joints; std::vector<GltfClip> clips; };
// F5: a lit mesh cooked once and shared by every renderer that uses the same glTF / primitive.
struct LitCached {
  PackMeshLit desc = {};
  uint32_t skin = kInvalidIndex;
  bool hasDerivedMaterial = false;
  Guid derivedMaterial;
  MaterialDef derivedDef;
  std::string derivedWarning;  // reported only when the derived material is the one used
};

}  // namespace

CookResult cookProject(const CookOptions& optIn, const CookSink& sink) {
  CookOptions opt = optIn;
  if (opt.target.empty()) {  // no default console: the target is the project's own profile (ConsoleTarget.json)
    try {
      opt.target = loadConsoleTarget(fs::absolute(opt.project) / "ProjectSettings" / "ConsoleTarget.json").profile;
    } catch (const std::exception& e) {
      CookResult bad;
      bad.errors.push_back({CookMessage::Error, std::string("no target given and the project profile is unreadable: ") + e.what(), "", ""});
      if (sink) sink(bad.errors.back());
      return bad;
    }
  }
  Ctx cx;
  cx.sink = sink;
  CookResult& R = cx.result;
  registerBuiltinComponents();

  const fs::path proj = fs::absolute(opt.project);
  const fs::path settings = proj / "ProjectSettings";
  ICookerFormatPlugin* plugin = CookerPluginRegistry::instance().find(opt.target);
  if (!plugin) {
    cx.error("no cooker format plugin registered for target '" + opt.target + "'");
    return R;
  }

  ConsoleTargetData target;
  json projectJson, inputsJson;
  try {
    target = loadConsoleTarget(settings / "ConsoleTarget.json");
    std::string txt;
    if (!readText(settings / "Project.json", txt)) throw std::runtime_error("ProjectSettings/Project.json: cannot open");
    projectJson = json::parse(txt);
    if (projectJson.value("format", 0) != 1) throw std::runtime_error("ProjectSettings/Project.json: unsupported format version");
    if (!readText(settings / "InputActions.json", txt)) throw std::runtime_error("ProjectSettings/InputActions.json: cannot open");
    inputsJson = json::parse(txt);
  } catch (const std::exception& e) {
    cx.error(e.what());
    return R;
  }
  ICookerFormatPlugin* meshPlugin = plugin;
  if (!plugin->isProfile()) {
    // Platform target (linux, windows, android): meshes use the project's console profile.
    meshPlugin = CookerPluginRegistry::instance().find(target.profile);
  } else if (target.profile != opt.target) {
    cx.error("ConsoleTarget profile '" + target.profile + "' does not match requested target '" + opt.target + "'");
    return R;
  }

  if (!opt.genHeader.empty()) {
    fs::create_directories(opt.genHeader.parent_path());
    std::string header = generateConsoleTargetHeader(target), old;
    if (!readText(opt.genHeader, old) || old != header) std::ofstream(opt.genHeader, std::ios::binary) << header;
  }

  // ---- asset index (GUID -> file) from the .meta next to every asset
  std::map<std::string, Asset> assets;
  std::vector<fs::path> hashed;
  const fs::path assetsDir = proj / "Assets";
  if (fs::exists(assetsDir)) {
    for (const auto& e : fs::recursive_directory_iterator(assetsDir)) {
      if (!e.is_regular_file()) continue;
      fs::path p = e.path();
      std::string ext = p.extension().string();
      if (ext == ".cpp" || ext == ".h") continue;
      hashed.push_back(p);
      if (ext != ".meta") continue;
      std::string txt;
      try {
        readText(p, txt);
        json metaJson = json::parse(txt);
        std::string g = metaJson.at("guid").get<std::string>();
        Guid parsed;
        if (!Guid::parse(g.c_str(), parsed)) throw std::runtime_error("bad guid '" + g + "'");
        Asset a;
        a.path = p;
        a.path.replace_extension("");
        a.rel = relPath(proj, a.path);
        a.guid = g;
        a.importSettings = effectiveImportSettings(metaJson, opt.target);
        if (assets.count(g)) {
          cx.error("duplicate asset GUID " + g + " in '" + a.rel + "' and '" + assets[g].rel + "'", a.rel, g);
        }
        assets[g] = a;
      } catch (const std::exception& ex) {
        cx.error(std::string("invalid .meta: ") + ex.what(), relPath(proj, p));
      }
    }
  }
  if (!R.errors.empty()) return R;
  for (const auto& e : fs::directory_iterator(settings))
    if (e.is_regular_file()) hashed.push_back(e.path());
  std::sort(hashed.begin(), hashed.end());

  // ---- content-hash cache: skip when no input (or reflected layout) changed
  const fs::path outDir = opt.outDir.empty() ? proj / "Library" / "Cooked" / opt.target : fs::absolute(opt.outDir);
  R.packPath = outDir / "game.rpak";
  R.reportPath = outDir / "budget-report.txt";
  // Streamed audio (F2.04): files in <project>/Streaming/ are not packed. They are copied next to the pack and read
  // through IFileSystem in chunks at run time (data:/music.wav). Done before the cache check so the copy is never skipped.
  if (fs::is_directory(proj / "Streaming")) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    for (const auto& e : fs::directory_iterator(proj / "Streaming")) {
      if (!e.is_regular_file()) continue;
      const fs::path dst = outDir / e.path().filename();
      if (!fs::exists(dst, ec) || fs::file_size(dst, ec) != e.file_size() || fs::last_write_time(dst, ec) < e.last_write_time())
        fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec);
      if (ec) cx.error("cannot copy streamed file " + e.path().filename().string() + ": " + ec.message(), "Streaming/" + e.path().filename().string());
    }
  }
  std::map<std::string, std::string> hashes;
  hashes["<cooker>"] = kCookerVersion;
  {
    hashes["<reflection>"] = reflectionLayoutHash();
  }
  for (const fs::path& p : hashed) {
    std::string txt;
    auto ov = opt.sceneOverrides.find(relPath(proj, p));
    if (ov != opt.sceneOverrides.end()) txt = ov->second;
    else readText(p, txt);
    hashes[relPath(proj, p)] = hex64(fnv64(txt));
  }
  const fs::path manifestPath = outDir / "cook.manifest.json";
  std::map<std::string, std::string> previous;
  {
    std::string txt;
    if (readText(manifestPath, txt)) {
      try {
        previous = json::parse(txt).get<std::map<std::string, std::string>>();
      } catch (...) {
      }
    }
  }
  if (!opt.force && fs::exists(R.packPath) && previous == hashes) {
    cx.info("cook: up to date (no input changed)");
    cx.info("cook: cooked 0, cached 0 (whole project up to date)");
    R.ok = true;
    R.upToDate = true;
    return R;
  }
  for (const auto& kv : hashes) {
    auto it = previous.find(kv.first);
    if (it == previous.end() || it->second != kv.second) cx.info("cook: changed " + kv.first);
  }

  // ---- scene selection: first scene listed in Project.json
  std::vector<std::string> sceneGuids;
  for (const json& s : projectJson.value("scenes", json::array())) sceneGuids.push_back(s.get<std::string>());
  if (sceneGuids.empty()) {
    cx.error("ProjectSettings/Project.json lists no scenes");
    return R;
  }
  // F2.05: every scene listed in Project.json is cooked into the one pack. Their objects are concatenated in
  // build-settings order (one contiguous block per scene, recorded in SCNE), so SceneManager can load any of them.
  struct SceneBlock { std::string guid, name; u32 first = 0, count = 0; };
  std::vector<SceneBlock> sceneBlocks;
  const Asset* firstScene = nullptr;
  json scene;
  for (size_t si = 0; si < sceneGuids.size(); ++si) {
    auto sit = assets.find(sceneGuids[si]);
    if (sit == assets.end()) {
      cx.error("Project.json references scene GUID " + sceneGuids[si] + " but no asset has that GUID", "ProjectSettings/Project.json", sceneGuids[si]);
      return R;
    }
    const Asset& sa = sit->second;
    json one;
    std::string txt;
    auto ov = opt.sceneOverrides.find(sa.rel);
    if (ov != opt.sceneOverrides.end()) txt = ov->second;
    else readText(sa.path, txt);
    try {
      one = json::parse(txt);
      if (one.value("format", 0) != 1) throw std::runtime_error("unsupported scene format version");
    } catch (const std::exception& e) {
      cx.error(std::string("scene parse error: ") + e.what(), sa.rel, sa.guid);
      return R;
    }
    // --- F6 A1 --- prefab instances become plain objects (the pack has no prefab concept); prefab Guid fields get templates
    {
      std::map<std::string, std::string> prefabFiles;
      for (const auto& kv : assets)
        if (fs::path(kv.second.rel).extension() == ".prefab") prefabFiles[kv.first] = kv.second.path.string();
      std::vector<std::string> perr, pnotes;
      if (!prefabFiles.empty() || rensga::editor::hasPrefabInstances(one.value("objects", json::array()))) {
        if (!cookExpandPrefabs(one, sa.guid, prefabFiles, perr, pnotes)) {
          for (const std::string& e : perr) cx.error(e, sa.rel, sa.guid);
          return R;
        }
        for (const std::string& n : pnotes) cx.info(n);
      }
    }
    // --- end F6 A1 ---
    SceneBlock blk;
    blk.guid = sceneGuids[si];
    blk.name = fs::path(sa.rel).stem().string();
    blk.count = static_cast<u32>(one.value("objects", json::array()).size());
    if (si == 0) {
      firstScene = &sa;
      scene = one;
      blk.first = 0;
    } else {
      blk.first = static_cast<u32>(scene.at("objects").size());
      for (const json& oj : one.value("objects", json::array())) scene["objects"].push_back(oj);
    }
    sceneBlocks.push_back(blk);
  }
  const Asset& sceneAsset = *firstScene;

  // ---- scene objects through reflection
  std::vector<ObjectDef> objs;
  std::map<std::string, int> objIndex;
  std::set<std::string> allowed;  // canonical names, so a ConsoleTarget written before a type rename still works
  for (const std::string& n : target.allowedComponents) allowed.insert(TypeRegistry::canonicalName(n.c_str()));
  try {
    for (const json& oj : scene.at("objects")) {
      ObjectDef o;
      std::string g = oj.at("guid").get<std::string>();
      if (!Guid::parse(g.c_str(), o.guid)) {
        cx.error("object has invalid GUID '" + g + "'", sceneAsset.rel, sceneAsset.guid);
        continue;
      }
      if (objIndex.count(g)) {
        cx.error("duplicate object GUID " + g, sceneAsset.rel, sceneAsset.guid);
        continue;
      }
      o.name = oj.value("name", "");
      o.active = oj.value("active", true);
      o.tag = oj.value("tag", "");
      o.layer = oj.value("layer", 0);
      if (oj.contains("parent") && !oj["parent"].is_null()) o.parentGuid = oj["parent"].get<std::string>();
      Vector3 p, s(1, 1, 1);
      Quaternion q;
      if (oj.contains("transform")) {
        const json& t = oj["transform"];
        if (t.contains("position")) p = Vector3(t["position"][0].get<float>(), t["position"][1].get<float>(), t["position"][2].get<float>());
        if (t.contains("rotation")) q = Quaternion(t["rotation"][0].get<float>(), t["rotation"][1].get<float>(), t["rotation"][2].get<float>(), t["rotation"][3].get<float>());
        if (t.contains("scale")) s = Vector3(t["scale"][0].get<float>(), t["scale"][1].get<float>(), t["scale"][2].get<float>());
      }
      o.xf.position[0] = p.x; o.xf.position[1] = p.y; o.xf.position[2] = p.z;
      o.xf.rotation[0] = q.x; o.xf.rotation[1] = q.y; o.xf.rotation[2] = q.z; o.xf.rotation[3] = q.w;
      o.xf.scale[0] = s.x; o.xf.scale[1] = s.y; o.xf.scale[2] = s.z;
      for (const json& cj : oj.value("components", json::array())) {
        std::string tn = cj.at("type").get<std::string>();
        const TypeInfo* ti = TypeRegistry::instance().find(tn.c_str());
        if (!ti || !ti->asComponent) {
          cx.error("unknown component type '" + tn + "' on object '" + o.name + "' (guid " + g +
                       "); is the script compiled into the cooker (RENSGA_PROJECT_DIR)?",
                   sceneAsset.rel, sceneAsset.guid);
          continue;
        }
        if (!allowed.count(ti->name)) {
          cx.error("component '" + tn + "' on object '" + o.name + "' (guid " + g + ") is not allowed by the " +
                       target.profile + " ConsoleTarget",
                   sceneAsset.rel, sceneAsset.guid);
          continue;
        }
        Instance inst(ti);
        std::string ferr;
        if (!fieldsFromJson(*ti, inst.mem, cj.value("fields", json::object()), &ferr)) {
          cx.error("object '" + o.name + "' (guid " + g + "): " + ferr, sceneAsset.rel, sceneAsset.guid);
          continue;
        }
        ObjectDef::Comp c;
        c.type = ti;
        c.blob.resize(blobSize(*ti));
        writeBlob(*ti, inst.mem, c.blob.data());
        // F6: fields appended after a type shipped (APPENDED_FIELDS) are left out of the blob while they hold their default, so
        // a scene that never touches them cooks to the same bytes as before they existed.
        if (ti->trimFloor != kNoTrim) {
          Instance defaults(ti);
          c.blob.resize(trimmedBlobSize(*ti, inst.mem, defaults.mem));
        }
        c.fields = fieldsToJson(*ti, inst.mem);
        c.srcFields = cj.value("fields", json::object());  // as written: error messages name methods, not hashes
        o.comps.push_back(std::move(c));
      }
      objIndex[g] = static_cast<int>(objs.size());
      objs.push_back(std::move(o));
    }
  } catch (const std::exception& e) {
    cx.error(std::string("scene structure error: ") + e.what(), sceneAsset.rel, sceneAsset.guid);
  }
  if (!R.errors.empty()) return R;

  for (ObjectDef& o : objs) {
    if (o.parentGuid.empty()) continue;
    auto it = objIndex.find(o.parentGuid);
    if (it == objIndex.end()) {
      cx.error("object '" + o.name + "' (guid " + guidText(o.guid) + ") has unknown parent " + o.parentGuid, sceneAsset.rel, sceneAsset.guid);
    } else {
      o.parent = it->second;
    }
  }
  // Guid fields must reference an object or asset in the project.
  for (const ObjectDef& o : objs)
    for (const auto& c : o.comps)
      for (u32 f = 0; f < c.type->fieldCount; ++f) {
        const FieldDesc& fd = c.type->fields[f];
        const bool guidArray = fd.type == FieldType::Array && fd.elemType == FieldType::Guid;  // F6: SERIALIZE_ARRAY(Guid, ...)
        if (fd.type != FieldType::Guid && !guidArray) continue;
        if (strcmp(c.type->name, "Animation") == 0 && strcmp(fd.name, "clip") == 0) continue;  // checked once the clips are cooked
        std::vector<std::string> refs;
        if (guidArray) {
          if (c.fields.contains(fd.name) && c.fields[fd.name].is_array())
            for (const auto& e : c.fields[fd.name]) refs.push_back(e.is_string() ? e.get<std::string>() : std::string());
        } else {
          refs.push_back(c.fields.value(fd.name, std::string()));
        }
        for (const std::string& g : refs) {
          Guid parsed;
          if (!Guid::parse(g.c_str(), parsed) || parsed.isNull()) continue;
          if (!objIndex.count(g) && !assets.count(g))
            cx.error("'" + o.name + "' (guid " + guidText(o.guid) + ") " + c.type->name + "." + fd.name + " references missing GUID " + g,
                     sceneAsset.rel, sceneAsset.guid);
        }
      }
  // UnityEvent listeners must name an object of this scene, a component type on it and a METHOD of that type.
  for (const ObjectDef& o : objs)
    for (const auto& c : o.comps)
      for (u32 f = 0; f < c.type->fieldCount; ++f) {
        if (c.type->fields[f].type != FieldType::UnityEvent || !c.fields.contains(c.type->fields[f].name)) continue;
        const auto& arr = c.srcFields.contains(c.type->fields[f].name) ? c.srcFields[c.type->fields[f].name] : c.fields[c.type->fields[f].name];
        if (!arr.is_array()) continue;  // reflect_json reports the shape error
        for (const auto& l : arr) {
          std::string where = "'" + o.name + "' (guid " + guidText(o.guid) + ") " + c.type->name + "." + c.type->fields[f].name + " listener ";
          std::string tg = l.value("target", std::string());
          auto it = objIndex.find(tg);
          if (it == objIndex.end()) { cx.error(where + "targets missing object " + tg, sceneAsset.rel, sceneAsset.guid); continue; }
          std::string tn = l.value("type", std::string()), mn = l.value("method", std::string());
          const TypeInfo* tt = TypeRegistry::instance().find(tn.c_str());
          if (!tt) { cx.error(where + "names unknown component type '" + tn + "'", sceneAsset.rel, sceneAsset.guid); continue; }
          if (!MethodRegistry::instance().find(*tt, mn.c_str())) {
            cx.error(where + "names method '" + tn + "." + mn + "' that is not declared with METHOD(" + mn + ")", sceneAsset.rel, sceneAsset.guid);
            continue;
          }
          bool has = false;
          for (const auto& oc : objs[static_cast<size_t>(it->second)].comps) has = has || oc.type == tt;
          if (!has) cx.error(where + "target object has no " + tn + " component", sceneAsset.rel, sceneAsset.guid);
        }
      }
  if (!R.errors.empty()) return R;

  // World rotation per object (parents may follow children in the file).
  std::vector<Quaternion> worldRot(objs.size());
  std::vector<char> rotDone(objs.size(), 0);
  std::function<Quaternion(size_t, int)> rotOf = [&](size_t i, int depth) -> Quaternion {
    if (rotDone[i]) return worldRot[i];
    const PackTransform& x = objs[i].xf;
    Quaternion local(x.rotation[0], x.rotation[1], x.rotation[2], x.rotation[3]);
    if (objs[i].parent >= 0 && depth < 64) local = rotOf(static_cast<size_t>(objs[i].parent), depth + 1) * local;
    worldRot[i] = local;
    rotDone[i] = 1;
    return local;
  };

  BakedLight light;
  for (const ObjectDef& o : objs)
    for (const auto& c : o.comps)
      if (strcmp(c.type->name, "DirectionalLight") == 0) {
        Instance inst(c.type);
        readBlob(*c.type, inst.mem, c.blob.data(), static_cast<u32>(c.blob.size()));
        DirectionalLight* dl = static_cast<DirectionalLight*>(inst.mem);
        light.direction = dl->direction;
        light.color = dl->color;
        goto lightDone;
      }
lightDone:

  // ---- meshes: one baked variant per MeshRenderer
  std::vector<PackMeshRec> meshRecs;
  std::vector<uint8_t> vertBytes;
  std::map<std::string, std::pair<PrimitiveParams, PrimitiveMesh>> primCache;
  SceneBudgetInput budget;
  budget.sceneAsset = sceneAsset.rel;
  budget.sceneGuid = sceneAsset.guid;
  const TypeInfo* mrType = TypeRegistry::instance().find("MeshRenderer");
  const TypeInfo* smrType = TypeRegistry::instance().find("SkinnedMeshRenderer");
  // F5 shared 3D state (empty for a project with no material and no skinned mesh: no new section is written).
  std::vector<PackMeshLit> meshLit;  // parallel to meshRecs
  std::vector<uint8_t> vtx3;
  std::vector<PackBoneBatch> allBatches;
  std::vector<MatEntry> mats;
  std::map<std::string, uint32_t> matIndex;
  std::vector<SkinEntry> skinsOut;
  std::map<std::string, uint32_t> skinIndex;
  std::map<std::string, LitCached> litCache;
  std::map<std::string, GltfModel> gltfCache;
  bool anyLit = false;
  struct LitUse { size_t obj; float radius; Vector3 center; };
  std::vector<LitUse> litUses;
  PackMeshLit bakedLit = {};
  bakedLit.material = bakedLit.skin = kInvalidIndex;
  std::map<std::string, std::string> guidOfPath;  // canonical asset file path -> asset GUID (glTF material images)
  for (const auto& kv : assets) {
    std::error_code ec;
    guidOfPath[fs::weakly_canonical(kv.second.path, ec).generic_string()] = kv.first;
  }
  const uint32_t maxBones = target.maxBonesPerDraw ? std::min<uint32_t>(target.maxBonesPerDraw, kMaxBatchBones) : kMaxBatchBones;
  const bool ownCompact = plugin->isProfile() && plugin->hasCompactMeshes();

  auto warnAsset = [&](const std::string& text, const Asset& a) { cx.msg(CookMessage::Warning, text + " ('" + a.rel + "')", a.rel, a.guid); };
  auto addMaterial = [&](const Guid& g, const MaterialDef& d, const std::string& rel) -> uint32_t {
    const std::string key = guidText(g);
    auto it = matIndex.find(key);
    if (it != matIndex.end()) return it->second;
    matIndex[key] = static_cast<uint32_t>(mats.size());
    mats.push_back({g, d, rel});
    return matIndex[key];
  };

  // Lit / skinned renderer: MeshRenderer with a material, or a SkinnedMeshRenderer. Returns false after reporting an error.
  auto cookLit = [&](size_t i, const ObjectDef::Comp& c, bool smr) -> bool {
    const std::string who = (smr ? "SkinnedMeshRenderer" : "MeshRenderer") + std::string(" on '") + objs[i].name + "' (guid " + guidText(objs[i].guid) + ")";
    const std::string mg = c.fields.value("mesh", std::string());
    Guid matG, meshG;
    Guid::parse(c.fields.value("material", std::string()).c_str(), matG);
    if (smr && (!Guid::parse(mg.c_str(), meshG) || meshG.isNull())) {  // nothing to draw yet (Unity allows an empty mesh slot)
      cx.msg(CookMessage::Warning, who + " has no mesh; it draws nothing", sceneAsset.rel, sceneAsset.guid);
      return true;
    }
    auto ait = assets.find(mg);
    const bool isGltf = ait != assets.end() && isGltfRel(ait->second.rel);
    const bool isPrim = ait != assets.end() && ait->second.rel.find(".prim.json") != std::string::npos;
    if (!isGltf && !isPrim) {
      cx.error(who + " references mesh GUID " + mg + ", which is not a primitive or glTF asset", sceneAsset.rel, sceneAsset.guid);
      return false;
    }
    if (smr && !isGltf) {
      cx.error(who + " needs a glTF mesh with a skin; '" + ait->second.rel + "' is a primitive", ait->second.rel, mg);
      return false;
    }
    const Asset& src = ait->second;
    const std::string key = mg + (smr ? "|S" : "|M");
    auto cit = litCache.find(key);
    if (cit == litCache.end()) {
      LitCached lc;
      lc.desc.material = lc.desc.skin = kInvalidIndex;
      LitMesh lm;
      std::vector<SkinInfluences> influences;
      if (isGltf) {
        auto git = gltfCache.find(key);
        if (git == gltfCache.end()) {
          GltfModel gm;
          ModelImport mi;
          std::string merr;
          if (!parseModelImport(src.importSettings, mi, merr)) {
            cx.error("bad importSettings: " + merr, src.rel, mg);
            return false;
          }
          std::string gerr;
          if (!importGltfModel(src.path, mi, smr, 30, gm, gerr)) {
            cx.error("glTF import failed: " + gerr, src.rel, mg);
            return false;
          }
          for (const std::string& w : gm.warnings) warnAsset(w, src);
          git = gltfCache.emplace(key, std::move(gm)).first;
        }
        const GltfModel& gm = git->second;
        if (smr && !gm.skinned) {
          cx.error(who + ": the glTF '" + src.rel + "' has no skin", src.rel, mg);
          return false;
        }
        lm = gm.mesh;
        if (smr) {
          influences = gm.influences;
          // One SKIN (and its CLP3 clips) per glTF.
          if (!skinIndex.count(mg)) {
            if (target.maxSkinnedJoints && gm.joints.size() > target.maxSkinnedJoints) {
              cx.error("skinned mesh '" + src.rel + "' has " + std::to_string(gm.joints.size()) + " joints, the " + target.profile +
                           " profile allows at most " + std::to_string(target.maxSkinnedJoints) + " (ConsoleTarget limits.maxSkinnedJoints)",
                       src.rel, mg);
              return false;
            }
            SkinEntry se;
            Guid::parse(mg.c_str(), se.guid);
            se.rel = src.rel;
            se.joints = gm.joints;
            se.clips = gm.clips;
            skinIndex[mg] = static_cast<uint32_t>(skinsOut.size());
            skinsOut.push_back(std::move(se));
          }
          lc.skin = skinIndex[mg];
        }
        if (gm.meshMaterial >= 0 && static_cast<size_t>(gm.meshMaterial) < gm.materials.size()) {
          const GltfMaterialInfo& gi = gm.materials[static_cast<size_t>(gm.meshMaterial)];
          lc.hasDerivedMaterial = true;
          Guid g;
          Guid::parse(mg.c_str(), g);
          lc.derivedMaterial = deriveGuid(g, kDerivedMaterialSalt);
          for (int k = 0; k < 4; ++k) lc.derivedDef.color[k] = static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(1.0f, gi.color[k])) * 255.0f));
          lc.derivedDef.unlit = gi.unlit;
          lc.derivedDef.alphaTest = gi.alphaTest;
          if (!gi.imagePath.empty()) {
            std::error_code ec;
            auto pit = guidOfPath.find(fs::weakly_canonical(gi.imagePath, ec).generic_string());
            if (pit != guidOfPath.end()) lc.derivedDef.textureGuid = pit->second;
            else lc.derivedWarning = "the glTF material's image '" + gi.imagePath.filename().string() + "' is not an asset of the project (add it, or set a .mat.json)";
          }
        }
      } else {
        std::string txt, perr;
        readText(src.path, txt);
        PrimitiveParams pp;
        if (!parsePrimitive(txt, pp, perr)) {
          cx.error("primitive parse error: " + perr, src.rel, mg);
          return false;
        }
        lm = litMeshFromPrimitive(generatePrimitive(pp, target));
      }
      // Skin split into batches of at most maxBones joints.
      uint32_t firstBatch = static_cast<uint32_t>(allBatches.size()), batchCount = 0;
      if (smr) {
        SkinSplitResult sr;
        std::string warn, err;
        if (!splitSkinnedMesh(lm, influences, maxBones, sr, warn, err)) {
          cx.error("skin split failed: " + err, src.rel, mg);
          return false;
        }
        if (!warn.empty()) warnAsset(warn, src);
        if (sr.unweightedVertices) warnAsset(std::to_string(sr.unweightedVertices) + " vertices have no skin weight and follow the first bone of their batch", src);
        lm = std::move(sr.mesh);
        batchCount = static_cast<uint32_t>(sr.batches.size());
        allBatches.insert(allBatches.end(), sr.batches.begin(), sr.batches.end());
      }
      std::string note;
      const uint32_t flags = analyzeMeshFlags(lm, note);
      if (!note.empty()) warnAsset(note, src);
      const size_t before = vtx3.size();
      PackMeshLit d = {};
      std::string cerr;
      if (!plugin->cookLitMesh(lm, vtx3, d, cerr)) {
        cx.error("cookLitMesh failed for '" + src.rel + "': " + cerr, src.rel, mg);
        return false;
      }
      if (ownCompact && !d.isC16())
        warnAsset("UV coordinates leave the range [0, 2), so the mesh is stored with float vertices instead of the compact 16-bit form (about twice the vertex bytes); keep UVs inside [0, 2)", src);
      d.vertexByteOffset = static_cast<uint32_t>(before);
      d.material = kInvalidIndex;
      d.skin = lc.skin;
      d.firstBatch = firstBatch;
      d.batchCount = batchCount;
      d.flags = flags;
      lc.desc = d;
      cit = litCache.emplace(key, lc).first;
    }
    const LitCached& lc = cit->second;
    PackMeshLit d = lc.desc;
    if (!matG.isNull()) {
      auto mit = assets.find(guidText(matG));
      if (mit == assets.end() || !endsWith(mit->second.rel, ".mat.json")) {
        cx.error(who + " references material GUID " + guidText(matG) + ", which is not a .mat.json asset", sceneAsset.rel, sceneAsset.guid);
        return false;
      }
      std::string txt, merr;
      MaterialDef md;
      readText(mit->second.path, txt);
      if (!parseMaterialJson(txt, md, merr)) {
        cx.error("material parse error: " + merr, mit->second.rel, mit->first);
        return false;
      }
      d.material = addMaterial(matG, md, mit->second.rel);
    } else if (smr && lc.hasDerivedMaterial) {
      if (!lc.derivedWarning.empty()) warnAsset(lc.derivedWarning, src);
      d.material = addMaterial(lc.derivedMaterial, lc.derivedDef, src.rel);
    }
    PackMeshRec rec = {};
    rec.objectIndex = static_cast<uint32_t>(i);
    meshRecs.push_back(rec);
    meshLit.push_back(d);
    anyLit = true;
    litUses.push_back({i, d.boundsRadius, Vector3(d.boundsCenter[0], d.boundsCenter[1], d.boundsCenter[2])});
    if (objs[i].active) {
      const uint64_t tris = d.vertexCount / 3;
      budget.triangles += tris;
      if (tris > budget.heaviestTriangles) {
        budget.heaviestTriangles = tris;
        budget.heaviestObjectName = objs[i].name;
        budget.heaviestObjectGuid = guidText(objs[i].guid);
      }
    }
    return true;
  };

  for (size_t i = 0; i < objs.size(); ++i) {
    for (const auto& c : objs[i].comps) {
      if (c.type != mrType && c.type != smrType) continue;
      {
        Guid mat;
        Guid::parse(c.fields.value("material", std::string()).c_str(), mat);
        if (c.type == smrType || !mat.isNull()) {
          cookLit(i, c, c.type == smrType);
          continue;
        }
      }
      std::string mg = c.fields.value("mesh", std::string());
      auto pit = primCache.find(mg);
      if (pit == primCache.end()) {
        auto ait = assets.find(mg);
        const bool isPrim = ait != assets.end() && ait->second.rel.find(".prim.json") != std::string::npos;
        const bool isGltf = ait != assets.end() && (ait->second.rel.size() > 5 && (ait->second.rel.compare(ait->second.rel.size() - 5, 5, ".gltf") == 0 ||
                                                                                   ait->second.rel.compare(ait->second.rel.size() - 4, 4, ".glb") == 0));
        if (!isPrim && !isGltf) {
          cx.error("MeshRenderer on '" + objs[i].name + "' (guid " + guidText(objs[i].guid) + ") references mesh GUID " + mg +
                       ", which is not a primitive or glTF asset",
                   sceneAsset.rel, sceneAsset.guid);
          continue;
        }
        if (isGltf) {
          PrimitiveMesh gm;
          std::string gerr;
          if (!importGltf(ait->second.path, gm, gerr)) {
            cx.error("glTF import failed: " + gerr, ait->second.rel, mg);
            continue;
          }
          ModelImport mi;
          std::string merr;
          if (!parseModelImport(ait->second.importSettings, mi, merr)) {
            cx.error("bad importSettings: " + merr, ait->second.rel, mg);
            continue;
          }
          applyModelImport(gm, mi);
          pit = primCache.emplace(mg, std::make_pair(PrimitiveParams(), std::move(gm))).first;
        } else {
          std::string txt, perr;
          readText(ait->second.path, txt);
          PrimitiveParams pp;
          if (!parsePrimitive(txt, pp, perr)) {
            cx.error("primitive parse error: " + perr, ait->second.rel, mg);
            continue;
          }
          pit = primCache.emplace(mg, std::make_pair(pp, generatePrimitive(pp, target))).first;
        }
      }
      PrimitiveMesh m = pit->second.second;
      Quaternion wr = rotOf(i, 0);
      for (Vector3& n : m.normals) n = wr * n;
      Color32 base(255, 255, 255, 255);
      {
        Instance inst(c.type);
        readBlob(*c.type, inst.mem, c.blob.data(), static_cast<u32>(c.blob.size()));
        base = static_cast<MeshRenderer*>(inst.mem)->color;
      }
      uint32_t count = 0;
      std::string cerr;
      size_t firstByte = vertBytes.size();
      if (!meshPlugin || !meshPlugin->cookMesh(m, light, base, vertBytes, count, cerr)) {
        if (!meshPlugin) cerr = "no mesh format plugin for profile '" + target.profile + "'";
        cx.error("cookMesh failed for '" + objs[i].name + "': " + cerr, sceneAsset.rel, sceneAsset.guid);
        continue;
      }
      PackMeshRec rec;
      rec.vertexOffset = static_cast<uint32_t>(firstByte / sizeof(PackVertex));
      rec.vertexCount = count;
      rec.primitiveType = 0;
      rec.objectIndex = static_cast<uint32_t>(i);
      meshRecs.push_back(rec);
      meshLit.push_back(bakedLit);
      if (objs[i].active) {
        uint64_t tris = count / 3;
        budget.triangles += tris;
        if (tris > budget.heaviestTriangles) {
          budget.heaviestTriangles = tris;
          budget.heaviestObjectName = objs[i].name;
          budget.heaviestObjectGuid = guidText(objs[i].guid);
        }
      }
    }
  }
  if (!R.errors.empty()) return R;

  // ---- F5 validation: lights that reach one lit object (the profile draws at most maxLightsPerObject)
  if (anyLit && target.maxLightsPerObject) {
    struct Xf { Vector3 p, s; bool done = false; };
    std::vector<Xf> wx(objs.size());
    std::function<const Xf&(size_t, int)> xfOf = [&](size_t i, int depth) -> const Xf& {
      Xf& o = wx[i];
      if (o.done) return o;
      const PackTransform& t = objs[i].xf;
      Vector3 lp(t.position[0], t.position[1], t.position[2]), ls(t.scale[0], t.scale[1], t.scale[2]);
      if (objs[i].parent >= 0 && depth < 64) {
        const Xf& par = xfOf(static_cast<size_t>(objs[i].parent), depth + 1);
        Quaternion pr = rotOf(static_cast<size_t>(objs[i].parent), 0);
        o.p = par.p + pr * Vector3(lp.x * par.s.x, lp.y * par.s.y, lp.z * par.s.z);
        o.s = Vector3(ls.x * par.s.x, ls.y * par.s.y, ls.z * par.s.z);
      } else {
        o.p = lp;
        o.s = ls;
      }
      o.done = true;
      return o;
    };
    auto blockOf = [&](size_t i) {
      for (size_t b = 0; b < sceneBlocks.size(); ++b)
        if (i >= sceneBlocks[b].first && i < static_cast<size_t>(sceneBlocks[b].first) + sceneBlocks[b].count) return b;
      return size_t(0);
    };
    struct LightRef { size_t obj; int type; float range; };
    std::vector<LightRef> lights;
    const TypeInfo* lightType = TypeRegistry::instance().find("Light");
    for (size_t i = 0; i < objs.size(); ++i)
      for (const auto& c : objs[i].comps)
        if (c.type == lightType && objs[i].active) {
          Instance inst(c.type);
          readBlob(*c.type, inst.mem, c.blob.data(), static_cast<u32>(c.blob.size()));
          const Light* l = static_cast<const Light*>(inst.mem);
          lights.push_back({i, l->type, l->range});
        }
    std::set<size_t> warned;
    for (const LitUse& u : litUses) {
      if (!objs[u.obj].active || warned.count(u.obj)) continue;
      const Xf& ox = xfOf(u.obj, 0);
      const Vector3 c = ox.p + rotOf(u.obj, 0) * Vector3(u.center.x * ox.s.x, u.center.y * ox.s.y, u.center.z * ox.s.z);
      const float rad = u.radius * std::max(std::fabs(ox.s.x), std::max(std::fabs(ox.s.y), std::fabs(ox.s.z)));
      const size_t blk = blockOf(u.obj);
      uint32_t n = 0;
      for (const LightRef& l : lights) {
        if (blockOf(l.obj) != blk) continue;
        if (l.type == 0) { ++n; continue; }
        if ((xfOf(l.obj, 0).p - c).magnitude() <= l.range + rad) ++n;
      }
      if (n > target.maxLightsPerObject) {
        warned.insert(u.obj);
        cx.msg(CookMessage::Warning, "object '" + objs[u.obj].name + "' (guid " + guidText(objs[u.obj].guid) + ") is reached by " + std::to_string(n) +
                                         " lights, the " + target.profile + " profile draws at most " + std::to_string(target.maxLightsPerObject) +
                                         " per object (ConsoleTarget limits.maxLightsPerObject); the strongest are used",
               sceneAsset.rel, sceneAsset.guid);
      }
    }
  }

  // ---- compact meshes (F4.05): only when cooking for the profile's own target; a platform cook keeps float vertices
  PackMeshExt meshExt = {};
  bool compactMeshes = false;
  if (plugin->isProfile() && plugin->hasCompactMeshes() && !vertBytes.empty()) {
    std::vector<uint8_t> compact;
    std::string cerr;
    if (plugin->compactMeshes(vertBytes, compact, meshExt, cerr)) {
      vertBytes.swap(compact);
      compactMeshes = true;
    } else if (!cerr.empty()) {
      cx.error("compact meshes: " + cerr);
      return R;
    }
  }

  // ---- textures, audio, tilemaps, clips, fonts (pack v2)
  CookedAssets cooked;
  {
    std::vector<AssetSource> srcs;
    for (const auto& kv : assets) {
      AssetSource a;
      a.path = kv.second.path;
      a.rel = kv.second.rel;
      a.guid = kv.second.guid;
      a.importSettings = kv.second.importSettings;
      if (opt.target == target.profile && a.rel.size() > 4 && a.rel.compare(a.rel.size() - 4, 4, ".wav") == 0)  // F5.17 (A6): console packs resample to the target rate; PC platform targets keep the source
        a.importSettings = withTargetAudioRate(a.importSettings, target.audioSampleRate);
      srcs.push_back(std::move(a));
    }
    // Per-asset cache: Library/Cache/<target>/. The salt covers everything besides the asset itself that changes cooked bytes.
    AssetCache cache;
    cache.dir = proj / "Library" / "Cache" / opt.target;
    cache.reuse = !opt.force;
    const std::string consoleKey = "ProjectSettings/ConsoleTarget.json";
    cache.salt = std::string(kCookerVersion) + "|" + opt.target + "|" + hashes[consoleKey] + "|" + hashes["<reflection>"];
    cookAssets(srcs, *plugin, cooked, [&](CookMessage::Severity sv, const std::string& t, const std::string& rel, const std::string& g) { cx.msg(sv, t, rel, g); },
               &cache);
    R.assetsCooked = cache.cooked;
    R.assetsCached = cache.cached;
    cx.info("cook: cooked " + std::to_string(cache.cooked) + ", cached " + std::to_string(cache.cached));
  }
  if (!R.errors.empty()) return R;

  // ---- F5: textures the cook scaled down (maxTextureSize of the profile, or the asset's own maxSize) are reported
  for (const auto& kv : assets) {
    if (!endsWith(kv.second.rel, ".png")) continue;
    Guid tg;
    Guid::parse(kv.first.c_str(), tg);
    for (const PackTexRec& tr : cooked.textures) {
      if (tr.guid != tg) continue;
      std::string bytes;
      uint32_t w = 0, h = 0;
      if (readText(kv.second.path, bytes) && pngSize(std::vector<uint8_t>(bytes.begin(), bytes.end()), w, h) && (tr.width < w || tr.height < h))
        cx.msg(CookMessage::Warning,
               "texture '" + kv.second.rel + "' downscaled from " + std::to_string(w) + "x" + std::to_string(h) + " to " + std::to_string(tr.width) + "x" +
                   std::to_string(tr.height) + " (box filter" +
                   (target.maxTextureSize ? ", " + target.profile + " maxTextureSize " + std::to_string(target.maxTextureSize) : std::string()) + ")",
               kv.second.rel, kv.first);
      break;
    }
  }

  // ---- assemble the pack
  struct Sec { u32 fourcc; std::vector<uint8_t> data; u32 count; };
  std::vector<Sec> secs;
  auto bytesOf = [](const void* p, size_t n) {
    return std::vector<uint8_t>(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
  };
  std::vector<uint8_t> strs(1, 0);  // offset 0 = ""
  u32 strCount = 1;
  std::map<std::string, u32> strIndex;  // string -> index in STRS (OBJS tag field)
  auto addStr = [&](const std::string& s) {
    if (s.empty()) return u32(0);
    u32 off = static_cast<u32>(strs.size());
    strs.insert(strs.end(), s.begin(), s.end());
    strs.push_back(0);
    strIndex[s] = strCount;
    ++strCount;
    return off;
  };

  std::vector<PackObject> pobjs;
  std::vector<PackTransform> pxf;
  std::vector<PackComponent> pcomps;
  std::vector<uint8_t> fdat;
  std::map<std::string, uint64_t> perType;
  for (size_t i = 0; i < objs.size(); ++i) {
    PackObject po = {};
    po.guid = objs[i].guid;
    po.nameStr = addStr(objs[i].name);
    po.parentIndex = objs[i].parent;
    po.active = objs[i].active ? 1 : 0;
    po.layer = static_cast<u16>(objs[i].layer);
    if (!objs[i].tag.empty() && objs[i].tag != "Untagged") {
      if (!strIndex.count(objs[i].tag)) addStr(objs[i].tag);
      po.tag = static_cast<u16>(strIndex[objs[i].tag]);
    }
    pobjs.push_back(po);
    pxf.push_back(objs[i].xf);
    for (const auto& c : objs[i].comps) {
      while (fdat.size() % 4) fdat.push_back(0);
      PackComponent pc;
      pc.typeHash = c.type->hash;
      pc.objectIndex = static_cast<uint32_t>(i);
      pc.fieldOffset = static_cast<uint32_t>(fdat.size());
      pc.fieldSize = static_cast<uint32_t>(c.blob.size());
      fdat.insert(fdat.end(), c.blob.begin(), c.blob.end());
      pcomps.push_back(pc);
      ++perType[c.type->name];
    }
  }

  std::vector<PackAnimRec> panims = cooked.anims;
  for (size_t i = 0; i < panims.size(); ++i) panims[i].nameStr = addStr(cooked.animNames[i]);

  CookedInput cookedInput;  // F3.06: INPT + INPX + INPS (see input_cook.h)
  {
    std::vector<std::string> inputErrors;
    cookInputActions(inputsJson, [&](const std::string& n) { return addStr(n); }, cookedInput, inputErrors);
    for (const std::string& e : inputErrors) cx.error("InputActions.json: " + e, "ProjectSettings/InputActions.json");
  }
  std::vector<PackInputAction>& pinputs = cookedInput.actions;
  if (!target.rumble)
    for (const std::string& f : scriptsUsingVibration(proj))
      cx.msg(CookMessage::Warning, "'" + f + "' uses Input::SetVibration but the '" + target.profile +
                                       "' ConsoleTarget has no rumble (set \"capabilities\": {\"rumble\": true}); the call does nothing", f);
  if (!R.errors.empty()) return R;

  PackConsoleTarget ct = {};
  {
    std::string p = target.profile;
    for (char& c : p) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    p.resize(4, ' ');
    ct.profile = makeFourCC(p[0], p[1], p[2], p[3]);
    ct.scalarType = target.scalar == "float" ? 0 : 1;
    ct.width = target.width; ct.height = target.height;
    ct.ramBytes = static_cast<u32>(target.ramBytes);
    ct.vramBytes = static_cast<u32>(target.vramBytes);
    ct.trianglesPerFrame = static_cast<u32>(target.trianglesPerFrame);
    ct.maxObjects = static_cast<u32>(target.maxObjects);
    ct.maxComponentsPerType = static_cast<u32>(target.maxComponentsPerType);
    ct.audioSampleRate = target.audioSampleRate;
    ct.audioVoices = target.audioVoices;
    ct.capabilities = target.rumble ? kTargetCapRumble : 0u;
  }

  // ---- F5: materials, skins and clips (MATL, SKIN, CLP3; joints and poses live in BLOB)
  std::vector<PackMaterial> pmats;
  std::vector<PackSkin> pskins;
  std::vector<PackClip3> pclips;
  for (const MatEntry& m : mats) {
    PackMaterial pm = {};
    pm.guid = m.guid;
    pm.texture = kInvalidIndex;
    if (!m.def.textureGuid.empty()) {
      Guid tg;
      Guid::parse(m.def.textureGuid.c_str(), tg);
      for (size_t t = 0; t < cooked.textures.size(); ++t)
        if (cooked.textures[t].guid == tg) pm.texture = static_cast<uint32_t>(t);
      if (pm.texture == kInvalidIndex) cx.error("material references texture GUID " + m.def.textureGuid + ", which is not a PNG asset of the project", m.rel, guidText(m.guid));
    }
    pm.colorABGR = colorAbgr(m.def.color);
    pm.flags = materialFlags(m.def);
    pmats.push_back(pm);
  }
  std::set<std::string> clipGuids;
  {
    auto blobAlign = [&]() { while (cooked.blob.size() % 16) cooked.blob.push_back(0); };
    for (size_t si = 0; si < skinsOut.size(); ++si) {
      const SkinEntry& se = skinsOut[si];
      blobAlign();
      PackSkin ps = {};
      ps.guid = se.guid;
      ps.jointCount = static_cast<u32>(se.joints.size());
      ps.jointsOffset = static_cast<u32>(cooked.blob.size());
      for (const GltfJoint& j : se.joints) {
        PackJoint pj = {};
        pj.parent = j.parent;
        pj.nameStr = addStr(j.name);
        memcpy(pj.t, j.t, sizeof pj.t);
        memcpy(pj.r, j.r, sizeof pj.r);
        memcpy(pj.s, j.s, sizeof pj.s);
        memcpy(pj.invBind, j.invBind, sizeof pj.invBind);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&pj);
        cooked.blob.insert(cooked.blob.end(), b, b + sizeof pj);
      }
      pskins.push_back(ps);
      for (size_t k = 0; k < se.clips.size(); ++k) {
        const GltfClip& gc = se.clips[k];
        blobAlign();
        PackClip3 pc = {};
        pc.guid = deriveGuid(se.guid, static_cast<uint32_t>(k));
        pc.nameStr = addStr(gc.name);
        pc.skin = static_cast<u32>(si);
        pc.frameCount = gc.frameCount;
        pc.fps = gc.fps;
        pc.loop = gc.loop ? 1 : 0;
        pc.dataOffset = static_cast<u32>(cooked.blob.size());
        const uint8_t* b = reinterpret_cast<const uint8_t*>(gc.poses.data());
        cooked.blob.insert(cooked.blob.end(), b, b + gc.poses.size() * sizeof(PackJointPose));
        pclips.push_back(pc);
        clipGuids.insert(guidText(pc.guid));
      }
    }
  }
  for (const ObjectDef& o : objs)
    for (const auto& c : o.comps) {
      if (strcmp(c.type->name, "Animation") != 0) continue;
      const std::string g = c.fields.value("clip", std::string());
      Guid parsed;
      if (!Guid::parse(g.c_str(), parsed) || parsed.isNull() || clipGuids.count(g)) continue;
      cx.error("'" + o.name + "' (guid " + guidText(o.guid) + ") Animation.clip references GUID " + g +
                   ", which is not a skeletal clip of a glTF used by a SkinnedMeshRenderer (use the glTF asset GUID for its first animation)",
               sceneAsset.rel, sceneAsset.guid);
    }
  if (!R.errors.empty()) return R;

  std::vector<PackSceneRec> pscenes;
  for (const SceneBlock& b : sceneBlocks) {
    PackSceneRec sr = {};
    Guid::parse(b.guid.c_str(), sr.guid);
    sr.nameStr = addStr(b.name);
    sr.firstObject = b.first;
    sr.objectCount = b.count;
    pscenes.push_back(sr);
  }
  // --- F6 a3 --- RSRC (Resources.Load names) and the Project.json "additiveScenes" flag; the call is in resources_cook.cpp
  std::vector<PackResource> resourceRecs;
  std::vector<PackPoolReserve> poolReserve;
  {
    std::vector<cooker::ResourceAsset> ra;
    for (const auto& kv : assets) ra.push_back({kv.second.rel, kv.second.guid});
    std::string rerr;
    if (!cooker::buildResourceTable(ra, addStr, resourceRecs, rerr)) cx.error(rerr, "ProjectSettings/Project.json");
    std::vector<std::string> blockGuids;
    for (const SceneBlock& b : sceneBlocks) blockGuids.push_back(b.guid);
    const std::string badScene = cooker::markAdditiveScenes(projectJson, blockGuids, pscenes);
    if (!badScene.empty()) cx.error("Project.json additiveScenes lists " + badScene + ", which is not a scene of the build", "ProjectSettings/Project.json", badScene);
    std::string perr;
    if (!cooker::buildPoolReserve(projectJson, poolReserve, perr)) cx.error(perr, "ProjectSettings/Project.json");
    if (!R.errors.empty()) return R;
  }
  // --- end F6 a3 ---
  secs.push_back({pack_section::STRS, strs, strCount});
  secs.push_back({pack_section::OBJS, bytesOf(pobjs.data(), pobjs.size() * sizeof(PackObject)), static_cast<u32>(pobjs.size())});
  secs.push_back({pack_section::XFRM, bytesOf(pxf.data(), pxf.size() * sizeof(PackTransform)), static_cast<u32>(pxf.size())});
  secs.push_back({pack_section::COMP, bytesOf(pcomps.data(), pcomps.size() * sizeof(PackComponent)), static_cast<u32>(pcomps.size())});
  secs.push_back({pack_section::FDAT, fdat, static_cast<u32>(pcomps.size())});
  secs.push_back({pack_section::MESH, bytesOf(meshRecs.data(), meshRecs.size() * sizeof(PackMeshRec)), static_cast<u32>(meshRecs.size())});
  secs.push_back({pack_section::VERT, vertBytes,
                  static_cast<u32>(vertBytes.size() / (compactMeshes ? sizeof(PackVertexCompact) : sizeof(PackVertex)))});
  if (compactMeshes) secs.push_back({pack_section::MSHX, bytesOf(&meshExt, sizeof meshExt), 1});
  secs.push_back({pack_section::INPT, bytesOf(pinputs.data(), pinputs.size() * sizeof(PackInputAction)), static_cast<u32>(pinputs.size())});
  secs.push_back({pack_section::INPX, bytesOf(cookedInput.ext.data(), cookedInput.ext.size() * sizeof(PackInputActionExt)), static_cast<u32>(cookedInput.ext.size())});
  secs.push_back({pack_section::INPS, bytesOf(&cookedInput.settings, sizeof cookedInput.settings), 1});
  secs.push_back({pack_section::CTGT, bytesOf(&ct, sizeof ct), 1});
  const PackConsoleTargetExt ctExt = makeConsoleTargetExt(target);  // F5: CTG2 (CTGT stays byte-identical)
  secs.push_back({pack_section::CTG2, bytesOf(&ctExt, sizeof ctExt), 1});
  PackSchedulerLimits sched = {target.maxCoroutines, target.maxInvokes, {0, 0}};
  secs.push_back({pack_section::CORO, bytesOf(&sched, sizeof sched), 1});
  secs.push_back({pack_section::SCNE, bytesOf(pscenes.data(), pscenes.size() * sizeof(PackSceneRec)), static_cast<u32>(pscenes.size())});
  secs.push_back({pack_section::BLOB, cooked.blob, static_cast<u32>(cooked.blob.size())});
  secs.push_back({pack_section::TEXR, bytesOf(cooked.textures.data(), cooked.textures.size() * sizeof(PackTexRec)), static_cast<u32>(cooked.textures.size())});
  secs.push_back({pack_section::AUDI, bytesOf(cooked.audio.data(), cooked.audio.size() * sizeof(PackAudioRec)), static_cast<u32>(cooked.audio.size())});
  secs.push_back({pack_section::TMAP, bytesOf(cooked.tilemaps.data(), cooked.tilemaps.size() * sizeof(PackTilemapRec)), static_cast<u32>(cooked.tilemaps.size())});
  secs.push_back({pack_section::ANIM, bytesOf(panims.data(), panims.size() * sizeof(PackAnimRec)), static_cast<u32>(panims.size())});
  secs.push_back({pack_section::FONT, bytesOf(cooked.fonts.data(), cooked.fonts.size() * sizeof(PackFontRec)), static_cast<u32>(cooked.fonts.size())});
  {
    // F2.05: layer collision matrix; no section when the project has no Physics2D.json (everything collides).
    std::string phText;
    if (readText(settings / "Physics2D.json", phText)) {
      PackPhysics2D ph = {};
      std::string phErr;
      if (!cookPhysics2D(phText, ph, phErr)) cx.error("ProjectSettings/Physics2D.json: " + phErr, "ProjectSettings/Physics2D.json");
      else secs.push_back({pack_section::PH2D, bytesOf(&ph, sizeof ph), 1});
    }
    // --- F6 a2 --- PHY3 (3D gravity and layer matrix) from ProjectSettings/Physics.json; no section without the file (physics_cook.cpp)
    std::string ph3Text;
    if (readText(settings / "Physics.json", ph3Text)) {
      std::string tagText, ph3Err;
      readText(settings / "TagManager.json", tagText);
      PackPhysics3D ph3 = {};
      if (!cooker::cookPhysics3D(ph3Text, tagText, ph3, ph3Err)) cx.error("ProjectSettings/Physics.json: " + ph3Err, "ProjectSettings/Physics.json");
      else secs.push_back({pack_section::PHY3, bytesOf(&ph3, sizeof ph3), 1});
    }
    // --- end F6 a2 ---
  }

  if (anyLit) {  // F5: optional sections, written only when the project has a lit or skinned mesh
    secs.push_back({pack_section::MSHL, bytesOf(meshLit.data(), meshLit.size() * sizeof(PackMeshLit)), static_cast<u32>(meshLit.size())});
    secs.push_back({pack_section::VTX3, vtx3, static_cast<u32>(vtx3.size())});
    if (!allBatches.empty())
      secs.push_back({pack_section::BTCH, bytesOf(allBatches.data(), allBatches.size() * sizeof(PackBoneBatch)), static_cast<u32>(allBatches.size())});
  }
  if (!pmats.empty()) secs.push_back({pack_section::MATL, bytesOf(pmats.data(), pmats.size() * sizeof(PackMaterial)), static_cast<u32>(pmats.size())});
  if (!pskins.empty()) secs.push_back({pack_section::SKIN, bytesOf(pskins.data(), pskins.size() * sizeof(PackSkin)), static_cast<u32>(pskins.size())});
  if (!pclips.empty()) secs.push_back({pack_section::CLP3, bytesOf(pclips.data(), pclips.size() * sizeof(PackClip3)), static_cast<u32>(pclips.size())});

  // --- F6 a3 ---
  if (!poolReserve.empty()) secs.push_back({pack_section::PCAP, bytesOf(poolReserve.data(), poolReserve.size() * sizeof(PackPoolReserve)), static_cast<u32>(poolReserve.size())});
  if (!resourceRecs.empty()) secs.push_back({pack_section::RSRC, bytesOf(resourceRecs.data(), resourceRecs.size() * sizeof(PackResource)), static_cast<u32>(resourceRecs.size())});
  // --- end F6 a3 ---

  const size_t packAlign = target.sectorAligned ? kPackSectorAlignment : kPackAlignment;
  auto align16 = [packAlign](size_t v) { return (v + packAlign - 1) & ~(packAlign - 1); };
  size_t cursor = align16(sizeof(PackHeader) + secs.size() * sizeof(PackSection));
  std::vector<PackSection> table;
  for (const Sec& s : secs) {
    PackSection ps = {s.fourcc, static_cast<u32>(cursor), static_cast<u32>(s.data.size()), s.count};
    table.push_back(ps);
    cursor = align16(cursor + s.data.size());
  }
  std::vector<uint8_t> pack(cursor, 0);
  PackHeader hdr = {};
  memcpy(hdr.magic, "RPAK", 4);
  hdr.version = kPackVersion;
  hdr.sectionCount = static_cast<u32>(secs.size());
  hdr.alignment = static_cast<u32>(packAlign);
  hdr.totalSize = static_cast<u32>(cursor);
  hdr.flags = target.sectorAligned ? kPackFlagSectorAligned : 0;
  memcpy(pack.data(), &hdr, sizeof hdr);
  memcpy(pack.data() + sizeof hdr, table.data(), table.size() * sizeof(PackSection));
  for (size_t i = 0; i < secs.size(); ++i)
    if (!secs[i].data.empty()) memcpy(pack.data() + table[i].offset, secs[i].data.data(), secs[i].data.size());

  // ---- budgets
  budget.packBytes = pack.size();
  budget.vertexBytes = vertBytes.size();
  budget.textureBytes = cooked.textureBytes;
  budget.objects = objs.size();
  R.usage.packBytes = pack.size();
  R.usage.vertexBytes = vertBytes.size();
  R.usage.triangles = budget.triangles;
  R.usage.objects = objs.size();
  for (const auto& kv : perType) {
    budget.componentsPerType.push_back({kv.first, kv.second});
    if (kv.second > R.usage.maxComponentsOfOneType) {
      R.usage.maxComponentsOfOneType = kv.second;
      R.usage.maxComponentsType = kv.first;
    }
  }
  {  // F5.17 (A6): AudioSources that sound from scene load (loop, or playOnAwake when the component has it) vs the voice budget
    for (const auto& o : objs)
      for (const auto& c : o.comps)
        if (c.type && c.type->name == std::string("AudioSource") && (c.fields.value("loop", false) || c.fields.value("playOnAwake", false)))
          ++budget.audioStartingSources;
    std::vector<CookMessage> audioWarn;
    warnAudioVoices(target, budget, audioWarn);
    for (const auto& w : audioWarn) cx.msg(CookMessage::Warning, w.text, w.asset, w.assetGuid);
  }
  std::vector<CookMessage> budgetErrors;
  bool fits = validateBudgets(target, budget, budgetErrors);
  if (!fits && target.vramBytes && budget.textureBytes > target.vramBytes) {  // F5: name the texture that costs the most
    size_t big = 0;
    uint64_t bigBytes = 0;
    for (size_t t = 0; t < cooked.textures.size(); ++t) {
      const uint64_t b = cooked.textures[t].pixelBytes + static_cast<uint64_t>(cooked.textures[t].paletteCount) * 4;
      if (b > bigBytes) { bigBytes = b; big = t; }
    }
    std::string rel = "<default font>", g = "";
    for (const auto& kv : assets) {
      Guid tg;
      Guid::parse(kv.first.c_str(), tg);
      if (!cooked.textures.empty() && cooked.textures[big].guid == tg) { rel = kv.second.rel; g = kv.first; }
    }
    CookMessage m;
    m.severity = CookMessage::Error;
    m.asset = rel;
    m.assetGuid = g;
    m.text = "texture budget: the largest texture is '" + rel + "' with " + std::to_string(bigBytes) + " cooked bytes (" +
             std::to_string(cooked.textures.size()) + " textures use " + std::to_string(budget.textureBytes) + " of " + std::to_string(target.vramBytes) +
             " VRAM bytes); lower its maxSize or use clut4 / clut8";
    budgetErrors.push_back(m);
  }
  if (!fits && opt.budgetsAreWarnings) {  // play in editor: run it anyway, the editor shows the overrun
    for (const auto& e : budgetErrors) cx.msg(CookMessage::Warning, e.text, e.asset, e.assetGuid);
    budgetErrors.clear();
    R.overBudget = true;
    fits = true;
  }

  fs::create_directories(outDir);
  {
    std::ostringstream rep;
    rep << "Budget report: " << sceneAsset.rel << " (guid " << sceneAsset.guid << ") for " << target.profile << "\n"
        << "  pack bytes       " << budget.packBytes << " / " << target.ramBytes << "\n"
        << "  triangles/frame  " << budget.triangles << " / " << target.trianglesPerFrame << "\n"
        << "  objects          " << budget.objects << " / " << target.maxObjects << "\n";
    for (const auto& c : budget.componentsPerType)
      rep << "  " << c.type << " components  " << c.count << " / " << target.maxComponentsPerType << "\n";
    rep << (fits ? "  result: OK\n" : "  result: OVER BUDGET\n");
    for (const auto& e : budgetErrors) rep << "  error: " << e.text << "\n";
    std::ofstream(R.reportPath, std::ios::binary) << rep.str();
  }
  for (const auto& e : budgetErrors) cx.msg(CookMessage::Error, e.text, e.asset, e.assetGuid);
  if (!fits) {
    fs::remove(R.packPath);
    fs::remove(manifestPath);
    return R;
  }

  {
    fs::path tmp = R.packPath;
    tmp += ".tmp";
    std::ofstream out(tmp, std::ios::binary);
    out.write(reinterpret_cast<const char*>(pack.data()), static_cast<std::streamsize>(pack.size()));
    out.close();
    fs::rename(tmp, R.packPath);
    std::ofstream(manifestPath) << json(hashes).dump(1);
  }
  cx.info("cook: wrote " + R.packPath.string() + " (" + std::to_string(pack.size()) + " bytes, " +
          std::to_string(objs.size()) + " objects, " + std::to_string(budget.triangles) + " triangles, " +
          std::to_string(cooked.textures.size()) + " textures, " + std::to_string(cooked.audio.size()) + " audio clips)");
  R.ok = true;
  return R;
}


// F3.02: pieces of the cook cache key shared with the editor's single-asset reimport.
std::string reflectionLayoutHash() {
  std::string layout;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i) {
    const TypeInfo* ti = reg.byId(i);
    layout += std::string(ti->name) + ":";
    for (u32 f = 0; f < ti->fieldCount; ++f) {
      const FieldDesc& fd = ti->fields[f];
      layout += std::string(fd.name) + "," + std::to_string(static_cast<int>(fd.type));
      // F6: array shape and the appended-fields boundary change the cooked bytes, so they are part of the layout.
      if (fd.type == FieldType::Array) layout += "[" + std::to_string(static_cast<int>(fd.elemType)) + "x" + std::to_string(fd.arrayCap) + "]";
      layout += ";";
    }
    if (ti->trimFloor != kNoTrim) layout += "trim" + std::to_string(ti->trimFloor) + ";";
  }
  return hex64(fnv64(layout));
}

std::string assetCacheSalt(const fs::path& projectRoot, const std::string& target) {
  std::string txt;
  readText(projectRoot / "ProjectSettings" / "ConsoleTarget.json", txt);
  return std::string(kCookerVersion) + "|" + target + "|" + hex64(fnv64(txt)) + "|" + reflectionLayoutHash();
}

}  // namespace rensga
