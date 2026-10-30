// MVP-20/21: shared cooker core. Scene JSON -> reflected component blobs -> pack v1.
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/core/components.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/reflect_json.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace {

constexpr const char* kCookerVersion = "rensga-cooker-1";

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
  struct Comp { const TypeInfo* type; std::vector<uint8_t> blob; json fields; };
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

}  // namespace

CookResult cookProject(const CookOptions& opt, const CookSink& sink) {
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
  if (target.profile != opt.target) {
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
        std::string g = json::parse(txt).at("guid").get<std::string>();
        Guid parsed;
        if (!Guid::parse(g.c_str(), parsed)) throw std::runtime_error("bad guid '" + g + "'");
        Asset a;
        a.path = p;
        a.path.replace_extension("");
        a.rel = relPath(proj, a.path);
        a.guid = g;
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
  const fs::path outDir = proj / "Library" / "Cooked" / opt.target;
  R.packPath = outDir / "game.rpak";
  R.reportPath = outDir / "budget-report.txt";
  std::map<std::string, std::string> hashes;
  hashes["<cooker>"] = kCookerVersion;
  {
    std::string layout;
    const TypeRegistry& reg = TypeRegistry::instance();
    for (u32 i = 0; i < reg.count(); ++i) {
      const TypeInfo* ti = reg.byId(i);
      layout += std::string(ti->name) + ":";
      for (u32 f = 0; f < ti->fieldCount; ++f)
        layout += std::string(ti->fields[f].name) + "," + std::to_string(static_cast<int>(ti->fields[f].type)) + ";";
    }
    hashes["<reflection>"] = hex64(fnv64(layout));
  }
  for (const fs::path& p : hashed) {
    std::string txt;
    readText(p, txt);
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
  auto sit = assets.find(sceneGuids[0]);
  if (sit == assets.end()) {
    cx.error("Project.json references scene GUID " + sceneGuids[0] + " but no asset has that GUID", "ProjectSettings/Project.json", sceneGuids[0]);
    return R;
  }
  if (sceneGuids.size() > 1) cx.msg(CookMessage::Warning, "only the first scene is cooked into game.rpak in the MVP");
  const Asset& sceneAsset = sit->second;
  json scene;
  {
    std::string txt;
    readText(sceneAsset.path, txt);
    try {
      scene = json::parse(txt);
      if (scene.value("format", 0) != 1) throw std::runtime_error("unsupported scene format version");
    } catch (const std::exception& e) {
      cx.error(std::string("scene parse error: ") + e.what(), sceneAsset.rel, sceneAsset.guid);
      return R;
    }
  }

  // ---- scene objects through reflection
  std::vector<ObjectDef> objs;
  std::map<std::string, int> objIndex;
  const std::set<std::string> allowed(target.allowedComponents.begin(), target.allowedComponents.end());
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
        if (!allowed.count(tn)) {
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
        c.fields = fieldsToJson(*ti, inst.mem);
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
        if (c.type->fields[f].type != FieldType::Guid) continue;
        std::string g = c.fields.value(c.type->fields[f].name, std::string());
        Guid parsed;
        if (!Guid::parse(g.c_str(), parsed) || parsed.isNull()) continue;
        if (!objIndex.count(g) && !assets.count(g))
          cx.error("'" + o.name + "' (guid " + guidText(o.guid) + ") " + c.type->name + "." + c.type->fields[f].name +
                       " references missing GUID " + g,
                   sceneAsset.rel, sceneAsset.guid);
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
  for (size_t i = 0; i < objs.size(); ++i) {
    for (const auto& c : objs[i].comps) {
      if (c.type != mrType) continue;
      std::string mg = c.fields.value("mesh", std::string());
      auto pit = primCache.find(mg);
      if (pit == primCache.end()) {
        auto ait = assets.find(mg);
        if (ait == assets.end() || ait->second.rel.find(".prim.json") == std::string::npos) {
          cx.error("MeshRenderer on '" + objs[i].name + "' (guid " + guidText(objs[i].guid) + ") references mesh GUID " + mg +
                       ", which is not a primitive asset",
                   sceneAsset.rel, sceneAsset.guid);
          continue;
        }
        std::string txt, perr;
        readText(ait->second.path, txt);
        PrimitiveParams pp;
        if (!parsePrimitive(txt, pp, perr)) {
          cx.error("primitive parse error: " + perr, ait->second.rel, mg);
          continue;
        }
        pit = primCache.emplace(mg, std::make_pair(pp, generatePrimitive(pp, target))).first;
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
      if (!plugin->cookMesh(m, light, base, vertBytes, count, cerr)) {
        cx.error("cookMesh failed for '" + objs[i].name + "': " + cerr, sceneAsset.rel, sceneAsset.guid);
        continue;
      }
      PackMeshRec rec;
      rec.vertexOffset = static_cast<uint32_t>(firstByte / sizeof(PackVertex));
      rec.vertexCount = count;
      rec.primitiveType = 0;
      rec.objectIndex = static_cast<uint32_t>(i);
      meshRecs.push_back(rec);
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

  std::vector<PackInputAction> pinputs;
  {
    static const std::map<std::string, PackInputSource> srcs = {
        {"nub", PackInputSource::Nub}, {"dpad", PackInputSource::DPad}, {"south", PackInputSource::South},
        {"east", PackInputSource::East}, {"west", PackInputSource::West}, {"north", PackInputSource::North},
        {"l", PackInputSource::L}, {"r", PackInputSource::R}, {"start", PackInputSource::Start},
        {"select", PackInputSource::Select}, {"up", PackInputSource::Up}, {"down", PackInputSource::Down},
        {"left", PackInputSource::Left}, {"right", PackInputSource::Right}};
    try {
      for (const json& a : inputsJson.at("actions")) {
        std::string name = a.at("name").get<std::string>(), kind = a.at("kind").get<std::string>(),
                    src = a.at("source").get<std::string>();
        auto it = srcs.find(src);
        if (it == srcs.end() || (kind != "axis2" && kind != "button")) {
          cx.error("InputActions.json: action '" + name + "' has unknown kind/source '" + kind + "/" + src + "'", "ProjectSettings/InputActions.json");
          continue;
        }
        PackInputAction pa = {};
        pa.nameStr = addStr(name);
        pa.kind = static_cast<u32>(kind == "axis2" ? PackInputKind::Axis2 : PackInputKind::Button);
        pa.source = static_cast<u32>(it->second);
        pinputs.push_back(pa);
      }
    } catch (const std::exception& e) {
      cx.error(std::string("InputActions.json: ") + e.what(), "ProjectSettings/InputActions.json");
    }
  }
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
  }

  secs.push_back({pack_section::STRS, strs, strCount});
  secs.push_back({pack_section::OBJS, bytesOf(pobjs.data(), pobjs.size() * sizeof(PackObject)), static_cast<u32>(pobjs.size())});
  secs.push_back({pack_section::XFRM, bytesOf(pxf.data(), pxf.size() * sizeof(PackTransform)), static_cast<u32>(pxf.size())});
  secs.push_back({pack_section::COMP, bytesOf(pcomps.data(), pcomps.size() * sizeof(PackComponent)), static_cast<u32>(pcomps.size())});
  secs.push_back({pack_section::FDAT, fdat, static_cast<u32>(pcomps.size())});
  secs.push_back({pack_section::MESH, bytesOf(meshRecs.data(), meshRecs.size() * sizeof(PackMeshRec)), static_cast<u32>(meshRecs.size())});
  secs.push_back({pack_section::VERT, vertBytes, static_cast<u32>(vertBytes.size() / sizeof(PackVertex))});
  secs.push_back({pack_section::INPT, bytesOf(pinputs.data(), pinputs.size() * sizeof(PackInputAction)), static_cast<u32>(pinputs.size())});
  secs.push_back({pack_section::CTGT, bytesOf(&ct, sizeof ct), 1});

  auto align16 = [](size_t v) { return (v + 15) & ~size_t(15); };
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
  hdr.alignment = kPackAlignment;
  hdr.totalSize = static_cast<u32>(cursor);
  hdr.flags = 0;
  memcpy(pack.data(), &hdr, sizeof hdr);
  memcpy(pack.data() + sizeof hdr, table.data(), table.size() * sizeof(PackSection));
  for (size_t i = 0; i < secs.size(); ++i)
    if (!secs[i].data.empty()) memcpy(pack.data() + table[i].offset, secs[i].data.data(), secs[i].data.size());

  // ---- budgets
  budget.packBytes = pack.size();
  budget.vertexBytes = vertBytes.size();
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
  std::vector<CookMessage> budgetErrors;
  bool fits = validateBudgets(target, budget, budgetErrors);

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
          std::to_string(objs.size()) + " objects, " + std::to_string(budget.triangles) + " triangles)");
  R.ok = true;
  return R;
}

}  // namespace rensga
