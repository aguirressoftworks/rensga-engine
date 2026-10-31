// F5.14 runtime: the integer-tick clip sampler, palette builder, AnimationSystem stepping and skinHash.
#include <math.h>

#include "fixtures/pack3d_builder.h"
#include "rensga/core/animation.h"
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using rensga::fixtures::Pack3dBuilder;
using rensga::fixtures::PackImage;

namespace {
struct AnimFixture {
  Pack3dBuilder b;
  PackImage img;
  PackView v;
  PackAssets assets;
  Arena arena;
  World world;
  LoadedPack lp;
  GameLoop loop;
  u32 handles[1] = {1};
  AnimFixture() {
    b.build(img);
    v.open(img.data, img.size);
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    world.loadPack(v, &lp);
    assets = v.assets();
    GfxAssets::bind(&v, &assets, handles, 1);
    loop.init(world);  // resets Time to the 1/60 fixed step
  }
  ~AnimFixture() {
    AnimationSystem::shutdown();
    GfxAssets::unbind();
  }
  const PackClip3& clip() const { return assets.clips3[0]; }
  Animation* anim() { return world.findByName("Bar")->GetComponent<Animation>(); }
  SkinnedMeshRenderer* smr() { return world.findByName("Bar")->GetComponent<SkinnedMeshRenderer>(); }
};
const Scalar kS = 0.38268343236508984f;  // sin(22.5 deg)
const Scalar kC = 0.9238795325112867f;   // cos(22.5 deg)
}  // namespace

TEST(anim_sample_clip_exact_values) {
  AnimFixture f;
  const PackJointPose* poses = f.assets.clipPoses(f.clip());
  PackJointPose out[2];
  // fps 30, tickHz 60: tick 0 is frame 0, tick 2 is frame 1, odd ticks are halfway.
  sampleClip(f.clip(), poses, 2, 0, 60, out);
  CHECK(out[1].r[2] == 0 && out[1].r[3] == 1);
  CHECK(out[1].t[1] == 1);
  sampleClip(f.clip(), poses, 2, 2, 60, out);
  CHECK_NEAR(out[1].r[2], kS, 1e-6f);
  CHECK_NEAR(out[1].r[3], kC, 1e-6f);
  // Halfway between identity and 45 degrees = 22.5 degrees about Z, i.e. z = sin(11.25), w = cos(11.25).
  sampleClip(f.clip(), poses, 2, 1, 60, out);
  CHECK_NEAR(out[1].r[2], 0.19509032201612825f, 1e-6f);
  CHECK_NEAR(out[1].r[3], 0.9807852804032304f, 1e-6f);
  CHECK_NEAR(out[1].t[1], 1.0f, 1e-6f);
  CHECK_NEAR(out[1].s, 1.0f, 1e-6f);
  // Loop: tick 3 blends frame 1 -> frame 0 (midpoint again, mirrored), tick 4 is back at frame 0, tick 6 at frame 1.
  sampleClip(f.clip(), poses, 2, 3, 60, out);
  CHECK_NEAR(out[1].r[2], 0.19509032201612825f, 1e-6f);
  sampleClip(f.clip(), poses, 2, 4, 60, out);
  CHECK(out[1].r[2] == 0 && out[1].r[3] == 1);
  sampleClip(f.clip(), poses, 2, 6, 60, out);
  CHECK_NEAR(out[1].r[2], kS, 1e-6f);
  // One-shot clip: holds the last frame.
  PackClip3 once = f.clip();
  once.loop = 0;
  sampleClip(once, poses, 2, 1000, 60, out);
  CHECK_NEAR(out[1].r[2], kS, 1e-6f);
  CHECK_NEAR(out[1].r[3], kC, 1e-6f);
  // A rotation sampled between two poses stays unit length at any tick.
  for (u32 t = 0; t < 12; ++t) {
    sampleClip(f.clip(), poses, 2, t, 60, out);
    Scalar n = out[1].r[0] * out[1].r[0] + out[1].r[1] * out[1].r[1] + out[1].r[2] * out[1].r[2] + out[1].r[3] * out[1].r[3];
    CHECK_NEAR(n, 1.0f, 1e-6f);
  }
}

TEST(anim_sampling_is_a_pure_function_of_the_tick) {
  AnimFixture f;
  const PackJointPose* poses = f.assets.clipPoses(f.clip());
  PackJointPose a[2], b[2];
  sampleClip(f.clip(), poses, 2, 777, 60, a);
  sampleClip(f.clip(), poses, 2, 777, 60, b);
  CHECK(memcmp(a, b, sizeof a) == 0);
}

TEST(anim_build_palette_bind_pose_and_bend) {
  AnimFixture f;
  const PackSkin& skin = f.assets.skins[0];
  const PackJoint* joints = f.assets.skinJoints(skin);
  const PackJointPose* poses = f.assets.clipPoses(f.clip());
  Matrix4 pal[2];
  buildPalette(skin, joints, poses, pal);  // frame 0 = bind pose: every palette matrix is the identity
  for (int j = 0; j < 2; ++j)
    for (int k = 0; k < 16; ++k) CHECK_NEAR(pal[j].m[k], (k % 5 == 0) ? 1.0f : 0.0f, 1e-6f);
  buildPalette(skin, joints, poses + 2, pal);  // frame 1: joint 1 bent 45 degrees about Z around (0,1,0)
  for (int k = 0; k < 16; ++k) CHECK_NEAR(pal[0].m[k], (k % 5 == 0) ? 1.0f : 0.0f, 1e-6f);
  Vector3 p = pal[1].transformPoint(Vector3(0, 2, 0));  // one unit above the pivot
  const Scalar s45 = 0.7071067811865476f;
  CHECK_NEAR(p.x, -s45, 1e-5f);
  CHECK_NEAR(p.y, 1.0f + s45, 1e-5f);
  CHECK_NEAR(p.z, 0.0f, 1e-6f);
  Vector3 q = pal[1].transformPoint(Vector3(0, 1, 0));  // the pivot does not move
  CHECK_NEAR(q.x, 0.0f, 1e-5f);
  CHECK_NEAR(q.y, 1.0f, 1e-5f);
}

TEST(anim_system_steps_integer_ticks_and_hash_is_stable) {
  u32 hashes[2][8] = {};
  for (int run = 0; run < 2; ++run) {
    AnimFixture f;
    CHECK_EQ(AnimationSystem::skinHash(), 0u);  // nothing animated yet
    CHECK(f.anim()->paletteSlot == kInvalidIndex);
    for (int s = 0; s < 8; ++s) {
      AnimationSystem::fixedStep(f.world);
      hashes[run][s] = AnimationSystem::skinHash();
      CHECK_EQ(f.anim()->tick, static_cast<u32>(s));  // playAutomatically: first step shows tick 0, then one tick per step
    }
    CHECK(f.anim()->isPlaying());
    u32 count = 0;
    const Matrix4* pal = AnimationSystem::palette(f.world, *f.smr(), &count);
    CHECK(pal != nullptr);
    CHECK_EQ(count, 2u);
    // tick 7 = frame 3.5 of a 2 frame loop: halfway between frame 1 and frame 0.
    Vector3 p = pal[1].transformPoint(Vector3(0, 2, 0));
    CHECK(p.x < -0.2f && p.x > -0.7f);
  }
  CHECK(hashes[0][0] != hashes[0][1]);
  CHECK(hashes[0][1] != hashes[0][2]);
  for (int s = 0; s < 8; ++s) CHECK_EQ(hashes[0][s], hashes[1][s]);  // same data, same bits
  // The loop is 2 frames = 4 ticks: ticks 0 and 4 are the same pose.
  CHECK_EQ(hashes[0][0], hashes[0][4]);
}

TEST(anim_play_stop_wrap_and_speed) {
  AnimFixture f;
  Animation* an = f.anim();
  an->playAutomatically = false;
  AnimationSystem::fixedStep(f.world);
  CHECK(!an->isPlaying());
  const u32 h0 = AnimationSystem::skinHash();
  AnimationSystem::fixedStep(f.world);
  AnimationSystem::fixedStep(f.world);
  CHECK_EQ(AnimationSystem::skinHash(), h0);  // not playing: the pose holds
  an->Play();
  CHECK(an->isPlaying());
  AnimationSystem::fixedStep(f.world);
  CHECK_EQ(an->tick, 0u);
  AnimationSystem::fixedStep(f.world);
  AnimationSystem::fixedStep(f.world);
  CHECK_EQ(an->tick, 2u);
  an->Stop();
  CHECK(!an->isPlaying());
  const u32 h2 = AnimationSystem::skinHash();
  AnimationSystem::fixedStep(f.world);
  CHECK_EQ(AnimationSystem::skinHash(), h2);
  // Speed 2: two ticks per step. WrapMode 1 (Once): finishes and holds the last frame.
  an->speed = 2.0f;
  an->wrapMode = 1;
  an->Play();
  for (int i = 0; i < 6; ++i) AnimationSystem::fixedStep(f.world);
  CHECK(!an->isPlaying());
  u32 count = 0;
  const Matrix4* pal = AnimationSystem::palette(f.world, *f.smr(), &count);
  Vector3 p = pal[1].transformPoint(Vector3(0, 2, 0));
  CHECK_NEAR(p.x, -0.7071067811865476f, 1e-5f);
  // Speed 0.5: one tick every two steps (integer carry).
  an->speed = 0.5f;
  an->wrapMode = 2;
  an->Play();
  AnimationSystem::fixedStep(f.world);  // shows tick 0
  for (int i = 0; i < 4; ++i) AnimationSystem::fixedStep(f.world);
  CHECK_EQ(an->tick, 2u);
}

TEST(anim_loop_runs_it_each_fixed_step_and_matches_a_manual_run) {
  u32 viaLoop = 0, manual = 0;
  {
    AnimFixture f;
    VirtualGamepad pad;
    for (int i = 0; i < 30; ++i) f.loop.frame(1.0f / 60.0f, pad);
    viaLoop = AnimationSystem::skinHash();
    CHECK_EQ(AnimationSystem::stepCount(), 30u);
  }
  {
    AnimFixture f;
    for (int i = 0; i < 30; ++i) AnimationSystem::fixedStep(f.world);
    manual = AnimationSystem::skinHash();
  }
  CHECK(viaLoop != 0);
  CHECK_EQ(viaLoop, manual);
}

TEST(anim_render_world_passes_the_palette) {
  AnimFixture f;
  NullRenderer r;
  r.init(480, 272);
  r.setMeshes(f.lp.meshes);
  AnimationSystem::fixedStep(f.world);
  AnimationSystem::fixedStep(f.world);
  AnimationSystem::fixedStep(f.world);
  renderWorld(f.world, r);
  CHECK_EQ(r.exDraws, 2u);
  CHECK_EQ(r.lastExMesh, 1u);  // the skinned bar draws last
  CHECK(r.lastParams.palette != nullptr);
  CHECK_EQ(r.lastParams.paletteCount, 2u);
}
