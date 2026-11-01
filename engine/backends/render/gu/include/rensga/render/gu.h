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
  // ---- F5.01 shared 3D: up to 4 hardware lights per draw, material color, 3D textures, fog, back-face culling, near-plane
  // clipping, hardware skinning (8 bones per batch). Baked meshes keep the F4 path. The palette of a draw must stay valid until
  // endFrame (the draws are recorded there). Lights and fog stay set until changed.
  void setFrameLights(const RenderLight* lights, u32 count, Color32 ambient) override;
  void setFog(bool on, Color32 color, Scalar start, Scalar end) override;
  void drawMeshEx(u32 meshIndex, const Matrix4& world, u32 sortKey, const DrawParams& params) override;
  void drawOverlayQuad(i32 x, i32 y, i32 w, i32 h, Color32 color) override;
  // ---- F6.09 cameras and effects. The draws of a frame are recorded and run at endFrame, so every setCamera / setViewport / clearView
  // that follows draws starts a new pass with its own matrices, viewport rectangle and clear (at most kMaxPasses per frame; the
  // extra cameras are merged into the last pass). World-space quads (particles, ribbons) are recorded per pass and drawn after its
  // meshes: blended, depth tested, not depth written.
  void setViewport(i32 x, i32 y, i32 w, i32 h) override;
  void clearView(u32 flags, Color32 color) override;
  bool drawQuads3D(u32 tex, const Quad3D* quads, u32 count, u32 sortKey) override;
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
  // F5.01 counters of the last finished frame.
  u32 lastLitDraws() const { return litDraws_; }
  u32 lastBatches() const { return batchesDrawn_; }
  u32 lastClipped() const { return clipped_; }
  u32 lastQuads() const { return quadsDrawn_; }   // F6.09: particle / ribbon quads recorded last frame
  u32 lastPasses() const { return passCount_; }   // camera passes of the last frame
  u32 quadsDropped() const { return quadsDropped_; }

 private:
  struct Draw { u32 key, mesh; Matrix4 world; DrawParams params; bool ex; };
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
    u16 srcW = 0, srcH = 0;                  // source size (UV space); 3D meshes scale their UVs by src / stored
  };

  void bindTexture(const Tex& t);
  void setState3D();
  void setState2D();
  void drawMeshes();
  // gu_3d.cpp
  void enterBakedState();
  void enterLitState();
  void leave3DState();
  void drawLit(const Draw& d, const PackMeshLit& ml);
  void bind3DTexture(u32 handle);
  void applyLights(const DrawParams& p, bool unlit);
  void drawSprites();
  // F6.09 (gu_fx.cpp, gu_3d.cpp)
  struct Pass {
    Matrix4 view, proj;
    i32 x = 0, y = 0, w = 480, h = 272;  // viewport rectangle, origin top-left
    u32 clearFlags = 0;                  // kClearColor | kClearDepth, applied before the pass's draws (passes after the first)
    Color32 clearColor;
    u32 firstDraw = 0, firstRun = 0;     // first draws_ / qruns_ entry of the pass
  };
  struct QuadRun { u32 tex, byteOffset, vertexCount, pass; };
  static constexpr u32 kMaxPasses = 6, kMaxQuadRuns = 96;
  Pass& editPass();
  void applyViewport(const Pass& p);
  void resetViewport();
  void drawPassQuads(u32 pass);
  void computeBakedBounds();
  bool drawBakedClipped(const Draw& d, const PackMeshRec& m);

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

  // F5.01 state. Lights are copied when set; the draws read them at endFrame.
  u16 order_[kMaxDraws];
  RenderLight lights_[kMaxFrameLights];
  u32 lightCount_ = 0;
  Color32 ambient_ = Color32(0, 0, 0, 255);
  bool fogOn_ = false;
  Color32 fogColor_ = Color32(0, 0, 0, 255);
  Scalar fogStart_ = 0, fogEnd_ = 0;
  u32 litDraws_ = 0, batchesDrawn_ = 0, clipped_ = 0, litSlots_ = 0, nearClipFallbacks_ = 0;
  u8 mode3D_ = 0;           // 0 none, 1 baked state, 2 lit state
  u32 curTex_ = 0;          // 3D texture handle bound (0 = none)
  u32 lightSig_ = 0;        // signature of the light setup last sent
  u32 hwMask_ = 0;          // hardware light slots enabled
  u32 cullState_ = 0xFF;    // 0 off, 1 front CW, 2 front CCW
  bool alphaTest3D_ = false;

  // F6.09 passes, quads and near-plane clipping of big baked meshes
  Pass passes_[kMaxPasses];
  u32 passCount_ = 1;
  QuadRun qruns_[kMaxQuadRuns];
  u32 qrunCount_ = 0, quadBytes_ = 0, quadsDrawn_ = 0, quadsDropped_ = 0;
  u32 clipUsed_ = 0;       // clipped vertices handed out this frame (each clipped draw owns a slice until the list has run)
  u32 boundsCount_ = 0;    // baked meshes with a bounding sphere (computed in setMeshes)
};

}  // namespace rensga
