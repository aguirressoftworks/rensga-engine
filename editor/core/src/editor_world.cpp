#include "rensga/editor/editor_world.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <iterator>
#include <memory>

#include "rensga/Physics.h"
#include "rensga/core/application.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/input.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/time.h"
#include "rensga/cooker/plugin.h"

namespace fs = std::filesystem;

namespace rensga {
namespace editor {

namespace {

std::string guidOfText(const std::string& t) {  // "(guid xxxxxxxx-...)" -> the 36 chars
  const size_t at = t.find("(guid ");
  if (at == std::string::npos || t.size() < at + 6 + 36) return "";
  return t.substr(at + 6, 36);
}

}  // namespace

std::vector<ValidationItem> classifyCookMessages(const std::vector<CookMessage>& msgs, const nlohmann::ordered_json& sceneDoc) {
  std::vector<ValidationItem> out;
  for (const CookMessage& m : msgs) {
    if (m.severity == CookMessage::Info) continue;
    ValidationItem v;
    v.severity = m.severity == CookMessage::Error ? ValidationItem::Error : ValidationItem::Warning;
    v.text = m.text;
    v.asset = m.asset;
    v.assetGuid = m.assetGuid;
    const std::string& t = m.text;
    auto has = [&](const char* s) { return t.find(s) != std::string::npos; };
    if (has("lights")) v.category = "lights";
    else if (has("joint") || has("bone") || has("skin")) v.category = "skinning";
    else if (has("texture") || has("downscaled") || has("VRAM")) v.category = "texture";
    else if (has("audio") || has("voice")) v.category = "audio";
    else if (has("budget") || has("over the")) v.category = "budget";
    else v.category = "other";
    // object: by the guid in the text, else by the first quoted name
    std::string g = guidOfText(t);
    if (!g.empty() && sceneDoc.contains("objects") && sceneDoc["objects"].is_array())
      for (const auto& o : sceneDoc["objects"])
        if (o.value("guid", std::string()) == g) { v.objectGuid = g; v.objectName = o.value("name", std::string()); break; }
    if (v.objectGuid.empty() && has("object '")) {
      const size_t a = t.find("object '") + 8, b = t.find('\'', a);
      if (b != std::string::npos && sceneDoc.contains("objects") && sceneDoc["objects"].is_array()) {
        const std::string name = t.substr(a, b - a);
        for (const auto& o : sceneDoc["objects"])
          if (o.value("name", std::string()) == name) { v.objectGuid = o.value("guid", std::string()); v.objectName = name; break; }
      }
    }
    // the list shows the object next to the sentence, so the sentence drops its own "object 'X' (guid ...)" lead-in and the
    // config-key note
    if (!v.objectName.empty() && v.text.rfind("object '", 0) == 0) {
      const size_t close = v.text.find(") ");
      if (close != std::string::npos) v.text = v.text.substr(close + 2);
    }
    const size_t cfg = v.text.find(" (ConsoleTarget ");
    if (cfg != std::string::npos) {
      const size_t end = v.text.find(')', cfg);
      if (end != std::string::npos) v.text.erase(cfg, end - cfg + 1);
    }
    out.push_back(std::move(v));
  }
  return out;
}

bool cookConsolePreview(const fs::path& projectRoot, const std::string& target, const fs::path& sceneFile,
                        const nlohmann::ordered_json& sceneDoc, ConsolePreview& out) {
  out = ConsolePreview();
  out.target = target;
  const auto t0 = std::chrono::steady_clock::now();
  ICookerFormatPlugin* plugin = CookerPluginRegistry::instance().find(target);
  if (!plugin) { out.error = "no cooker plugin for target '" + target + "'"; return false; }
  std::error_code ec;
  const fs::path root = fs::absolute(projectRoot);
  const std::string sceneRel = fs::relative(fs::absolute(sceneFile), root, ec).generic_string();
  CookOptions opt;
  opt.project = root;
  opt.target = target;
  opt.outDir = root / "Library" / "Cooked" / "editor" / (target + "-preview");
  opt.sceneOverrides[sceneRel] = sceneDoc.dump();
  opt.budgetsAreWarnings = true;
  std::vector<CookMessage> msgs;
  std::string firstError;
  // The cooker is silent when no input changed since the last cook into this folder, so the findings of that cook are kept next
  // to its pack (preview-cook.json) and replayed. A missing file forces a real cook.
  const fs::path memo = opt.outDir / "preview-cook.json";
  auto cook = [&](bool force) {
    opt.force = force;
    msgs.clear();
    firstError.clear();
    return cookProject(opt, [&](const CookMessage& m) {
      msgs.push_back(m);
      if (m.severity == CookMessage::Error && firstError.empty()) firstError = m.text + (m.asset.empty() ? std::string() : " (" + m.asset + ")");
    });
  };
  CookResult cr = cook(false);
  if (cr.upToDate) {
    nlohmann::json j;
    bool have = false;
    try {
      std::ifstream in(memo);
      j = nlohmann::json::parse(in, nullptr, false);
      have = j.is_object() && j.contains("messages");
    } catch (...) {
    }
    if (!have) {
      cr = cook(true);
    } else {
      msgs.clear();
      for (const nlohmann::json& m : j["messages"]) {
        CookMessage cm;
        cm.severity = static_cast<CookMessage::Severity>(m.value("severity", 1));
        cm.text = m.value("text", "");
        cm.asset = m.value("asset", "");
        cm.assetGuid = m.value("guid", "");
        msgs.push_back(std::move(cm));
      }
      cr.usage.packBytes = j.value("packBytes", 0ull);
      cr.usage.triangles = j.value("triangles", 0ull);
      cr.usage.objects = j.value("objects", 0ull);
    }
  }
  if (cr.ok && !cr.upToDate) {
    nlohmann::json j;
    j["messages"] = nlohmann::json::array();
    for (const CookMessage& m : msgs)
      if (m.severity != CookMessage::Info) j["messages"].push_back({{"severity", static_cast<int>(m.severity)}, {"text", m.text}, {"asset", m.asset}, {"guid", m.assetGuid}});
    j["packBytes"] = cr.usage.packBytes;
    j["triangles"] = cr.usage.triangles;
    j["objects"] = cr.usage.objects;
    std::error_code wec;
    fs::create_directories(opt.outDir, wec);
    std::ofstream(memo) << j.dump();
  }
  // The cook covers every scene of the build settings; the list is about the open one: a finding that names an object
  // (by guid) of another scene is dropped.
  out.items = classifyCookMessages(msgs, sceneDoc);
  out.items.erase(std::remove_if(out.items.begin(), out.items.end(),
                                 [](const ValidationItem& v) { return v.objectGuid.empty() && v.text.find("(guid ") != std::string::npos; }),
                  out.items.end());
  if (!cr.ok) { out.error = firstError.empty() ? "the cook failed" : firstError; return false; }
  out.packBytes = cr.usage.packBytes;
  out.triangles = cr.usage.triangles;
  out.objects = cr.usage.objects;
  std::ifstream in(cr.packPath, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (raw.empty()) { out.error = "cannot read " + cr.packPath.string(); return false; }
  const size_t cap = (raw.size() + 63) & ~size_t(15);
  std::unique_ptr<uint8_t, void (*)(void*)> mem(static_cast<uint8_t*>(std::aligned_alloc(16, cap)), std::free);
  if (!mem) { out.error = "out of memory"; return false; }
  std::copy(raw.begin(), raw.end(), reinterpret_cast<char*>(mem.get()));
  PackView view;
  if (!view.open(mem.get(), raw.size(), "console preview pack")) { out.error = "the console pack is not valid"; return false; }
  const PackAssets as = view.assets();
  for (u32 i = 0; i < as.textureCount; ++i) {
    const PackTexRec& t = as.textures[i];
    ConsolePreview::Texture ct;
    char g[40];
    t.guid.format(g);
    ct.guid = g;
    ct.format = t.format;
    ct.srcWidth = t.width;
    ct.srcHeight = t.height;
    ct.storedWidth = t.storedW();
    ct.storedHeight = t.storedH();
    ct.bytes = t.pixelBytes + t.paletteCount * 4u;
    std::string derr;
    if (!plugin->decodeCookedTexture(t, as.blob, ct.rgba, derr)) {
      ct.rgba = ImageRgba();
      out.items.push_back({ValidationItem::Warning, "texture", "cannot decode texture " + ct.guid + ": " + derr, "", ct.guid, "", ""});
    }
    out.textureBytes += ct.bytes;
    out.textures.push_back(std::move(ct));
  }
  out.ok = true;
  out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

void EditorWorld::setMaxLightsPerObject(uint32_t n) {
  if (!valid_) return;
  if (const PackConsoleTargetExt* ext = view_.consoleTargetExt())
    if (n) const_cast<PackConsoleTargetExt*>(ext)->maxLightsPerObject = n;
}

bool EditorWorld::build(const fs::path& projectRoot, const std::string& target, const fs::path& sceneFile,
                        const nlohmann::ordered_json& sceneDoc, std::string& err) {
  destroy();
  messages_.clear();
  const auto t0 = std::chrono::steady_clock::now();
  std::error_code ec;
  const fs::path root = fs::absolute(projectRoot);
  const std::string sceneRel = fs::relative(fs::absolute(sceneFile), root, ec).generic_string();

  // Play in editor draws with the desktop renderer, so it cooks the desktop form of the project: the host
  // platform target keeps the project's profile and budgets but skips console-only encodings (swizzled
  // textures, compact meshes) that only the console renderer reads. `target` names the profile for logs.
#ifdef _WIN32
  const std::string hostTarget = "windows";
#else
  const std::string hostTarget = "linux";
#endif
  CookOptions opt;
  opt.project = root;
  opt.target = hostTarget;
  opt.outDir = root / "Library" / "Cooked" / "editor" / hostTarget;
  opt.sceneOverrides[sceneRel] = sceneDoc.dump();
  opt.budgetsAreWarnings = true;
  std::string firstError;
  CookResult cr = cookProject(opt, [&](const CookMessage& m) {
    messages_.push_back(m);
    if (m.severity == CookMessage::Error && firstError.empty())
      firstError = m.text + (m.asset.empty() ? std::string() : " (" + m.asset + ")");
  });
  if (!cr.ok) {
    err = firstError.empty() ? "the cook failed" : firstError;
    return false;
  }
  cooked_ = cr.assetsCooked;
  cached_ = cr.assetsCached;
  overBudget_ = cr.overBudget;
  if (cr.upToDate) {  // the budget report of the cook that wrote this pack says whether it was over
    std::ifstream rep(cr.reportPath);
    std::string line;
    while (std::getline(rep, line)) {
      if (line.find("result: OVER BUDGET") != std::string::npos) overBudget_ = true;
      if (line.find("  error: ") == 0) {
        CookMessage m;
        m.severity = CookMessage::Warning;
        m.text = line.substr(9);
        messages_.push_back(m);
      }
    }
  }
  try {
    target_ = loadConsoleTarget(root / "ProjectSettings" / "ConsoleTarget.json");
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }

  // The pack goes into 16-byte aligned memory owned by this object.
  std::ifstream in(cr.packPath, std::ios::binary);
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (raw.empty() || !packArena_.create(raw.size() + 64)) {
    err = "cannot read the cooked pack " + cr.packPath.string();
    return false;
  }
  u8* bytes = static_cast<u8*>(packArena_.allocate(raw.size(), 16));
  std::copy(raw.begin(), raw.end(), reinterpret_cast<char*>(bytes));
  if (!view_.open(bytes, raw.size(), "editor play pack")) {
    err = "the cooked pack is not valid";
    packArena_.release();
    return false;
  }

  SceneManager::bind(&view_, 0);
  const std::string stem = sceneFile.stem().string();
  const i32 si = SceneManager::sceneIndexByName(stem.c_str());
  if (si < 0) {
    SceneManager::unbind();
    packArena_.release();
    err = "scene '" + stem + "' is not in the project's build settings (ProjectSettings/Project.json scenes)";
    return false;
  }
  sceneIndex_ = static_cast<uint32_t>(si);
  sceneName_ = stem;

  // World memory from the ConsoleTarget budgets, with room to spare (the editor is not a console).
  WorldConfig cfg;
  cfg.maxObjects = std::max<u32>(256, static_cast<u32>(target_.maxObjects));
  cfg.maxComponentsPerType = std::max<u32>(256, static_cast<u32>(target_.maxComponentsPerType));
  cfg.maxCoroutines = target_.maxCoroutines;
  cfg.maxInvokes = target_.maxInvokes;
  size_t arenaBytes = MemoryConfig::fromRamBudget(static_cast<size_t>(target_.ramBytes)).arenaBytes;
  arenaBytes = std::min<size_t>(std::max<size_t>(arenaBytes, 24u << 20), 256u << 20);
  if (!arena_.create(arenaBytes)) {
    SceneManager::unbind();
    packArena_.release();
    err = "cannot allocate the world arena";
    return false;
  }
  guardWas_ = FrameHeapGuard::enabled();
  FrameHeapGuard::setEnabled(false);  // the editor process allocates freely (UI, logging)
  Application::setPlaying(true);
  TimeInternal::reset(1.0f / static_cast<Scalar>(target_.refreshHz ? target_.refreshHz : 60));
  if (!world_.init(cfg, arena_) || !world_.loadPack(view_, &loaded_, sceneIndex_)) {
    err = "cannot load the cooked pack into a World";
    world_.shutdown();
    SceneManager::unbind();
    arena_.release();
    packArena_.release();
    FrameHeapGuard::setEnabled(guardWas_);
    return false;
  }
  Input::loadActions(view_, loaded_.inputs, loaded_.inputCount);
  SceneManager::bind(&view_, sceneIndex_);
  loop_.init(world_, LoopConfig::forRefreshHz(target_.refreshHz));
  loop_.setFixedStepHook(physics::fixedStep, nullptr);
  const PackAssets& as = loaded_.assets;
  textureBytes_ = 0;
  for (u32 i = 0; i < as.textureCount; ++i) textureBytes_ += as.textures[i].pixelBytes + as.textures[i].paletteCount * 4u;
  static uint64_t s_nextId = 1;
  id_ = s_nextId++;
  valid_ = true;
  // The console form of the same scene: textures as the console shows them, budget usage, validation findings.
  preview_ = ConsolePreview();
  if (!target.empty() && target != hostTarget && CookerPluginRegistry::instance().find(target)) {
    std::string perr;
    if (!cookConsolePreview(root, target, sceneFile, sceneDoc, preview_)) {
      preview_.ok = false;
      messages_.push_back({CookMessage::Warning, "console preview: " + preview_.error, "", ""});
    }
  }
  cookMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

void EditorWorld::destroy() {
  if (!valid_ && !arena_.capacity() && !packArena_.capacity()) return;
  if (valid_) {
    world_.shutdown();
  }
  Gameplay2D::resetTriggerMemory();
  physics::resetTriggerMemory();
  UI::resetFocus();
  Input::clearActions();
  GfxAssets::unbind();
  SceneManager::unbind();
  loaded_ = LoadedPack();
  view_ = PackView();
  arena_.release();
  packArena_.release();
  if (valid_) FrameHeapGuard::setEnabled(guardWas_);
  valid_ = false;
}

}  // namespace editor
}  // namespace rensga
