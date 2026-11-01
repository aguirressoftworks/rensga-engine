// F6.03 (B1): Animator controllers: parameters, conditions, any-state transitions, exit time, triggers, cross-fades, Play/CrossFade,
// speed parameter, looping, events on the fixed step, determinism of the state hash, no heap use.
#include "anim_pack.h"
#include "rensga/core/animation.h"
#include "rensga/core/animator_system.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/prop_clip.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/time.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using namespace animpack;

namespace {
struct StepProbe : Behaviour {
  REFLECT(StepProbe)
  SERIALIZE_FIELD(i32, steps, 0)
  METHOD(Step)
  void Step() { ++steps; }
};

CurveDef posX(const char* path, Scalar a, Scalar b, u32 len) {
  CurveDef c;
  c.path = path;
  c.component = "Transform"; c.field = "localPosition"; c.part = 0; c.keys = {{0, a}, {len, b}};
  return c;
}
CurveDef srcX(std::vector<KeyDef> keys) {
  CurveDef c;
  c.component = "SpriteRenderer"; c.field = "srcX"; c.type = FieldType::I32; c.interp = kPropInterpStep; c.keys = std::move(keys);
  return c;
}

struct AnimWorld {
  Builder b;
  u8* mem = nullptr;
  size_t size = 0;
  PackView view;
  PackAssets assets;
  Arena arena;
  World world;
  LoadedPack lp;
  GameLoop loop;
  VirtualGamepad pad;
  Guid ctl;
  void open(const char* objName = "Player") {
    mem = b.build(&size);
    CHECK(view.open(mem, size));
    arena.create(1 << 21);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    CHECK(world.loadPack(view, &lp));
    assets = view.assets();
    GfxAssets::bind(&view, &assets, nullptr, 0);
    loop.init(world);
    (void)objName;
  }
  ~AnimWorld() {
    AnimationSystem::shutdown();
    GfxAssets::unbind();
    world.shutdown();
    free(mem);
  }
  void frames(int n) { for (int i = 0; i < n; ++i) loop.frame(1.0f / 60.0f, pad); }
  GameObject* go(const char* n = "Player") { return world.findByName(n); }
  Animator* anim() { return go()->GetComponent<Animator>(); }
  void addPlayer(Guid c) {
    u32 o = b.addObject("Player");
    b.addComponent(o, *SpriteRenderer::staticTypeInfo());
    b.addComponent(o, *Animator::staticTypeInfo(), [c](void* p) { static_cast<Animator*>(p)->controller = c; });
  }
};
const Scalar kOne = 1.0f;
}  // namespace

TEST(animator_state_machine_params_conditions_and_triggers) {
  AnimWorld w;
  Guid idle = w.b.addClip(1, "Idle", 60, 60, true, {srcX({{0, 0}})});
  Guid run = w.b.addClip(2, "Run", 60, 60, true, {srcX({{0, 100}})});
  Guid jump = w.b.addClip(3, "Jump", 60, 30, false, {srcX({{0, 200}})});
  // params: 0 speed (float), 1 jump (trigger), 2 grounded (bool, default true)
  Guid c = w.b.addController(10, "Player", {{"speed", PackAnimParamType::Float, bitsOf(0)}, {"jump", PackAnimParamType::Trigger, 0}, {"grounded", PackAnimParamType::Bool, 1}},
                             {{"Idle", PackAnimStateKind::PropClip, idle}, {"Run", PackAnimStateKind::PropClip, run}, {"Jump", PackAnimStateKind::PropClip, jump, false}},
                             {{0, 1, false, 1, 0, {{0, PackAnimCondMode::Greater, bitsOf(0.1f)}}},
                              {1, 0, false, 1, 0, {{0, PackAnimCondMode::Less, bitsOf(0.1f)}}},
                              {-1, 2, false, 1, 0, {{1, PackAnimCondMode::If}}},
                              {2, 0, true, 1.0f, 0, {{2, PackAnimCondMode::If}}}});
  w.addPlayer(c);
  w.open();
  Animator* a = w.anim();
  SpriteRenderer* sr = w.go()->GetComponent<SpriteRenderer>();
  w.frames(2);
  CHECK(a->GetCurrentStateInfo().IsName("Idle"));
  CHECK_EQ(sr->srcX, 0);
  a->SetFloat("speed", 0.5f);
  CHECK_NEAR(a->GetFloat("speed"), 0.5f, 1e-6f);
  w.frames(1);
  CHECK(a->GetCurrentStateInfo().IsName("Run"));
  CHECK_EQ(sr->srcX, 100);
  a->SetTrigger("jump");  // any-state transition
  w.frames(1);
  CHECK(a->GetCurrentStateInfo().IsName("Jump"));
  CHECK_EQ(sr->srcX, 200);
  CHECK(!a->GetBool("jump"));  // consumed by the transition
  a->SetFloat("speed", 0.0f);  // back to standing still, so Idle is where the exit-time transition ends
  // Jump has an exit-time transition (after the whole clip) when grounded: 30 ticks of a non-looping clip.
  w.frames(20);
  CHECK(a->GetCurrentStateInfo().IsName("Jump"));
  w.frames(15);
  CHECK(a->GetCurrentStateInfo().IsName("Idle"));
  a->SetFloat("speed", 0.5f);
  w.frames(1);
  CHECK(a->GetCurrentStateInfo().IsName("Run"));
  (void)kOne;
}

TEST(animator_crossfade_blends_transform_values_in_integer_ticks) {
  AnimWorld w;
  Guid a0 = w.b.addClip(1, "A", 60, 120, true, {posX("", 0, 0, 120)});
  Guid b0 = w.b.addClip(2, "B", 60, 120, true, {posX("", 10, 10, 120)});
  Guid c = w.b.addController(10, "C", {{"go", PackAnimParamType::Bool, 0}},
                             {{"A", PackAnimStateKind::PropClip, a0}, {"B", PackAnimStateKind::PropClip, b0}},
                             {{0, 1, false, 1, 10, {{0, PackAnimCondMode::If}}}});
  w.addPlayer(c);
  w.open();
  w.frames(3);
  CHECK_NEAR(w.go()->transform.localPosition().x, 0.0f, 1e-6f);
  w.anim()->SetBool("go", true);
  w.frames(5);  // transition starts in the first of these steps: elapsed = 5 of 10
  CHECK_NEAR(w.go()->transform.localPosition().x, 5.0f, 1e-4f);
  AnimatorSystem::DebugInfo d;
  CHECK(AnimatorSystem::debugInfo(*w.anim(), d));
  CHECK(d.next == 1u && d.current == 0u);
  CHECK_NEAR(d.blend, 0.5f, 1e-6f);
  w.frames(5);
  CHECK_NEAR(w.go()->transform.localPosition().x, 10.0f, 1e-4f);
  CHECK(w.anim()->GetCurrentStateInfo().IsName("B"));
  CHECK(AnimatorSystem::debugInfo(*w.anim(), d) && d.next == kInvalidIndex);
}

TEST(animator_play_crossfade_and_state_info) {
  AnimWorld w;
  Guid a0 = w.b.addClip(1, "A", 30, 30, true, {srcX({{0, 1}})});  // 30 fps, 30 ticks = 1 second
  Guid b0 = w.b.addClip(2, "B", 30, 30, false, {srcX({{0, 2}})});
  Guid c = w.b.addController(10, "C", {}, {{"A", PackAnimStateKind::PropClip, a0}, {"B", PackAnimStateKind::PropClip, b0, false}}, {});
  w.addPlayer(c);
  w.open();
  Animator* a = w.anim();
  w.frames(31);  // first step shows time 0, then 30 steps at 0.5 clip ticks each = 15 ticks = half the clip
  AnimatorStateInfo i = a->GetCurrentStateInfo();
  CHECK(i.IsName("A"));
  CHECK_NEAR(i.normalizedTime, 0.5f, 1e-5f);
  CHECK_NEAR(i.length, 1.0f, 1e-6f);
  CHECK(!a->Play("Nope"));
  CHECK(a->Play("B"));
  w.frames(1);
  CHECK(a->GetCurrentStateInfo().IsName("B"));
  CHECK_EQ(w.go()->GetComponent<SpriteRenderer>()->srcX, 2);
  CHECK_NEAR(a->GetCurrentStateInfo().normalizedTime, 0.0f, 1e-6f);  // a fresh state shows time 0 first
  w.frames(60 + 5);
  CHECK_NEAR(a->GetCurrentStateInfo().normalizedTime, 1.0f, 1e-6f);  // one-shot holds at 1
  CHECK(a->CrossFade("A", 0.1f));                                     // 0.1 s = 6 fixed steps
  w.frames(3);
  AnimatorSystem::DebugInfo d;
  CHECK(AnimatorSystem::debugInfo(*a, d) && d.next == 0u && d.blend > 0.4f && d.blend < 0.6f);
  w.frames(4);
  CHECK(AnimatorSystem::debugInfo(*a, d) && d.next == kInvalidIndex && d.current == 0u);
}

TEST(animator_speed_parameter_and_animator_speed_scale_time) {
  AnimWorld w;
  Guid a0 = w.b.addClip(1, "A", 60, 600, true, {posX("", 0, 600, 600)});  // x == clip tick
  StateDef st{"A", PackAnimStateKind::PropClip, a0};
  st.speedParam = 0;
  Guid c = w.b.addController(10, "C", {{"mult", PackAnimParamType::Float, bitsOf(2.0f)}}, {st}, {});
  w.addPlayer(c);
  w.open();
  w.frames(11);  // one step to show time 0, then 10 steps at speed 2
  CHECK_NEAR(w.go()->transform.localPosition().x, 20.0f, 1e-3f);
  w.anim()->speed = 0.5f;
  w.frames(10);  // 10 steps at 2 * 0.5
  CHECK_NEAR(w.go()->transform.localPosition().x, 30.0f, 1e-3f);
  w.anim()->speed = 0;
  w.frames(10);
  CHECK_NEAR(w.go()->transform.localPosition().x, 30.0f, 1e-3f);
}

TEST(animator_events_fire_once_per_loop_on_the_fixed_step) {
  AnimWorld w;
  Guid a0 = w.b.addClip(1, "Run", 60, 20, true, {}, {{5, "Step"}, {15, "Step"}});
  Guid c = w.b.addController(10, "C", {}, {{"Run", PackAnimStateKind::PropClip, a0}}, {});
  u32 o = w.b.addObject("Player");
  w.b.addComponent(o, *StepProbe::staticTypeInfo());
  w.b.addComponent(o, *Animator::staticTypeInfo(), [c](void* p) { static_cast<Animator*>(p)->controller = c; });
  w.open();
  StepProbe* p = w.go()->GetComponent<StepProbe>();
  w.frames(5);   // times 0..4
  CHECK_EQ(p->steps, 0);
  w.frames(1);   // time 5
  CHECK_EQ(p->steps, 1);
  w.frames(10);  // time 15
  CHECK_EQ(p->steps, 2);
  w.frames(10);  // wraps at 20, time 5 of the next loop
  CHECK_EQ(p->steps, 3);
  w.frames(20);  // a whole loop: two more
  CHECK_EQ(p->steps, 5);
}

TEST(animator_state_hash_is_reproducible_and_changes_with_parameters) {
  u32 hashes[2][40];
  for (int run = 0; run < 2; ++run) {
    AnimWorld w;
    Guid a0 = w.b.addClip(1, "A", 30, 60, true, {posX("", 0, 5, 60)});
    Guid b0 = w.b.addClip(2, "B", 60, 60, true, {posX("", 5, 0, 60)});
    Guid c = w.b.addController(10, "C", {{"go", PackAnimParamType::Bool, 0}, {"f", PackAnimParamType::Float, bitsOf(0.25f)}},
                               {{"A", PackAnimStateKind::PropClip, a0}, {"B", PackAnimStateKind::PropClip, b0}},
                               {{0, 1, false, 1, 7, {{0, PackAnimCondMode::If}}}, {1, 0, true, 0.5f, 0, {}}});
    w.addPlayer(c);
    w.open();
    for (int i = 0; i < 40; ++i) {
      if (i == 10) w.anim()->SetBool("go", true);
      w.frames(1);
      hashes[run][i] = AnimatorSystem::stateHash();
    }
  }
  for (int i = 0; i < 40; ++i) CHECK_EQ(hashes[0][i], hashes[1][i]);
  CHECK(hashes[0][5] != hashes[0][12]);
}

TEST(animator_without_a_controller_or_pack_does_nothing) {
  AnimWorld w;
  w.addPlayer(Guid());
  w.open();
  w.frames(3);
  w.anim()->SetFloat("x", 1);
  CHECK(!w.anim()->Play("A"));
  CHECK(w.anim()->GetCurrentStateInfo().nameHash == 0u);
  CHECK_NEAR(w.anim()->GetFloat("x"), 0.0f, 1e-9f);
}

TEST(animator_event_ticks_are_exact_when_fps_does_not_divide_the_step_rate) {
  AnimWorld w;
  Guid a0 = w.b.addClip(1, "Run", 12, 4, true, {}, {{0, "Step"}, {2, "Step"}});  // 12 fps at 60 Hz: 5 steps per tick
  Guid c = w.b.addController(10, "C", {}, {{"Run", PackAnimStateKind::PropClip, a0}}, {});
  u32 o = w.b.addObject("Player");
  w.b.addComponent(o, *StepProbe::staticTypeInfo());
  w.b.addComponent(o, *Animator::staticTypeInfo(), [c](void* p) { static_cast<Animator*>(p)->controller = c; });
  w.open();
  StepProbe* p = w.go()->GetComponent<StepProbe>();
  w.frames(1);   // entering the state shows time 0: the tick 0 event
  CHECK_EQ(p->steps, 1);
  w.frames(9);   // time 9 steps = 1.8 ticks
  CHECK_EQ(p->steps, 1);
  w.frames(1);   // time 10 steps = exactly tick 2
  CHECK_EQ(p->steps, 2);
  w.frames(10);  // time 20 steps = tick 4 = the loop end: tick 0 fires again as the clip wraps
  CHECK_EQ(p->steps, 3);
  w.frames(10);  // tick 2 of the second loop: step 30
  CHECK_EQ(p->steps, 4);
  for (int i = 0; i < 300; ++i) w.frames(1);  // 15 more loops, no drift: 2 events per 20 steps
  CHECK_EQ(p->steps, 4 + 30);
}

// ---- skeletal (CLP3) states -----------------------------------------------------------------------------------------------------
#include "fixtures/pack3d_builder.h"
#include "rensga/core/animation.h"

namespace {
// Rewrites a finished pack with an ANIC section and the controller's tables appended to BLOB (offsets rebased).
void withController(const rensga::fixtures::PackImage& in, animpack::Builder& ab, rensga::fixtures::PackImage& out) {
  struct Sec { u32 fourcc; std::vector<u8> data; u32 count; };
  std::vector<Sec> secs;
  const PackHeader* h = reinterpret_cast<const PackHeader*>(in.data);
  const PackSection* table = reinterpret_cast<const PackSection*>(in.data + sizeof(PackHeader));
  for (u32 i = 0; i < h->sectionCount; ++i) secs.push_back({table[i].fourcc, std::vector<u8>(in.data + table[i].offset, in.data + table[i].offset + table[i].size), table[i].count});
  u32 base = 0;
  for (Sec& s : secs)
    if (s.fourcc == pack_section::BLOB) {
      while (s.data.size() % 16) s.data.push_back(0);
      base = static_cast<u32>(s.data.size());
      s.data.insert(s.data.end(), ab.blob.begin(), ab.blob.end());
    }
  PackAnimController c = ab.ctls[0];
  if (c.paramCount) c.paramsOffset += base;
  if (c.stateCount) c.statesOffset += base;
  if (c.transitionCount) c.transitionsOffset += base;
  if (c.conditionCount) c.conditionsOffset += base;
  const u8* cp = reinterpret_cast<const u8*>(&c);
  secs.push_back({pack_section::ANIC, std::vector<u8>(cp, cp + sizeof c), 1});
  size_t off = (sizeof(PackHeader) + secs.size() * sizeof(PackSection) + 15) & ~size_t(15);
  std::vector<PackSection> nt;
  for (const Sec& s : secs) { nt.push_back({s.fourcc, static_cast<u32>(off), static_cast<u32>(s.data.size()), s.count}); off = (off + s.data.size() + 15) & ~size_t(15); }
  std::vector<u8> bytes(off, 0);
  PackHeader nh = *h;
  nh.sectionCount = static_cast<u32>(secs.size());
  nh.totalSize = static_cast<u32>(off);
  memcpy(bytes.data(), &nh, sizeof nh);
  memcpy(bytes.data() + sizeof nh, nt.data(), nt.size() * sizeof(PackSection));
  for (size_t i = 0; i < secs.size(); ++i) memcpy(bytes.data() + nt[i].offset, secs[i].data.data(), secs[i].data.size());
  out.assign(bytes);
}
}  // namespace

TEST(animator_skeletal_state_builds_the_same_palette_as_the_clip_sampler) {
  using rensga::fixtures::Pack3dBuilder;
  using rensga::fixtures::PackImage;
  Pack3dBuilder pb;
  PackImage base, img;
  pb.build(base);
  animpack::Builder ab;
  const Guid ctl = ab.addController(10, "Bend", {}, {{"Bend", PackAnimStateKind::Clip3, Pack3dBuilder::clipGuid()}}, {});
  withController(base, ab, img);
  PackView view;
  CHECK(view.open(img.data, img.size));
  PackAssets assets = view.assets();
  Arena arena;
  arena.create(1 << 21);
  World world;
  WorldConfig wc;
  wc.maxObjects = 16;
  wc.maxComponentsPerType = 8;
  world.init(wc, arena);
  LoadedPack lp;
  CHECK(world.loadPack(view, &lp));
  u32 handles[1] = {1};
  GfxAssets::bind(&view, &assets, handles, 1);
  GameLoop loop;
  loop.init(world);
  VirtualGamepad pad;
  GameObject* bar = world.findByName("Bar");
  world.destroy(bar->GetComponent<Animation>());  // the legacy player would also drive the palette
  loop.frame(1.0f / 60.0f, pad);
  Animator* an = world.addComponent<Animator>(bar);
  an->controller = ctl;
  SkinnedMeshRenderer* smr = bar->GetComponent<SkinnedMeshRenderer>();
  const PackClip3& clip = assets.clips3[0];
  const PackSkin& skin = assets.skins[clip.skin];
  for (int n = 1; n <= 12; ++n) {
    loop.frame(1.0f / 60.0f, pad);
    u32 count = 0;
    const Matrix4* pal = AnimationSystem::palette(world, *smr, &count);
    CHECK(pal != nullptr && count == skin.jointCount);
    if (!pal || count != skin.jointCount) break;
    // First step shows tick 0, then one tick per step (speed 1); the state loops, so the clip does too.
    PackClip3 eff = clip;
    eff.loop = 1;
    PackJointPose pose[8];
    sampleClip(eff, assets.clipPoses(clip), skin.jointCount, static_cast<u32>(n - 1), 60, pose);
    Matrix4 want[8];
    buildPalette(skin, assets.skinJoints(skin), pose, want);
    CHECK(memcmp(pal, want, sizeof(Matrix4) * count) == 0);
  }
  AnimationSystem::shutdown();
  GfxAssets::unbind();
  world.shutdown();
}
