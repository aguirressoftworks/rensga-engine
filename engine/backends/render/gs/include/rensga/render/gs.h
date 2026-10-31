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
// VRAM (4 MB), explicit layout (see GsVramLayout, logged at init):
//   [ frame buffer 0 | frame buffer 1 | z buffer (only with enableZBuffer, 16-bit; off today: 2D needs none) |
//     CLUT area (kClutSlots x 1 KB) | texture area (8 KB GS pages, to the end of VRAM) ]
// The frame buffers are 640x448 (NTSC) or 640x512 (PAL) 32-bit, allocated by gsKit_init_screen. Everything behind them
// is managed here, not by gsKit's bump pointer. A texture is uploaded the first time a frame draws it and gets a run of
// whole pages (first fit) plus a CLUT slot if it is CLUT4/CLUT8. When no run is free, the least recently used resident
// texture is evicted. A texture already drawn since the last kick still has queued draws that read its VRAM, so it is
// never overwritten before those draws run: the queue is kicked first (gsKit_queue_exec), then it may be evicted.
// All tables are fixed-size; there is no heap use after init. Every kLogEveryFrames frames the renderer logs
// "rensga: vram fb=<KB> tex=<KB used>/<KB area> clut=<KB used> free=<KB> evictions=<n> uploads=<n>" (totals).
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
  u32 kicks = 0;                           // mid-frame queue kicks forced by VRAM pressure in the last frame
  u32 evictions = 0;                       // textures evicted from VRAM in the last frame
  u32 cpuMicros = 0;                       // EE time from beginFrame to the end of queueing (before the vsync wait)
  u32 vramUsed = 0;                        // VRAM bytes in use (frame buffers + CLUT area + resident textures)
};

class GsRenderer : public IRenderer {
 public:
  static constexpr u32 kMaxTextures = 128;
  static constexpr u32 kMaxCommands = 8192;
  static constexpr u32 kClutSlots = 32;       // 1 KB each (a CLUT8 palette); CLUT4 uses a slot too
  static constexpr u32 kMaxPages = 512;       // 8 KB GS pages tracked (4 MB / 8 KB)
  static constexpr u32 kLogEveryFrames = 120;

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
  // Cumulative counters and layout, for tests and the periodic log line.
  struct VramInfo { u32 fbBytes, areaBytes, usedBytes, clutBytes, freeBytes, evictions, uploads; };
  VramInfo vramInfo() const;
  u32 failedUploads() const { return failedUploads_; }  // uploadTexture calls that returned 0 (logged as ERROR, repeated in the periodic vram line)

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
  void kick();
  bool allocPages(Tex& t, u32 pages, u32 index);
  void evict(u32 index);
  void logVram() const;

  u32 w_ = 0, h_ = 0, vw_ = 0, vh_ = 0;
  GsVideo video_;
  bool videoSet_ = false, zbuffer_ = false;
  void* gs_ = nullptr;
  u32 texCount_ = 0, cmdCount_ = 0, failedUploads_ = 0;
  u32 useClock_ = 0, kickEpoch_ = 1;                      // LRU clock; bumps when the queue is kicked
  u32 fbBytes_ = 0, pageBase_ = 0, pageCount_ = 0, clutBase_ = 0;  // layout (bytes / pages)
  u32 evictionsTotal_ = 0, uploadsTotal_ = 0, frameNo_ = 0;
  GsStats stats_;
  GsStats frame_;
  u32 frameStartTicks_ = 0;
};

}  // namespace rensga
