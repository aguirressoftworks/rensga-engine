// PS2 Graphics Synthesizer renderer through gsKit (Engine 2, PS2 half).
//
// What it draws: 2D. Sprites (CLUT4 / CLUT8 / RGBA8 textures), solid rects and bitmap-font text, ordered by sortKey.
// 3D: drawMesh is a deliberate no-op here. The ps2gl / VU1 3D path is roadmap phase F8.01 (PS2 Engine); until then a
// scene is shown only through its 2D calls. drawOverlayQuad is a rect that sorts after every sprite.
//
// Video: 640x448 NTSC or 640x512 PAL, chosen by the caller (main_ps2 passes Ps2Platform::info().pal) through
// setVideo() before init(). Interlace is selectable (see GsVideo). Every frame ends with gsKit_sync_flip, which waits
// for vsync, so the frame rate is locked to 60 (NTSC) or 50 (PAL) fields per second.
//
// VRAM (4 MB): two 32-bit frame buffers (640x448x4 = 1.1 MB each) are allocated first by gsKit, no z buffer
// (2D only; enableZBuffer(true) before init adds a 16-bit one for the later 3D path). Everything after that is the
// texture area: a bump ring. A texture is uploaded the first time a frame draws it. When the area is full, the draw
// queue is flushed, the ring restarts from the beginning and every texture is marked non-resident; the ones still
// used are uploaded again on demand (a whole-ring LRU: coarse, but it costs no per-texture bookkeeping in VRAM).
// Palettes are copied (RGBA8 alpha 0..255 -> GS alpha 0..128, CLUT8 entries swizzled for the GS CSM1 layout).
// RGBA8 textures need their alpha converted too, so they pass through a 256 KB staging buffer (max 256x256).
//
// Memory: no heap after init. Commands go into a fixed array (kMaxCommands) per frame; extra draws are dropped
// and counted in stats().dropped.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

enum class GsInterlace : u8 {
  Field,        // 640x448 / 640x512 interlaced, field mode (gsKit default; the GS draws each field, half the lines shown)
  Frame,        // interlaced, frame mode (full frame drawn each vsync; sharper still images, more flicker)
  Progressive,  // non-interlaced "240p": 640x224 (NTSC) or 640x256 (PAL), vertical pixels doubled by the TV
};

struct GsVideo {
  bool pal = false;  // false: NTSC 640x448 (60 Hz), true: PAL 640x512 (50 Hz)
  GsInterlace interlace = GsInterlace::Field;
};

struct GsStats {
  u32 sprites = 0, rects = 0, glyphs = 0;  // drawn in the last frame
  u32 dropped = 0;                         // draws lost to the command cap in the last frame
  u32 uploads = 0;                         // texture uploads in the last frame
  u32 ringWraps = 0;                       // VRAM ring restarts in the last frame
  u32 cpuMicros = 0;                       // EE time from beginFrame to the end of queueing (before the vsync wait)
  u32 vramUsed = 0;                        // VRAM bytes in use (frame buffers + textures) at the end of the last frame
};

class GsRenderer : public IRenderer {
 public:
  static constexpr u32 kMaxTextures = 128;
  static constexpr u32 kMaxCommands = 8192;

  // Call before init(). Without it the console's detected signal and field mode are used.
  void setVideo(const GsVideo& video) { video_ = video; videoSet_ = true; }
  void enableZBuffer(bool on) { zbuffer_ = on; }

  bool init(u32 width, u32 height) override;
  void setMeshes(const MeshTable&) override {}
  void beginFrame(Color32 clear) override;
  void setCamera(const Matrix4&, const Matrix4&) override {}
  void drawMesh(u32, const Matrix4&, u32) override {}  // F8.01 (ps2gl on VU1)
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) override;
  void endFrame() override;
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }

  u32 uploadTexture(const TextureDesc& desc) override;
  void drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) override;
  void drawRect(const Rect& dst, Color32 color, u32 sortKey) override;
  void drawText(u32 fontTex, const PackFontRec& font, i32 x, i32 y, const char* text, Color32 color, u32 sortKey) override;
  void setVirtualResolution(u32 w, u32 h) override { vw_ = w ? w : w_; vh_ = h ? h : h_; }

  // Debug capture: writes both frame buffers as raw 640xH RGBA8 (R first, alpha 0..128) to <base>0.raw / <base>1.raw
  // (gsKit_vram_dump; host: on dev builds). Which one is on screen is shown by activeBuffer(). Not for shipping builds.
  void dumpFrameBuffers(const char* base);
  const GsStats& stats() const { return stats_; }
  const GsVideo& video() const { return video_; }

 // Implementation types (public only so gs_renderer.cpp can keep its fixed tables at file scope).
  struct Command {  // 48 bytes
    u64 order;      // sortKey << 32 | texture << 16 | submit index: sorts by key, then texture, then submit order
    float sx, sy, sw, sh;  // source rect (pixels), unused for rects
    float dx, dy, dw, dh;  // destination (virtual pixels)
    u32 color;      // packed RGBA, R in the low byte
    u32 tex;        // 0 = solid rect
  };
  struct Tex;

 private:
  bool push(const Command& c);
  bool ensureResident(Tex& t);
  void flushRing();

  u32 w_ = 0, h_ = 0, vw_ = 0, vh_ = 0;
  GsVideo video_;
  bool videoSet_ = false, zbuffer_ = false;
  void* gs_ = nullptr;
  u32 texCount_ = 0, cmdCount_ = 0;
  u32 generation_ = 1;
  GsStats stats_;
  GsStats frame_;
  u32 frameStartTicks_ = 0;
};

}  // namespace rensga
