// MVP-10: the four backend interfaces. engine/core sees backends only through these.
#pragma once
#include "rensga/core/math.h"
#include "rensga/core/memory.h"
#include "rensga/core/keycode.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/types.h"

namespace rensga {

struct VirtualGamepad {
  Scalar leftX = 0, leftY = 0;    // left stick / nub, -1..1 (+Y is up/forward)
  Scalar rightX = 0, rightY = 0;  // right stick, -1..1 (0 on pads without one)
  Scalar l2Value = 0, r2Value = 0;  // analog triggers 0..1 (digital pads report 0 or 1 with held.l2 / held.r2)
  KeyState keys;                    // keyboard, empty where the target has none
  // Only bools: Input::update walks this struct as a bool array.
  struct Buttons {
    bool south = false, east = false, west = false, north = false;
    bool l = false, r = false, start = false, select = false;
    bool up = false, down = false, left = false, right = false;
    bool l2 = false, r2 = false;  // triggers as buttons
  };
  Buttons held;
  Buttons pressed;   // went down this frame (set by Input::update)
  Buttons released;  // went up this frame (set by Input::update)
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
  // F3.06: pad vibration. `low` is the heavy (low-frequency) motor, `high` the light one, both 0..1; the call
  // replaces the previous one and (0, 0) stops it. Input only calls this on targets whose ConsoleTarget says
  // "rumble": true. `seconds` is how long Input will keep the effect (SDL uses it; others wait for the (0, 0) call).
  virtual void setVibration(Scalar low, Scalar high, Scalar seconds) { (void)low; (void)high; (void)seconds; }

  // ---- F5 platform seams. Every default means "this platform has no such thing".
  // Sets the CPU clock in MHz (PSP: 222 or 333 -> scePowerSetClockFrequency). False when unsupported or refused.
  virtual bool setCpuClock(i32 /*mhz*/) { return false; }
  // The system language as the SystemLanguage enum value (application.h); 0xFFFFFFFF = unknown, treated as English.
  virtual u32 language() { return 0xFFFFFFFFu; }
  // confirm button: true when the system's confirm button is the east face button (PSP in Japan: circle).
  virtual bool confirmButtonEast() { return false; }
  // Battery: percent 0..100 or -1 unknown; status 0 unknown, 1 charging, 2 discharging, 3 not charging, 4 full.
  virtual i32 batteryPercent() { return -1; }
  virtual i32 batteryStatus() { return 0; }
  // Suspend / resume / Home: returns the number of pending transitions since the last call, one per call, as
  // +1 = paused (suspend or Home popup opened), -1 = resumed, 0 = none. The loop turns them into OnApplicationPause.
  virtual i32 suspendState() { return 0; }
  // System text input (OSK). beginTextInput starts the dialog (false = unsupported); pollTextInput returns
  // 0 running, 1 done (text copied to out, UTF-8, NUL terminated), 2 cancelled, 3 unsupported / failed.
  virtual bool beginTextInput(const char* /*initial*/, const char* /*title*/, u32 /*maxLen*/) { return false; }
  virtual i32 pollTextInput(char* /*out*/, u32 /*outSize*/) { return 3; }
  // Memory use: bytes of system RAM / video RAM in total and in use (0 = unknown).
  virtual u64 systemMemoryBytes() { return 0; }
  virtual u64 graphicsMemoryBytes() { return 0; }
  virtual u64 usedSystemMemoryBytes() { return 0; }
  virtual u64 usedGraphicsMemoryBytes() { return 0; }
  // Save dialog (sceUtilitySavedata). slot is the save name; save: `data` holds `bytes` to write; load: `data` is a buffer of
  // `bytes` capacity to fill. Returns -1 = no dialog on this platform (the engine falls back to the save file store),
  // 0 = cancelled or failed, > 0 = bytes saved or loaded. May block (the platform draws its own frames meanwhile).
  virtual i32 saveDialog(const char* /*slot*/, void* /*data*/, u32 /*bytes*/, bool /*load*/) { return -1; }
  // F6 (A3): the title and detail line SaveSystem::Write gives its slot, for the platform's own save browser (PSP: PARAM.SFO of the
  // next SAVEDATA save). Called before the write; platforms without per-save titles ignore it.
  virtual void setSaveMetadata(const char* /*title*/, const char* /*detail*/) {}
};

// Meshes are cooked vertex bytes (pack VERT section) described by MeshRec entries.
struct MeshTable {
  const PackMeshRec* meshes = nullptr;
  u32 meshCount = 0;
  const PackVertex* vertices = nullptr;  // 16-byte aligned, lives in the pack
  u32 vertexCount = 0;
  // F4: optional compact-vertex description (pack MSHX). nullptr = the vertices are float PackVertex. When non-null
  // with format kMeshFormatCompact16, `vertices` actually points at PackVertexCompact[vertexCount] (cast by the
  // PSP renderer; every other renderer never sees a pack with MSHX because only the psp-target cook emits it).
  const PackMeshExt* ext = nullptr;
  // F5: optional lit / skinned meshes (pack MSHL, VTX3, BTCH). lit == nullptr means every mesh is baked. lit[i] is parallel
  // to meshes[i]; a non-baked mesh has meshes[i].vertexCount == 0 and its vertices at vtx3 + lit[i].vertexByteOffset.
  const PackMeshLit* lit = nullptr;
  const u8* vtx3 = nullptr;  // 16-byte aligned, lives in the pack
  u32 vtx3Size = 0;
  const PackBoneBatch* batches = nullptr;
  u32 batchCount = 0;
};

// F5: one light for the frame. type 0 = directional (`vec` is the direction the light TRAVELS, world space, normalized),
// 1 = point (`vec` is the world position, `range` the distance where it reaches 0).
struct RenderLight {
  u8 type = 0;
  Color32 color = Color32(255, 255, 255, 255);
  Scalar intensity = 1;
  Scalar range = 0;
  Vector3 vec = Vector3(0, -1, 0);
};
constexpr u32 kMaxFrameLights = 32;
constexpr u32 kMaxDrawLights = 8;
// F5: per-draw state of drawMeshEx. `lights` holds indices into the array given to setFrameLights, strongest first;
// `lightCount` is at most the target's maxLightsPerObject (PSP 4). `palette` is paletteCount skin matrices
// (joint model-space matrix x inverse bind, indexed by SKIN joint) for skinned meshes, null otherwise.
// `texture` 0 = untextured; `matColor` multiplies vertex color; `matFlags` is kMat* from the MATL record.
struct DrawParams {
  u32 texture = 0;
  Color32 matColor = Color32(255, 255, 255, 255);
  u32 matFlags = 0;
  u8 lightCount = 0;
  u8 lights[8] = {};
  const Matrix4* palette = nullptr;
  u32 paletteCount = 0;
};

// Pixel formats of TextureDesc (contract 4, Engine 2).
namespace tex_format {
constexpr u8 RGBA8 = 0, CLUT4 = 1, CLUT8 = 2, RGBA5551 = 3, RGBA4444 = 4;  // same numbers as PackTexFormat
}
// tex_flag mirrors PackTexRec::flags. GL and GS renderers return 0 from uploadTexture when flags != 0.
namespace tex_flag {
constexpr u16 kSwizzled = kTexSwizzled;
}
// CLUT8: `pixels` is one index byte per pixel. CLUT4: two indices per byte, low nibble first, rows padded to a
// whole byte (the same layout as the pack TEXR section). `palette` has 16 or 256 RGBA8 entries (R in the low byte).
// RGBA8: `pixels` is width*height u32 in memory order R,G,B,A. RGBA5551 / RGBA4444: width*height u16 (see PackTexFormat).
// F4: width / height are the SOURCE size (UV space); storedWidth / storedHeight (0 = same) is the size of the pixel
// array the cooker stored (padded, see PackTexRec). flags carries PackTexRec::flags (tex_flag::kSwizzled).
struct TextureDesc {
  u32 width = 0, height = 0;
  u8 format = 0;
  const void* pixels = nullptr;
  const u32* palette = nullptr;
  u32 storedWidth = 0, storedHeight = 0;
  u32 flags = 0;
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

  // ---- 2D (Engine 2). Defaults are no-ops so renderers without 2D (GU until F4, Null) compile untouched.
  // Returns a texture handle (> 0), or 0 on failure.
  virtual u32 uploadTexture(const TextureDesc&) { return 0; }
  // src in texture pixels (origin top-left), dst in virtual pixels (origin top-left, +Y down).
  // Sprites and rects are drawn after 3D, ordered by sortKey (then texture) at endFrame.
  virtual void drawSprite(u32 /*tex*/, const Rect& /*src*/, const Rect& /*dst*/, Color32 /*tint*/, u32 /*sortKey*/) {}
  virtual void drawRect(const Rect& /*dst*/, Color32 /*color*/, u32 /*sortKey*/) {}
  virtual void drawText(u32 /*fontTex*/, const PackFontRec& /*font*/, i32 /*x*/, i32 /*y*/, const char* /*text*/,
                        Color32 /*color*/, u32 /*sortKey*/) {}
  // Size of the 2D coordinate space; the GL renderer letterboxes it into the window.
  virtual void setVirtualResolution(u32 /*w*/, u32 /*h*/) {}

  // ---- F5 shared 3D. Defaults keep renderers without lighting (GS, Null) compiling untouched.
  // Replaces the frame's lights (call after beginFrame, before the draws). `ambient` is added to every lit surface.
  virtual void setFrameLights(const RenderLight* /*lights*/, u32 /*count*/, Color32 /*ambient*/) {}
  // Linear distance fog from `start` to `end` (view-space distance); on == false turns it off.
  virtual void setFog(bool /*on*/, Color32 /*color*/, Scalar /*start*/, Scalar /*end*/) {}
  // Draws lit / textured / skinned meshes. Baked meshes with default params behave exactly like drawMesh.
  virtual void drawMeshEx(u32 meshIndex, const Matrix4& world, u32 sortKey, const DrawParams& /*params*/) {
    drawMesh(meshIndex, world, sortKey);
  }
};

class AudioMixer;
// Pull model (Engine 4): the backend owns the device and pulls int16 stereo frames from the mixer.
// All calls happen on the main thread; see audio.h for the threading model.
class IAudioBackend {
 public:
  virtual ~IAudioBackend() {}
  // sampleRate 22050 or 44100. A null mixer / rate 0 opens nothing (NullAudio).
  virtual bool init(u32 sampleRate = 0, AudioMixer* mixer = nullptr) = 0;
  virtual void shutdown() = 0;
  // Once per frame: keeps the device fed (calls mixer->update() and mixer->mix()).
  virtual void update() {}
  virtual u32 sampleRate() const { return 0; }
};

class IFileSystem {
 public:
  virtual ~IFileSystem() {}
  // Reads a whole file into the arena (16-byte aligned). Empty span on failure.
  virtual Span<const u8> readFile(const char* path, Arena& arena) = 0;
  // Appends text to the persistent game log (host0:/rensga.log on PSP dev builds).
  virtual void writeLog(const char* text) = 0;

  // Virtual paths: "data:/<rel>" is the game data root (read only), "save:/<rel>" is writable.
  // A path without a prefix is taken as relative to the data root.
  // Streams use a fixed slot table (kMaxStreams, no heap). Default implementations fail.
  static constexpr i32 kMaxStreams = 8;
  // Returns a handle >= 0, or -1 (missing file or no free slot).
  virtual i32 openStream(const char*) { return -1; }
  // Reads up to `bytes` into dst; returns the bytes read (0 at the end or on a bad handle).
  virtual u32 readStream(i32, void*, u32) { return 0; }
  virtual bool seekStream(i32, u32) { return false; }
  virtual void closeStream(i32) {}
  // Writes (replaces) a file, normally under "save:/". False on failure or a read-only root.
  virtual bool writeFile(const char*, const void*, u32) { return false; }
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
  void setFrameLights(const RenderLight* l, u32 n, Color32 amb) override {
    lightCount = n > kMaxFrameLights ? kMaxFrameLights : n;
    for (u32 i = 0; i < lightCount; ++i) lights[i] = l[i];
    ambient = amb;
  }
  void setFog(bool on, Color32 c, Scalar s, Scalar e) override { fogOn = on; fogColor = c; fogStart = s; fogEnd = e; }
  void drawMeshEx(u32 mesh, const Matrix4& w, u32 key, const DrawParams& p) override {
    drawMesh(mesh, w, key);
    ++exDraws;
    lastParams = p;
    lastExMesh = mesh;
  }
  void drawOverlayQuad(i32, i32, i32, i32, Color32) override { ++overlays; }
  void endFrame() override {}
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }
  MeshTable table;
  Matrix4 view, proj;
  bool cameraSet = false;
  u32 frames = 0, draws = 0, overlays = 0, totalDraws = 0;
  // F5: what drawMeshEx / setFrameLights / setFog received (draws also counts drawMeshEx calls).
  u32 exDraws = 0, lastExMesh = 0xFFFFFFFFu, lightCount = 0;
  DrawParams lastParams;
  RenderLight lights[kMaxFrameLights];
  Color32 ambient = Color32(0, 0, 0, 255);
  bool fogOn = false;
  Color32 fogColor = Color32(0, 0, 0, 255);
  Scalar fogStart = 0, fogEnd = 0;

 private:
  u32 w_ = 0, h_ = 0;
};

class NullAudio : public IAudioBackend {
 public:
  bool init(u32 = 0, AudioMixer* = nullptr) override { return true; }
  void shutdown() override {}
};

// Empty file system: every read fails, log text is dropped.
class NullFileSystem : public IFileSystem {
 public:
  Span<const u8> readFile(const char*, Arena&) override { return {}; }
  void writeLog(const char*) override {}
};

// stdio-based file system (host sim, tests; PSP dev builds can use it for host0: too).
// `root` is the data root (prepended as-is, so give it a trailing '/'); `saveRoot` is where
// "save:/" goes (defaults to the data root).
class StdioFileSystem : public IFileSystem {
 public:
  explicit StdioFileSystem(const char* root = "", const char* logPath = nullptr, const char* saveRoot = nullptr);
  ~StdioFileSystem() override;
  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;
  i32 openStream(const char* path) override;
  u32 readStream(i32 handle, void* dst, u32 bytes) override;
  bool seekStream(i32 handle, u32 offset) override;
  void closeStream(i32 handle) override;
  bool writeFile(const char* path, const void* data, u32 bytes) override;

 private:
  bool resolve(const char* path, char* out, u32 outSize, bool* isSave) const;
  char root_[256];
  char saveRoot_[256];
  char logPath_[256];
  void* streams_[kMaxStreams];
};

}  // namespace rensga
