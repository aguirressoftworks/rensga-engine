// MVP-10: the four backend interfaces. engine/core sees backends only through these.
#pragma once
#include "rensga/core/math.h"
#include "rensga/core/memory.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/types.h"

namespace rensga {

struct VirtualGamepad {
  Scalar leftX = 0, leftY = 0;  // nub, -1..1 (+Y is up/forward)
  struct Buttons {
    bool south = false, east = false, west = false, north = false;
    bool l = false, r = false, start = false, select = false;
    bool up = false, down = false, left = false, right = false;
  };
  Buttons held;
  Buttons pressed;  // went down this frame (set by the platform or by Input::update)
};

class IPlatform {
 public:
  virtual ~IPlatform() {}
  virtual bool init() = 0;
  virtual void shutdown() = 0;
  // Fills the gamepad's axes and held buttons. `pressed` is derived by the loop/Input.
  virtual void pollInput(VirtualGamepad& pad) = 0;
  virtual double timeSeconds() = 0;
  virtual bool shouldQuit() = 0;
};

// Meshes are cooked vertex bytes (pack VERT section) described by MeshRec entries.
struct MeshTable {
  const PackMeshRec* meshes = nullptr;
  u32 meshCount = 0;
  const PackVertex* vertices = nullptr;  // 16-byte aligned, lives in the pack
  u32 vertexCount = 0;
};

class IRenderer {
 public:
  virtual ~IRenderer() {}
  virtual bool init(u32 width, u32 height) = 0;
  // Added in MVP (listed in interfaceChanges): hand the cooked meshes to the renderer.
  virtual void setMeshes(const MeshTable& table) = 0;
  virtual void beginFrame(Color32 clear) = 0;
  virtual void setCamera(const Matrix4& view, const Matrix4& proj) = 0;
  // sortKey orders draws (PSX has no z-buffer, so sorting is part of the contract).
  virtual void drawMesh(u32 meshIndex, const Matrix4& world, u32 sortKey) = 0;
  // Screen-space quad in pixels, origin top-left.
  virtual void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) = 0;
  virtual void endFrame() = 0;
  virtual u32 width() const = 0;
  virtual u32 height() const = 0;
};

class IAudioBackend {
 public:
  virtual ~IAudioBackend() {}
  virtual bool init() = 0;
  virtual void shutdown() = 0;
};

class IFileSystem {
 public:
  virtual ~IFileSystem() {}
  // Reads a whole file into the arena (16-byte aligned). Empty span on failure.
  virtual Span<const u8> readFile(const char* path, Arena& arena) = 0;
  // Appends text to the persistent game log (host0:/rensga.log on PSP dev builds).
  virtual void writeLog(const char* text) = 0;
};

// ---- null implementations for headless tests -------------------------------

class NullPlatform : public IPlatform {
 public:
  bool init() override { return true; }
  void shutdown() override {}
  void pollInput(VirtualGamepad& pad) override { pad = scripted; }
  double timeSeconds() override { return time += step; }
  bool shouldQuit() override { return ++polls > maxPolls; }
  VirtualGamepad scripted;  // what pollInput returns
  double time = 0, step = 1.0 / 60.0;
  u32 polls = 0, maxPolls = 0xFFFFFFFFu;
};

class NullRenderer : public IRenderer {
 public:
  bool init(u32 w, u32 h) override { w_ = w; h_ = h; return true; }
  void setMeshes(const MeshTable& t) override { table = t; }
  void beginFrame(Color32) override { ++frames; draws = 0; overlays = 0; }
  void setCamera(const Matrix4& v, const Matrix4& p) override { view = v; proj = p; cameraSet = true; }
  void drawMesh(u32, const Matrix4&, u32) override { ++draws; ++totalDraws; }
  void drawOverlayQuad(i32, i32, i32, i32, Color32) override { ++overlays; }
  void endFrame() override {}
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }
  MeshTable table;
  Matrix4 view, proj;
  bool cameraSet = false;
  u32 frames = 0, draws = 0, overlays = 0, totalDraws = 0;

 private:
  u32 w_ = 0, h_ = 0;
};

class NullAudio : public IAudioBackend {
 public:
  bool init() override { return true; }
  void shutdown() override {}
};

// Empty file system: every read fails, log text is dropped.
class NullFileSystem : public IFileSystem {
 public:
  Span<const u8> readFile(const char*, Arena&) override { return {}; }
  void writeLog(const char*) override {}
};

// stdio-based file system (host sim, tests; PSP dev builds can use it for host0: too).
class StdioFileSystem : public IFileSystem {
 public:
  explicit StdioFileSystem(const char* root = "", const char* logPath = nullptr);
  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;

 private:
  char root_[256];
  char logPath_[256];
};

}  // namespace rensga
