// PSP entry point: boot -> load host0:/game.rpak (or a built-in test cube) -> fixed-timestep loop -> render.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>

#include "rensga/Rensga.h"
#include "rensga/platform/psp.h"
#include "rensga/render/gu.h"

PSP_MODULE_INFO("rensga", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);  // everything except 1 MB for the system

using namespace rensga;

namespace {

// Built-in fallback: a unit cube with per-face colours (36 vertices, GU layout).
alignas(16) PackVertex g_cubeVerts[36];
alignas(16) PackMeshRec g_cubeMesh[1];

void buildTestCube() {
  static const float c[8][3] = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                                {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
  static const int f[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
  static const Color32 col[6] = {{220, 60, 60}, {60, 220, 60}, {60, 60, 220}, {220, 220, 60}, {60, 220, 220}, {220, 60, 220}};
  int n = 0;
  for (int i = 0; i < 6; ++i) {
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; ++k) {
      const float* p = c[f[i][idx[k]]];
      g_cubeVerts[n++] = PackVertex{col[i].packedABGR(), p[0], p[1], p[2]};
    }
  }
  g_cubeMesh[0] = PackMeshRec{0, 36, 0, 0};
}

}  // namespace

#ifdef RENSGA_EXIT_AFTER_FRAMES
// Screenshot/CI builds: dump the displayed 32-bit frame to host0:/rensga_shot.bmp so an emulator run can be captured
// without a window grab (PPSSPP's GL window reads black to X grabs under Xvfb).
static void dumpFrameBmp() {
  void* top = nullptr; int bufw = 0, fmt = 0;
  if (sceDisplayGetFrameBuf(&top, &bufw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME) < 0 || !top || fmt != 3) return;
  const unsigned char* px = reinterpret_cast<const unsigned char*>(reinterpret_cast<unsigned>(top) | 0x40000000u);
  const int w = 480, h = 272, rowBytes = w * 3;
  static unsigned char bmp[54 + 480 * 272 * 3];
  unsigned dataSize = rowBytes * h, fileSize = 54 + dataSize;
  unsigned char hdr[54] = {'B', 'M'};
  auto put32 = [&](int o, unsigned v) { for (int i = 0; i < 4; ++i) hdr[o + i] = (v >> (8 * i)) & 0xff; };
  put32(2, fileSize); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
  hdr[26] = 1; hdr[28] = 24; put32(34, dataSize);
  for (int i = 0; i < 54; ++i) bmp[i] = hdr[i];
  for (int y = 0; y < h; ++y) {
    const unsigned char* src = px + static_cast<unsigned>(y) * bufw * 4;
    unsigned char* dst = bmp + 54 + (h - 1 - y) * rowBytes;
    for (int x = 0; x < w; ++x) { dst[x * 3] = src[x * 4 + 2]; dst[x * 3 + 1] = src[x * 4 + 1]; dst[x * 3 + 2] = src[x * 4]; }
  }
  SceUID fd = sceIoOpen("host0:/rensga_shot.bmp", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if (fd >= 0) { sceIoWrite(fd, bmp, fileSize); sceIoClose(fd); }
}
#endif

int main() {
  pspInstallLogSinks();
  Application::setPlatform(RuntimePlatform::Psp);
  RLOG_INFO("game", "rensga: boot");

  PspPlatform platform;
  PspFileSystem fs;
  GuRenderer renderer;
  NullAudio audio;
  platform.init();
  audio.init();

  Arena arena;
  if (!arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes)) {
    RLOG_ERROR("game", "arena allocation failed");
    platform.shutdown();
    return 1;
  }
  WorldConfig cfg;
  World world;
  if (!world.init(cfg, arena)) {
    RLOG_ERROR("game", "world init failed");
    platform.shutdown();
    return 1;
  }
  renderer.init(480, 272);

  LoadedPack loaded;
  PackView view;
  bool havePack = false;
  Span<const u8> bytes = fs.readFile("game.rpak", arena);
  if (bytes && view.open(bytes.data, bytes.size, "game.rpak") && world.loadPack(view, &loaded)) {
    havePack = true;
    Input::loadActions(view, loaded.inputs, loaded.inputCount);
    renderer.setMeshes(loaded.meshes);
  } else {
    RLOG_WARN("game", "no usable host0:/game.rpak, drawing the built-in test cube");
    buildTestCube();
    MeshTable t;
    t.meshes = g_cubeMesh;
    t.meshCount = 1;
    t.vertices = g_cubeVerts;
    t.vertexCount = 36;
    renderer.setMeshes(t);
    GameObject* cam = world.createObject("Camera");
    cam->AddComponent<Camera>();
    cam->transform.lpos_ = Vector3(0, 1.2f, 3.5f);
    cam->transform.lrot_ = Quaternion::AngleAxis(-15.0f, Vector3(1, 0, 0));
    GameObject* cube = world.createObject("Cube");
    cube->AddComponent<MeshRenderer>()->meshIndex = 0;
  }
  u32 pickups = 0;
  world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
    GameObject* go = mr.gameObject();
    if (go && go->name[0] == 'P' && go->name[1] == 'i' && go->name[2] == 'c') ++pickups;
  });
  RLOG_INFO("game", "rensga: scene loaded objects=%u pickups=%u", world.objectCount(), pickups);

  GameLoop loop;
  loop.init(world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);

  double last = platform.timeSeconds();
  VirtualGamepad pad;
  u32 frame = 0;
  while (!platform.shouldQuit() && !Application::quitRequested()) {
    double now = platform.timeSeconds();
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    if (!havePack) {
      GameObject* cube = world.findByName("Cube");
      if (cube) cube->transform.lrot_ = Quaternion::Euler(20.0f, static_cast<Scalar>(frame) * 1.5f, 0);
    }
    renderWorld(world, renderer);
    ++frame;
    if (frame == 1) heap::checkReset();  // first frame includes lazy GU/driver setup (same rule as the desktop player)
#ifdef RENSGA_EXIT_AFTER_FRAMES
    if (frame >= RENSGA_EXIT_AFTER_FRAMES) break;  // screenshot/CI builds (PPSSPPHeadless needs a clean exit)
#endif
    if (frame == 600) heap::logAllocCheck();  // dev builds run on until the editor stops them: log the alloc-check once (like the desktop player)
    if (frame % 60 == 0) RLOG_INFO("game", "rensga: frame %u", static_cast<unsigned>(frame));
  }
#ifdef RENSGA_EXIT_AFTER_FRAMES
  dumpFrameBmp();
#endif
  heap::logAllocCheck();
  RLOG_INFO("game", "rensga: quit after %u frames", static_cast<unsigned>(frame));
  log::flushAll();
  world.shutdown();
  platform.shutdown();
  return 0;
}
