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

// Scene indices of the sample (build order in ProjectSettings/Project.json).
constexpr u32 kTitle = 0, kRoom = 1, kRoom2 = 2, kRoom3 = 3, kGameOver = 4, kResults = 5, kTitleBot = 6, kInputTest = 7, kRoomBot = 8;

// Cooks a project for a target and returns the pack path (empty on failure; cook errors go to stderr).
inline fs::path cookProjectDir(const fs::path& project, const char* target) {
  CookOptions o;
  o.project = project.string();
  o.target = target;
  o.force = true;
  CookResult r = cookProject(o, [](const CookMessage& m) {
    if (m.severity == CookMessage::Error) fprintf(stderr, "    [cook error] %s\n", m.text.c_str());
  });
  return r.ok ? fs::path(r.packPath) : fs::path();
}

inline fs::path cookSample() {
  static fs::path packPath;
  if (!packPath.empty()) return packPath;
  packPath = cookProjectDir(RENSGA_SAMPLE_DIR, "ps2");
  return packPath;
}

// Collects "rensga: ..." log lines from Debug::Log into a fixed buffer (no heap while a frame runs).
struct LogCapture : ILogSink {
  char buf[1 << 16] = {};
  u32 used = 0;
  void write(const LogRecord& r) override {
    for (const char* p = r.message; *p && used + 2 < sizeof(buf); ++p) buf[used++] = *p;
    if (used + 2 < sizeof(buf)) buf[used++] = '\n';
    buf[used] = 0;
  }
  bool has(const char* needle) const { return strstr(buf, needle) != nullptr; }
  int count(const char* needle) const {
    int n = 0;
    for (const char* p = buf; (p = strstr(p, needle)) != nullptr; ++p) ++n;
    return n;
  }
  const char* find(const char* needle) const { return strstr(buf, needle); }
};

// Records the 2D draw calls renderGameplay2D makes (UI layout checks at any view size).
struct RecordingRenderer : IRenderer {
  u32 w = 640, h = 448;
  struct Rec { Rect r; Color32 color; u32 key; bool isText; i32 tx, ty, tw, th; char text[64]; };
  std::vector<Rec> rects;
  bool init(u32, u32) override { return true; }
  void setMeshes(const MeshTable&) override {}
  void beginFrame(Color32) override { rects.clear(); }
  void setCamera(const Matrix4&, const Matrix4&) override {}
  void drawMesh(u32, const Matrix4&, u32) override {}
  void drawOverlayQuad(i32, i32, i32, i32, Color32) override {}
  void endFrame() override {}
  u32 width() const override { return w; }
  u32 height() const override { return h; }
  void drawRect(const Rect& dst, Color32 c, u32 key) override { rects.push_back({dst, c, key, false, 0, 0, 0, 0, {}}); }
  void drawText(u32, const PackFontRec& f, i32 x, i32 y, const char* t, Color32 c, u32 key) override {
    Rec r{Rect(), c, key, true, x, y, UI::textWidth(f, t), UI::textHeight(f, t), {}};
    snprintf(r.text, sizeof r.text, "%s", t);
    rects.push_back(r);
  }
};

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
    GameState::ResetRun();
    GameState::bestTicks = 0;
    GameState::track = "music.wav";   // the runtime starts it at boot
    loop.init(world);
    return true;
  }

  // Pretends every texture uploaded (handle = index + 1), so renderGameplay2D draws everything into a RecordingRenderer.
  void fakeUploads() {
    for (u32 i = 0; i < handles.size(); ++i) handles[i] = i + 1;
    GfxAssets::bind(&view, &loaded.assets, handles.data(), loaded.assets.textureCount);
  }

  void frame() {
    if (!loop.frame(1.0f / 60.0f, pad)) { ++rtest::failures(); fprintf(stderr, "  heap allocation during frame %u\n", frames); }
    ++frames;
  }
  void run(u32 n) { for (u32 i = 0; i < n; ++i) frame(); }
};

}  // namespace sample
