#include "rensga/editor/editor_world.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

#include "rensga/Physics.h"
#include "rensga/core/application.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/input.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/time.h"

namespace fs = std::filesystem;

namespace rensga {
namespace editor {

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
  (void)target;
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
