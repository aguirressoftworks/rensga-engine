// F5.01 dev probe (PSP only, built with -DRENSGA_GU_PROBE=ON): boots host0:/game.rpak (tests/fixtures/pack3d_builder.h, written by
// tools/gu3d_makepack.cpp), draws the builder's cube row and the animated bar straight through IRenderer::drawMeshEx
// (no GameLoop, so it does not wait for renderWorld) and exits after RENSGA_EXIT_AFTER_FRAMES. Pixel probes read the
// RENSGA_GU_DUMP_FRAMES bitmaps. Variants (compile definitions): GU3D_C16=1 (pack with a C16 cube), GU3D_CLIP=1 (flag the cube
// kMeshNeedsNearClip and put the camera inside it), GU3D_SCALE=1 (draw a x2 scaled cube to see how the GE treats normal length), GU3D_DIM=1 (one dim light so nothing saturates).
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>

#include "rensga/core/log.h"
#include "rensga/core/loop.h"
#include "rensga/core/scene_manager.h"
#include "rensga/platform/psp.h"
#include "rensga/render/gu.h"
#include "runtime_boot.h"

PSP_MODULE_INFO("gu3dprobe", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

using namespace rensga;

#ifndef RENSGA_EXIT_AFTER_FRAMES
#define RENSGA_EXIT_AFTER_FRAMES 60
#endif

namespace {
PackMeshLit g_lit[2];
alignas(16) u32 g_checker[16 * 16];
Matrix4 g_palette[2];

[[maybe_unused]] Matrix4 trs(const Vector3& t, const Quaternion& q, const Vector3& s) { return Matrix4::translation(t) * Matrix4::rotation(q) * Matrix4::scaling(s); }
}  // namespace

int main(int, char**) {
  pspInstallLogSinks();
  static PspPlatform platform;
  static GuRenderer renderer;
  if (!platform.init()) return 2;
  static PspFileSystem fs("host0:/");
  static Arena arena;
  if (!arena.create(16u << 20)) return 3;
  static World world;
  WorldConfig cfg;
  if (!world.init(cfg, arena)) return 4;
  if (!renderer.init(480, 272)) return 5;
  static BootState boot;
  static u32 handles[GfxAssets::kMaxTextures] = {};
  BootDeps deps;
  deps.fs = &fs; deps.arena = &arena; deps.world = &world;
  deps.packPath = "data:/game.rpak";
  deps.renderer = &renderer; deps.initRenderer = false;
  deps.textureHandles = handles; deps.textureHandleCapacity = GfxAssets::kMaxTextures;
  if (!bootPack(deps, boot)) { RLOG_ERROR("probe", "bootPack failed"); return 6; }
#if defined(GU3D_WORLD)
  // The real path: GameLoop + renderWorld straight into the GuRenderer (no ProfilingRenderer in between).
  {
#if defined(GU3D_FORCECLIP)  // flag the room (mesh 0) NeedsNearClip as the cooker would for a big mesh
    {
      static PackMeshLit litCopy[64];
      MeshTable m2 = boot.loaded.meshes;
      const u32 n = m2.meshCount < 64 ? m2.meshCount : 64;
      for (u32 i = 0; i < n; ++i) litCopy[i] = m2.lit[i];
      litCopy[0].flags |= kMeshNeedsNearClip;
      m2.lit = litCopy;
      renderer.setMeshes(m2);
    }
#endif
    static GameLoop loop;
    loop.init(world);
    u32 f = 0;
    while (!platform.shouldQuit() && f < RENSGA_EXIT_AFTER_FRAMES) {
      VirtualGamepad pad;
      platform.pollInput(pad);
#if defined(GU3D_LIGHTS12)
      if (f == 3) SceneManager::LoadScene("Lights12");  // 12 point lights: each object takes its 4 strongest
#endif
      loop.frame(1.0f / 60.0f, pad);
#if defined(GU3D_LIGHTS12)  // the sample's cameras are yawed 180 degrees (they look along +Z): turn this one to look down -Z at the cubes
      if (f > 4)
        if (GameObject* cam = world.findByName("Camera")) cam->transform.lrot_ = Quaternion::AngleAxis(-35.0f, Vector3(1, 0, 0));
#endif
#if defined(GU3D_CAM)  // look at the character from the other side of the room (engine cameras look down -Z; yaw 180 looks along +Z)
      if (GameObject* cam = world.findByName("Camera")) {
        cam->transform.lpos_ = Vector3(0.6f, 1.5f, -3.0f);
        cam->transform.lrot_ = Quaternion::AngleAxis(180.0f, Vector3(0, 1, 0)) * Quaternion::AngleAxis(-8.0f, Vector3(1, 0, 0));
      }
#endif
      if (f == 5) {
        if (GameObject* cam = world.findByName("Camera")) {
          const Matrix4 m = cam->transform.localToWorld();
          const Vector3 fwd = m.transformVector(Vector3(0, 0, -1));
          RLOG_INFO("probe", "rensga: probe camera pos=%.2f,%.2f,%.2f fwd(-z)=%.2f,%.2f,%.2f", m.m[12], m.m[13], m.m[14], fwd.x, fwd.y, fwd.z);
        }
      }
      renderWorld(world, renderer);
      ++f;
      heap::noteFrameDone();
      if (f == 1) heap::checkReset();
    }
    renderer.flush();
    heap::logAllocCheck();
    RLOG_INFO("probe", "rensga: probe world done frames=%u draws=%u batches=%u clipped=%u", static_cast<unsigned>(f),
              static_cast<unsigned>(renderer.lastLitDraws()), static_cast<unsigned>(renderer.lastBatches()),
              static_cast<unsigned>(renderer.lastClipped()));
    log::flushAll();
    platform.shutdown();
    return 0;
  }
#endif
  MeshTable mt = boot.loaded.meshes;
  RLOG_INFO("probe", "rensga: probe lit=%u batches=%u vtx3=%u", mt.lit ? 1u : 0u, static_cast<unsigned>(mt.batchCount), static_cast<unsigned>(mt.vtx3Size));
  if (!mt.lit) return 7;
  g_lit[0] = mt.lit[0];
  g_lit[1] = mt.lit[1];
  g_lit[0].flags |= kMeshWindingCCW;  // the builder's boxes are wound counter-clockwise (GL order) but leave the flag at 0
  g_lit[1].flags |= kMeshWindingCCW;
#if defined(GU3D_CLIP)
  g_lit[0].flags |= kMeshNeedsNearClip;
  g_lit[0].boundsCenter[1] = 0.5f;
#endif
  mt.lit = g_lit;
  renderer.setMeshes(mt);

  // Own 12x12-in-16x16 checker with a red guard band, to see the UV rescale (source 12, stored 16).
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      const bool inside = x < 12 && y < 12;
      const u32 v = ((x / 3 + y / 3) & 1) ? 0xFFE6E6E6u : 0xFF5A5A5Au;
      g_checker[y * 16 + x] = inside ? v : 0xFF0000FFu;  // ABGR: red outside
    }
  TextureDesc td;
  td.width = 12; td.height = 12; td.storedWidth = 16; td.storedHeight = 16;
  td.format = tex_format::RGBA8; td.pixels = g_checker;
  const u32 checker = renderer.uploadTexture(td);
  RLOG_INFO("probe", "rensga: probe checker handle=%u", static_cast<unsigned>(checker));

  const Matrix4 proj = Matrix4::perspective(60.0f, 480.0f / 272.0f, 0.1f, 60.0f);
  u32 frame = 0;
  while (!platform.shouldQuit() && frame < RENSGA_EXIT_AFTER_FRAMES) {
    VirtualGamepad pad;
    platform.pollInput(pad);
    renderer.beginFrame(Color32(40, 60, 90));
#if defined(GU3D_CLIP) || defined(GU3D_CLIPOFF)
#if defined(GU3D_FAR)
    Matrix4 view = Matrix4::lookAt(Vector3(0, 3.0f, 14.0f), Vector3(0.5f, 0, -4), Vector3(0, 1, 0));
#else
    Matrix4 view = Matrix4::lookAt(Vector3(0, 1.0f, 2.0f), Vector3(0.5f, 0, -4), Vector3(0, 1, 0));
#endif
#else
    Matrix4 view = Matrix4::lookAt(Vector3(0, 2.2f, 6.5f), Vector3(0, 0.8f, -4), Vector3(0, 1, 0));
#endif
    renderer.setCamera(view, proj);
    RenderLight ls[3];
    ls[0].type = 0; ls[0].vec = Vector3(-0.5f, -0.6f, -0.6f).normalized(); ls[0].color = Color32(255, 244, 220); ls[0].intensity = 1.0f;
    ls[1].type = 1; ls[1].vec = Vector3(2.5f, 1.5f, 1.0f); ls[1].color = Color32(255, 40, 40); ls[1].intensity = 1.0f; ls[1].range = 6.0f;
    ls[2].type = 1; ls[2].vec = Vector3(-2.5f, 1.5f, 1.0f); ls[2].color = Color32(40, 80, 255); ls[2].intensity = 1.0f; ls[2].range = 6.0f;
#if defined(GU3D_DIM)  // one dim directional light, no ambient: brightness differences show (no saturation)
    ls[0].intensity = 0.35f;
    renderer.setFrameLights(ls, 1, Color32(0, 0, 0));
#else
    renderer.setFrameLights(ls, 3, Color32(40, 40, 50));
#endif
#if defined(GU3D_DIM)
    renderer.setFog(false, Color32(40, 60, 90), 8.0f, 30.0f);
#else
    renderer.setFog(true, Color32(40, 60, 90), 8.0f, 30.0f);
#endif

    DrawParams lit;
    lit.lights[0] = 0; lit.lights[1] = 1; lit.lights[2] = 2;
#if defined(GU3D_DIM)
    lit.lightCount = 1;
#else
    lit.lightCount = 3;
#endif
    lit.texture = 0;
#if defined(GU3D_CLIP) || defined(GU3D_CLIPOFF)
    // A big slab floor: the camera stands inside its footprint, so its triangles cross (and pass behind) the near plane.
    renderer.drawMeshEx(0, Matrix4::translation(Vector3(0, -0.1f, 0)) * Matrix4::scaling(Vector3(8, 0.1f, 8)), 100, lit);
    renderer.drawMeshEx(1, Matrix4::translation(Vector3(0, 0, -2.0f)), 90, lit);
    renderer.endFrame();
    ++frame;
    continue;
#endif
    // A row of cubes receding in depth (fog gradient), the nearest one textured.
    for (int i = 0; i < 6; ++i) {
      DrawParams p = lit;
      if (i == 0) p.texture = checker;
      if (i == 5) { p.matFlags = kMatUnlit; p.matColor = Color32(255, 200, 80); }
      renderer.drawMeshEx(0, Matrix4::translation(Vector3(-2.0f + (i % 2) * 4.0f, 0, -4.0f * (i / 2) * 1.5f)), 100 + i, p);
    }
#if defined(GU3D_SCALE)
    renderer.drawMeshEx(0, trs(Vector3(-1.5f, 0, 3.0f), Quaternion::identity(), Vector3(1, 1, 1)), 120, lit);
    renderer.drawMeshEx(0, trs(Vector3(1.5f, 0, 3.0f), Quaternion::identity(), Vector3(2, 2, 2)), 121, lit);
#endif
    // Animated bar: joint 1 bends 45 degrees about Z over 30 frames (palette = joint model x inverse bind).
    {
      const float ang = 0.7853982f * (0.5f - 0.5f * Mathf::Cos(frame * 0.2094395f));
      g_palette[0] = Matrix4::identity();
      const Matrix4 j1 = Matrix4::translation(Vector3(0, 1, 0)) * Matrix4::rotation(Quaternion::AngleAxis(ang * 57.29578f, Vector3(0, 0, 1)));
      g_palette[1] = j1 * Matrix4::translation(Vector3(0, -1, 0));
      DrawParams p = lit;
      p.palette = g_palette; p.paletteCount = 2;
      renderer.drawMeshEx(1, Matrix4::translation(Vector3(0, 0, 0.0f)), 90, p);
    }
    renderer.endFrame();
    ++frame;
    heap::noteFrameDone();
    if (frame == 1) heap::checkReset();
  }
  renderer.flush();
  heap::logAllocCheck();
  RLOG_INFO("probe", "rensga: probe done frames=%u draws=%u batches=%u clipped=%u", static_cast<unsigned>(frame),
            static_cast<unsigned>(renderer.lastLitDraws()), static_cast<unsigned>(renderer.lastBatches()),
            static_cast<unsigned>(renderer.lastClipped()));
  log::flushAll();
  platform.shutdown();
  return 0;
}
