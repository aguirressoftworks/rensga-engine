// Shared by the F2.05 tests: cooks samples/platformer2d for the PS2 profile and runs the real GameLoop headless.
#pragma once
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <filesystem>
#include <vector>

#include "GameState.h"
#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga_test.h"

namespace sample {

using namespace rensga;
namespace fs = std::filesystem;

inline fs::path cookSample() {
  static fs::path packPath;
  if (!packPath.empty()) return packPath;
  CookOptions o;
  o.project = RENSGA_SAMPLE_DIR;
  o.target = "ps2";
  o.force = true;
  CookResult r = cookProject(o, [](const CookMessage& m) {
    if (m.severity == CookMessage::Error) fprintf(stderr, "    [cook error] %s\n", m.text.c_str());
  });
  if (r.ok) packPath = r.packPath;
  return packPath;
}

inline fs::path tempDir(const char* tag) {
  fs::path p = fs::temp_directory_path() / (std::string("rensga_") + tag + "_" + std::to_string(static_cast<long>(getpid())));
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

struct Game {
  Arena arena;
  World world;
  PackView view;
  LoadedPack loaded;
  GameLoop loop;
  StdioFileSystem* fs = nullptr;
  FileSystemSaveStore* store = nullptr;
  std::string saveRoot;
  VirtualGamepad pad;
  u32 frames = 0;
  std::vector<u32> handles;

  ~Game() {
    world.shutdown();
    GfxAssets::unbind();
    SceneManager::unbind();
    Save::use(nullptr);
    delete store;
    delete fs;
  }

  bool boot(const fs::path& pack, const fs::path& saveDir, u32 scene = 0) {
    saveRoot = saveDir.string() + "/";
    fs = new StdioFileSystem("", nullptr, saveRoot.c_str());
    store = new FileSystemSaveStore(*fs);
    if (!arena.create(24u * 1024 * 1024)) return false;
    Span<const u8> bytes = fs->readFile(pack.string().c_str(), arena);
    if (!bytes || !view.open(bytes.data, bytes.size, pack.string().c_str())) return false;
    WorldConfig cfg;
    if (!world.init(cfg, arena)) return false;
    if (!world.loadPack(view, &loaded, scene)) return false;
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
    handles.assign(loaded.assets.textureCount ? loaded.assets.textureCount : 1, 0);
    GfxAssets::bind(&view, &loaded.assets, handles.data(), loaded.assets.textureCount);
    SceneManager::bind(&view, scene);
    Save::use(store);
    Gameplay2D::resetTriggerMemory();
    UI::resetFocus();
    Input::update(VirtualGamepad());
    Input::update(VirtualGamepad());
    GameState::StartRun();
    GameState::best = 0;
    loop.init(world);
    return true;
  }

  void frame() {
    if (!loop.frame(1.0f / 60.0f, pad)) { ++rtest::failures(); fprintf(stderr, "  heap allocation during frame %u\n", frames); }
    ++frames;
  }
  void run(u32 n) { for (u32 i = 0; i < n; ++i) frame(); }
};

}  // namespace sample
