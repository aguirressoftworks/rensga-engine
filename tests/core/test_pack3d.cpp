// F5 contract tests: the shared-3D pack sections, the new components, IRenderer defaults, appended-field compatibility.
#include <string.h>

#include "fixtures/pack3d_builder.h"
#include "rensga/core/animation.h"
#include "rensga/core/app_events.h"
#include "rensga/core/loop.h"
#include "rensga/core/log.h"
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/cull.h"
#include "rensga/core/feature.h"
#include "rensga/core/lighting.h"
#include "rensga/core/save.h"
#include "rensga/core/system.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using rensga::fixtures::Pack3dBuilder;
using rensga::fixtures::PackImage;

TEST(pack3d_record_sizes) {
  CHECK_EQ(sizeof(PackMeshLit), 64u);
  CHECK_EQ(sizeof(PackVertexLit), 36u);
  CHECK_EQ(sizeof(PackVertexSkin), 44u);
  CHECK_EQ(sizeof(PackVertexLitC16), 20u);
  CHECK_EQ(sizeof(PackVertexSkinC16), 28u);
  CHECK_EQ(sizeof(PackBoneBatch), 32u);
  CHECK_EQ(sizeof(PackMaterial), 32u);
  CHECK_EQ(sizeof(PackSkin), 32u);
  CHECK_EQ(sizeof(PackJoint), 112u);
  CHECK_EQ(sizeof(PackClip3), 48u);
  CHECK_EQ(sizeof(PackJointPose), 32u);
  CHECK_EQ(sizeof(PackConsoleTargetExt), 32u);
  CHECK_EQ(packVertexStride(2), 36u);
  CHECK_EQ(packVertexStride(5), 28u);
  CHECK_EQ(packVertexStride(0), 0u);
}

TEST(pack3d_open_and_tables) {
  Pack3dBuilder b;
  PackImage img;
  b.build(img);
  PackView v;
  CHECK(v.open(img.data, img.size, "pack3d"));
  PackAssets a = v.assets();
  CHECK_EQ(a.materialCount, 1u);
  CHECK_EQ(a.skinCount, 1u);
  CHECK_EQ(a.clip3Count, 1u);
  CHECK_EQ(a.skins[0].jointCount, 2u);
  CHECK_EQ(a.skinJoints(a.skins[0])[1].parent, 0);
  CHECK_NEAR(a.skinJoints(a.skins[0])[1].invBind[13], -1.0f, 1e-6f);
  CHECK(a.findClip3(Pack3dBuilder::clipGuid()) == &a.clips3[0]);
  CHECK_EQ(a.clips3[0].fps, 30u);
  CHECK_NEAR(a.clipPoses(a.clips3[0])[3].r[3], 0.92387953f, 1e-5f);
  CHECK(a.findMaterial(Pack3dBuilder::materialGuid()) == &a.materials[0]);
  const PackConsoleTargetExt* e = v.consoleTargetExt();
  CHECK(e != nullptr);
  CHECK_EQ(e->maxLightsPerObject, 4u);
  CHECK_EQ(e->maxBonesPerDraw, 8u);
  CHECK_EQ(e->features & kTargetFeatSkinning, kTargetFeatSkinning);
  // MESH / VERT stay readable by an old renderer: the non-baked meshes have zero baked vertices.
  u32 meshCount = 0;
  const PackMeshRec* mesh = v.records<PackMeshRec>(pack_section::MESH, &meshCount);
  CHECK_EQ(meshCount, 2u);
  CHECK_EQ(mesh[0].vertexCount, 0u);
  CHECK_EQ(mesh[1].vertexCount, 0u);
}

struct World3dFixture {
  Arena arena;
  World world;
  World3dFixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
  }
};

TEST(pack3d_world_load_and_mesh_table) {
  Pack3dBuilder b;
  PackImage img;
  b.build(img);
  PackView v;
  CHECK(v.open(img.data, img.size));
  World3dFixture f;
  LoadedPack lp;
  CHECK(f.world.loadPack(v, &lp));
  CHECK_EQ(f.world.objectCount(), 4u);
  CHECK(lp.meshes.lit != nullptr);
  CHECK(lp.meshes.vtx3 != nullptr);
  CHECK_EQ(lp.meshes.batchCount, 1u);
  CHECK_EQ(lp.meshes.lit[0].vertexFormat, static_cast<u32>(PackVertexFormat::LitF));
  CHECK_EQ(lp.meshes.lit[1].vertexFormat, static_cast<u32>(PackVertexFormat::SkinF));
  CHECK(lp.meshes.lit[1].isSkinned());
  CHECK_EQ(lp.meshes.lit[1].vertexByteOffset % 16, 0u);
  CHECK(lp.meshes.vtx3Size >= lp.meshes.lit[1].vertexByteOffset + 72u * sizeof(PackVertexSkin));
  // Vertex weights sum to 128.
  const PackVertexSkin* sv = reinterpret_cast<const PackVertexSkin*>(lp.meshes.vtx3 + lp.meshes.lit[1].vertexByteOffset);
  for (u32 i = 0; i < 72; ++i) {
    u32 sum = 0;
    for (int k = 0; k < 8; ++k) sum += sv[i].w[k];
    CHECK_EQ(sum, 128u);
  }
  GameObject* cube = f.world.findByName("Cube");
  GameObject* bar = f.world.findByName("Bar");
  CHECK(cube && bar);
  MeshRenderer* mr = cube->GetComponent<MeshRenderer>();
  CHECK(mr && mr->meshIndex == 0 && mr->material == Pack3dBuilder::materialGuid());
  SkinnedMeshRenderer* sm = bar->GetComponent<SkinnedMeshRenderer>();
  CHECK(sm && sm->meshIndex == 1);
  Animation* an = bar->GetComponent<Animation>();
  CHECK(an && an->clip == Pack3dBuilder::clipGuid() && an->playAutomatically && an->speed == 1.0f);
  Light* li = f.world.findByName("Sun")->GetComponent<Light>();
  CHECK(li && li->type == 0 && li->intensity == 1.0f);
}

TEST(pack3d_optional_sections_absent) {
  Pack3dBuilder b;
  b.withLit = false;
  b.withCtg2 = false;
  PackImage img;
  b.build(img);
  PackView v;
  // Components of the new types still resolve; the lit tables are simply empty.
  CHECK(v.open(img.data, img.size));
  CHECK(v.consoleTargetExt() == nullptr);
  CHECK_EQ(v.assets().skinCount, 0u);
  World3dFixture f;
  LoadedPack lp;
  CHECK(f.world.loadPack(v, &lp));
  CHECK(lp.meshes.lit == nullptr);
  CHECK(lp.meshes.vtx3 == nullptr);
  CHECK_EQ(lp.meshes.batchCount, 0u);
}

TEST(pack3d_c16_cube_opens) {
  Pack3dBuilder b;
  b.c16Cube = true;
  PackImage img;
  b.build(img);
  PackView v;
  CHECK(v.open(img.data, img.size));
}

static bool corruptAndOpen(const char* what, void (*mutate)(PackImage&, PackView&)) {
  Pack3dBuilder b;
  PackImage img;
  b.build(img);
  PackView good;
  if (!good.open(img.data, img.size)) return false;
  mutate(img, good);
  PackView v;
  const bool ok = v.open(img.data, img.size, what);
  return ok;
}

TEST(pack3d_validation_rejects_bad_ranges) {
  // Silence the expected error lines.
  CallbackSink quiet([](const LogRecord&, void*) {}, nullptr);
  log::addSink(&quiet);
  CHECK(!corruptAndOpen("batch", [](PackImage& img, PackView& v) {
    PackBoneBatch* bt = const_cast<PackBoneBatch*>(v.records<PackBoneBatch>(pack_section::BTCH));
    bt[0].vertexCount = 9999;
    (void)img;
  }));
  CHECK(!corruptAndOpen("vtx3", [](PackImage& img, PackView& v) {
    PackMeshLit* m = const_cast<PackMeshLit*>(v.records<PackMeshLit>(pack_section::MSHL));
    m[1].vertexCount = 9999 * 3;
    (void)img;
  }));
  CHECK(!corruptAndOpen("material", [](PackImage& img, PackView& v) {
    PackMeshLit* m = const_cast<PackMeshLit*>(v.records<PackMeshLit>(pack_section::MSHL));
    m[0].material = 7;
    (void)img;
  }));
  CHECK(!corruptAndOpen("skin", [](PackImage& img, PackView& v) {
    PackMeshLit* m = const_cast<PackMeshLit*>(v.records<PackMeshLit>(pack_section::MSHL));
    m[1].skin = kInvalidIndex;
    (void)img;
  }));
  CHECK(!corruptAndOpen("format", [](PackImage& img, PackView& v) {
    PackMeshLit* m = const_cast<PackMeshLit*>(v.records<PackMeshLit>(pack_section::MSHL));
    m[0].vertexFormat = 9;
    (void)img;
  }));
  CHECK(!corruptAndOpen("clip", [](PackImage& img, PackView& v) {
    PackClip3* c = const_cast<PackClip3*>(v.records<PackClip3>(pack_section::CLP3));
    c[0].frameCount = 100000;
    (void)img;
  }));
  CHECK(!corruptAndOpen("parent", [](PackImage& img, PackView& v) {
    PackSkin* s = const_cast<PackSkin*>(v.records<PackSkin>(pack_section::SKIN));
    PackJoint* j = reinterpret_cast<PackJoint*>(img.data + v.find(pack_section::BLOB)->offset + s[0].jointsOffset);
    j[0].parent = 1;  // parent after child
  }));
  log::removeSink(&quiet);
}

TEST(renderer_defaults_and_null_counts) {
  NullRenderer r;
  r.init(480, 272);
  r.beginFrame(Color32(0, 0, 0, 255));
  RenderLight l[2];
  l[1].type = 1;
  l[1].range = 5;
  r.setFrameLights(l, 2, Color32(10, 20, 30, 255));
  CHECK_EQ(r.lightCount, 2u);
  CHECK_EQ(r.lights[1].type, 1);
  CHECK(r.ambient == Color32(10, 20, 30, 255));
  r.setFog(true, Color32(1, 2, 3, 255), 5, 50);
  CHECK(r.fogOn && r.fogEnd == 50.0f);
  DrawParams p;
  p.texture = 3;
  p.matFlags = kMatUnlit;
  p.lightCount = 2;
  p.lights[0] = 1;
  Matrix4 w;
  r.drawMeshEx(4, w, 77, p);
  r.drawMesh(5, w, 78);
  CHECK_EQ(r.exDraws, 1u);
  CHECK_EQ(r.draws, 2u);  // drawMeshEx counts as a draw
  CHECK_EQ(r.lastExMesh, 4u);
  CHECK_EQ(r.lastParams.texture, 3u);
  CHECK_EQ(r.lastParams.lights[0], 1);
  // A renderer that only implements drawMesh gets drawMeshEx forwarded.
  struct Plain : IRenderer {
    u32 hits = 0, lastMesh = 0;
    bool init(u32, u32) override { return true; }
    void setMeshes(const MeshTable&) override {}
    void beginFrame(Color32) override {}
    void setCamera(const Matrix4&, const Matrix4&) override {}
    void drawMesh(u32 m, const Matrix4&, u32) override { ++hits; lastMesh = m; }
    void drawOverlayQuad(i32, i32, i32, i32, Color32) override {}
    void endFrame() override {}
    u32 width() const override { return 1; }
    u32 height() const override { return 1; }
  } plain;
  plain.drawMeshEx(9, w, 0, p);
  CHECK_EQ(plain.hits, 1u);
  CHECK_EQ(plain.lastMesh, 9u);
}

TEST(platform_seam_defaults) {
  NullPlatform p;
  CHECK(!p.setCpuClock(333));
  CHECK_EQ(p.language(), 0xFFFFFFFFu);
  CHECK(!p.confirmButtonEast());
  CHECK_EQ(p.batteryPercent(), -1);
  CHECK_EQ(p.suspendState(), 0);
  CHECK(!p.beginTextInput("a", "t", 8));
  char out[8];
  CHECK_EQ(p.pollTextInput(out, 8), 3);
  CHECK_EQ(p.saveDialog("s", nullptr, 0, false), -1);
}

struct FakePlatform : NullPlatform {
  i32 pending = 0;
  u32 osk = 0;
  bool setCpuClock(i32 mhz) override { return mhz == 333; }
  u32 language() override { return static_cast<u32>(SystemLanguage::French); }
  bool confirmButtonEast() override { return true; }
  i32 batteryPercent() override { return 55; }
  i32 batteryStatus() override { return 2; }
  i32 suspendState() override { const i32 s = pending; pending = 0; return s; }
  bool beginTextInput(const char*, const char*, u32) override { osk = 1; return true; }
  i32 pollTextInput(char* out, u32 cap) override {
    if (osk++ < 3) return 0;
    strncpy(out, "Ada", cap);
    return 1;
  }
  u64 systemMemoryBytes() override { return 32ull << 20; }
  u64 usedSystemMemoryBytes() override { return 5ull << 20; }
};

struct PauseProbe : Behaviour {
  REFLECT(PauseProbe)
  i32 calls = 0;
  bool last = false;
  void OnApplicationPause(bool p) override { ++calls; last = p; }
};

TEST(api_system_services_through_platform) {
  FakePlatform fp;
  system_services::bind(&fp);
  CHECK(Application::SetCpuClock(333));
  CHECK(!Application::SetCpuClock(111));
  CHECK(Application::systemLanguage() == SystemLanguage::French);
  CHECK(Application::confirmButtonEast());
  CHECK_NEAR(SystemInfo::batteryLevel(), 0.55f, 1e-5f);
  CHECK(SystemInfo::batteryStatus() == SystemInfo::BatteryStatus::Discharging);
  CHECK_EQ(SystemInfo::systemMemorySize(), 32);
  CHECK_EQ(SystemInfo::usedMemory(), 5u << 20);
  CHECK_EQ(SystemInfo::graphicsMemorySize(), 0);
  CHECK(TouchScreenKeyboard::Open("Bob", "Name", 8));
  CHECK(TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::Visible);
  CHECK(strcmp(TouchScreenKeyboard::text(), "Bob") == 0);
  TouchScreenKeyboard::Status s = TouchScreenKeyboard::status();
  for (int i = 0; i < 4 && s != TouchScreenKeyboard::Status::Done; ++i) s = TouchScreenKeyboard::status();
  CHECK(s == TouchScreenKeyboard::Status::Done);
  CHECK(strcmp(TouchScreenKeyboard::text(), "Ada") == 0);
  // OnApplicationPause through pumpApplicationEvents.
  World3dFixture f;
  GameObject* go = f.world.createObject("p");
  PauseProbe* pr = go->AddComponent<PauseProbe>();
  GameLoop loop;
  loop.init(f.world);
  VirtualGamepad pad;
  loop.frame(1.0f / 60.0f, pad);  // Start
  fp.pending = 1;
  pumpApplicationEvents(f.world);
  CHECK_EQ(pr->calls, 1);
  CHECK(pr->last);
  fp.pending = -1;
  pumpApplicationEvents(f.world);
  CHECK_EQ(pr->calls, 2);
  CHECK(!pr->last);
  system_services::bind(nullptr);
  CHECK(!Application::SetCpuClock(333));
  CHECK(Application::systemLanguage() == SystemLanguage::Unknown);
  CHECK_EQ(SystemInfo::batteryLevel(), -1.0f);
}

TEST(stubs_link_and_defaults) {
  CHECK_EQ(resolveMaxLights(nullptr, RuntimePlatform::Psp), 4u);
  CHECK_EQ(resolveMaxLights(nullptr, RuntimePlatform::LinuxPlayer), 8u);
  PackConsoleTargetExt e = {};
  e.maxLightsPerObject = 3;
  CHECK_EQ(resolveMaxLights(&e, RuntimePlatform::LinuxPlayer), 3u);
  Frustum fr = frustumFromViewProj(Matrix4());
  CHECK(sphereInFrustum(fr, Vector3(0, 0, 0), 1.0f));
  CHECK_EQ(AnimationSystem::skinHash(), 0u);
  CHECK_EQ(Save::mode() == SaveMode::Auto ? 1 : 0, 1);
}

// Component fields are only appended: blobs written before a field existed still load.
TEST(appended_fields_load_from_older_blobs) {
  const TypeInfo& mt = *MeshRenderer::staticTypeInfo();
  MeshRenderer full;
  full.mesh = Pack3dBuilder::cubeMeshGuid();
  full.material = Pack3dBuilder::materialGuid();
  u8 blob[64];
  writeBlob(mt, &full, blob);
  const u32 oldSize = blobSize(mt) - 16;  // before `material` existed
  MeshRenderer old;
  CHECK(readBlob(mt, &old, blob, oldSize));
  CHECK(old.mesh == Pack3dBuilder::cubeMeshGuid());
  CHECK(old.material.isNull());
  const TypeInfo& at = *AudioSource::staticTypeInfo();
  AudioSource as;
  as.volume = 0.25f;
  as.priority = 10;
  u8 ab[96];
  writeBlob(at, &as, ab);
  AudioSource oldAs;
  CHECK(readBlob(at, &oldAs, ab, blobSize(at) - 6 - 4));  // before `priority` existed (and before the F6 playOnAwake / mute / outputGroup)
  CHECK_EQ(oldAs.volume, 0.25f);
  CHECK_EQ(oldAs.priority, 128);
  AudioSource f5As;
  CHECK(readBlob(at, &f5As, ab, blobSize(at) - 6));  // an F5 blob: priority is read, the F6 fields keep their defaults
  CHECK_EQ(f5As.priority, 10);
  CHECK(!f5As.playOnAwake && !f5As.mute && f5As.outputGroup == 0);
}

TEST(feature_macro_present_without_header) {
  // This test binary sees no console_target.gen.h, so every feature is present and the gated calls compile.
  CHECK_EQ(RENSGA_FEATURE_PRESENT_OSK, 1);
  CHECK_EQ(RENSGA_FEATURE_PRESENT_CPUCLOCK, 1);
}
