// GU renderer (MVP-25, -26; F4.04): 5650 framebuffers + 16-bit depth in VRAM, double-buffered display lists,
// perspective via sceGum, meshes drawn in sortKey order (float or compact 16-bit vertices), and a 2D path: sprites,
// rects and text as GU_SPRITES in through mode, sorted by (sortKey, texture, submission order).
// VRAM map (gu_vram.h): fb0 0x000000, fb1 0x044000, depth 0x088000, textures first-fit from 0x0CC000 to 0x200000.
// Header is free of PSP includes so host tests can include it.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/render/gu_sprite.h"
#include "rensga/render/gu_vram.h"

namespace rensga {

class GuRenderer : public IRenderer {
 public:
  static constexpr u32 kMaxDraws = 512;
  static constexpr u32 kMaxTextures = GuSpriteQueue::kMaxTexture;  // handles 1..255
  static constexpr u32 kMaxSprites = 3072;                         // sprites + rects + glyphs + overlays per frame

  bool init(u32 width, u32 height) override;
  void setMeshes(const MeshTable& table) override;
  void beginFrame(Color32 clear) override;
  void setCamera(const Matrix4& view, const Matrix4& proj) override;
  void drawMesh(u32 meshIndex, const Matrix4& world, u32 sortKey) override;
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) override;
  void endFrame() override;
  u32 width() const override { return w_; }
  u32 height() const override { return h_; }

  // ---- 2D (Engine 2). Coordinates are virtual pixels (origin top-left, +Y down); sprites are clipped to the screen.
  // uploadTexture accepts RGBA8, RGBA5551, RGBA4444, CLUT4 and CLUT8, swizzled or not. The stored size must be a power
  // of two up to 512 (the cooker pads). Returns a handle 1..255, or 0 on failure (logged).
  u32 uploadTexture(const TextureDesc& desc) override;
  void drawSprite(u32 tex, const Rect& src, const Rect& dst, Color32 tint, u32 sortKey) override;
  void drawRect(const Rect& dst, Color32 color, u32 sortKey) override;
  void drawText(u32 fontTex, const PackFontRec& font, i32 x, i32 y, const char* text, Color32 color,
                u32 sortKey) override;
  // The 2D space is scaled to fit 480x272 (uniform scale, centered) when it differs from the screen.
  void setVirtualResolution(u32 w, u32 h) override;

  // ---- Backend extras (not on IRenderer).
  // Waits until the GE has finished every queued list. Call before reading VRAM or exiting.
  void flush();
  // Writes the most recently drawn frame (the 5650 or 8888 draw buffer, after flush()) as a 24-bit BMP.
  // Default path is the CI screenshot file. Returns false when the file cannot be written.
  bool dumpFrame(const char* path = "host0:/rensga_shot.bmp");
  // Frees every texture (VRAM and palettes) so a hot reload can upload the pack again. Handles become invalid.
  void releaseTextures();
  u32 textureCount() const { return texCount_; }
  u32 vramTextureBytes() const { return vram_.used(); }
  u32 ramTextureCount() const { return ramTex_; }
  u32 spritesDropped() const { return queue_.dropped(); }
  u32 lastSpriteCount() const { return lastSprites_; }
  u32 lastTriangles() const { return triangles_; }
  u32 lastListBytes() const { return lastListBytes_; }  // display list bytes the last frame used
  u32 listOverflows() const { return listOverflows_; }  // frames cut short because the list was nearly full

 private:
  struct Draw { u32 key, mesh; Matrix4 world; };
  struct Tex {
    const void* pixels = nullptr;  // address the GE reads (VRAM 0x04..., or the pack in RAM)
    const void* clut = nullptr;    // palette address (VRAM or pack), null for direct color
    u16 w = 0, h = 0;              // stored size (power of two)
    u8 psm = 0;                    // GU pixel format
    u8 clutBlocks = 0;             // 8-entry blocks to load
    u8 clutMask = 0;               // 0x0F (CLUT4) or 0xFF (CLUT8)
    u8 swizzled = 0;
    u32 vramOff = 0, vramBytes = 0;         // pixel block in VRAM (bytes 0 = in RAM)
    u32 clutVramOff = 0, clutVramBytes = 0;  // palette block in VRAM
  };

  void bindTexture(const Tex& t);
  void setState3D();
  void setState2D();
  void drawMeshes();
  void drawSprites();

  u32 w_ = 480, h_ = 272;  // virtual (2D) size; the screen is always 480x272
  GuViewMap map_;
  MeshTable table_;
  bool compact_ = false;
  Matrix4 view_, proj_;
  Color32 clear_;
  Draw draws_[kMaxDraws];
  u32 drawCount_ = 0;
  u32 triangles_ = 0;
  Tex tex_[kMaxTextures];
  u32 texCount_ = 0, ramTex_ = 0;
  bool uploadsDirty_ = false;
  GuVramAllocator vram_;
  GuSpriteQueue queue_;
  u32 lastSprites_ = 0, lastListBytes_ = 0, listOverflows_ = 0;
  u32 frame_ = 0;
  u32 drawFb_ = 0;       // VRAM offset of the buffer the current frame is drawn into
  u32 lastDrawnFb_ = 0;  // VRAM offset of the last finished frame (what dumpFrame reads)
  bool ready_ = false;
};

}  // namespace rensga
