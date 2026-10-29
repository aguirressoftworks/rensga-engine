// PSP entry point: boot -> load host0:/game.rpak (or a built-in test cube) -> fixed-timestep loop -> render.
#include <pspkernel.h>
#include <pspdebug.h>

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

int main() {
  pspInstallLogSinks();
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
  if (bytes && view.open(bytes.data, bytes.size) && world.loadPack(view, &loaded)) {
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
    cam->transform.localPosition = Vector3(0, 1.2f, 3.5f);
    cam->transform.localRotation = Quaternion::AngleAxis(-15.0f, Vector3(1, 0, 0));
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
  while (!platform.shouldQuit()) {
    double now = platform.timeSeconds();
    platform.pollInput(pad);
    loop.frame(static_cast<Scalar>(now - last), pad);
    last = now;
    if (!havePack) {
      GameObject* cube = world.findByName("Cube");
      if (cube) cube->transform.localRotation = Quaternion::Euler(20.0f, static_cast<Scalar>(frame) * 1.5f, 0);
    }
    renderWorld(world, renderer);
    ++frame;
    if (frame % 60 == 0) RLOG_INFO("game", "rensga: frame %u", frame);
  }
  RLOG_INFO("game", "rensga: quit after %u frames", frame);
  log::flushAll();
  world.shutdown();
  platform.shutdown();
  return 0;
}
