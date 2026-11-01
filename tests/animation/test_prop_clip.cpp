// F6.03 (B1): property clips. Curve sampling (linear, step, Hermite), writing reflected fields by path (Transform, Color32 channel,
// I32, Guid, GameObject.active, child path), weights, and animation events (range rule, OnAnimationEvent, METHOD dispatch).
#include "anim_pack.h"
#include "rensga/core/animator.h"
#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/prop_clip.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;
using namespace animpack;

namespace {
struct EvProbe : Behaviour {
  REFLECT(EvProbe)
  SERIALIZE_FIELD(i32, steps, 0)
  SERIALIZE_FIELD(i32, lastInt, 0)
  SERIALIZE_FIELD(Scalar, lastFloat, 0)
  SERIALIZE_FIELD(i32, generic, 0)
  METHOD(Footstep)
  METHOD(Hit)
  METHOD(Pitch)
  void Footstep() { ++steps; }
  void Hit(i32 v) { lastInt = v; }
  void Pitch(Scalar v) { lastFloat = v; }
  void OnAnimationEvent(const AnimationEvent&) override { ++generic; }
};

struct ClipWorld {
  Builder b;
  u8* mem = nullptr;
  size_t size = 0;
  PackView view;
  PackContent content;
  Arena arena;
  World world;
  LoadedPack lp;
  PackAssets assets;
  void open() {
    mem = b.build(&size);
    CHECK(view.open(mem, size));
    content = view.content();
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    CHECK(world.loadPack(view, &lp));
    assets = view.assets();
    GfxAssets::bind(&view, &assets, nullptr, 0);
  }
  ~ClipWorld() {
    GfxAssets::unbind();
    world.shutdown();
    free(mem);
  }
  PropClipSource source(u32 clip) const {
    const PackSection* s = view.find(pack_section::STRS);
    return PropClipSource{&content.propClips[clip], content.blob, content.blobSize, reinterpret_cast<const char*>(view.base + s->offset), s->size};
  }
};
u64 q(Scalar ticks) { return static_cast<u64>(ticks * 65536.0f); }
}  // namespace

TEST(prop_clip_curve_modes) {
  ClipWorld w;
  u32 root = w.b.addObject("Root");
  (void)root;
  CurveDef lin, step, herm, hermT;
  lin.component = "Transform"; lin.field = "localPosition"; lin.part = 0; lin.keys = {{0, 0}, {10, 10}};
  step = lin; step.interp = kPropInterpStep; step.keys = {{0, 1}, {10, 5}};
  herm = lin; herm.interp = kPropInterpHermite; herm.keys = {{0, 0}, {10, 10}};
  hermT = lin; hermT.interp = kPropInterpHermite; hermT.keys = {{0, 0, 0, 2}, {10, 10, 0, 0}};
  w.b.addClip(1, "c", 60, 10, false, {lin, step, herm, hermT});
  w.open();
  const PropClipSource s = w.source(0);
  const PackPropCurve* c = s.curves();
  CHECK_NEAR(PropClips::sampleCurve(s, c[0], q(5)), 5.0f, 1e-5f);
  CHECK_NEAR(PropClips::sampleCurve(s, c[0], q(2.5f)), 2.5f, 1e-5f);
  CHECK_NEAR(PropClips::sampleCurve(s, c[0], 0), 0.0f, 1e-5f);   // before the first key
  CHECK_NEAR(PropClips::sampleCurve(s, c[0], q(99)), 10.0f, 1e-5f);  // after the last key
  CHECK_NEAR(PropClips::sampleCurve(s, c[1], q(9.9f)), 1.0f, 1e-6f);  // step holds the left key
  CHECK_NEAR(PropClips::sampleCurve(s, c[1], q(10)), 5.0f, 1e-6f);
  CHECK_NEAR(PropClips::sampleCurve(s, c[2], q(5)), 5.0f, 1e-4f);     // zero tangents with a straight line: the midpoint
  CHECK(PropClips::sampleCurve(s, c[2], q(2)) < 2.0f);                // ease in: slower than linear at the start
  // s = 0.5: 0.5*p0 + 0.125*dt*m0 + 0.5*p1 - 0.125*dt*m1 = 0 + 0.125*10*2 + 5 = 7.5
  CHECK_NEAR(PropClips::sampleCurve(s, c[3], q(5)), 7.5f, 1e-4f);
}

TEST(prop_clip_applies_reflected_fields_by_path) {
  ClipWorld w;
  u32 root = w.b.addObject("Root");
  u32 arm = w.b.addObject("Arm", static_cast<int>(root));
  w.b.addComponent(root, *SpriteRenderer::staticTypeInfo());
  w.b.addComponent(arm, *SpriteRenderer::staticTypeInfo());
  CurveDef px, scl, col, srcx, tex, active, rot0, rot3, armColor;
  px.component = "Transform"; px.field = "localPosition"; px.part = 0; px.keys = {{0, 0}, {10, 10}};
  scl = px; scl.field = "localScale"; scl.part = 1; scl.keys = {{0, 1}, {10, 3}};
  col.component = "SpriteRenderer"; col.field = "color"; col.part = 0; col.type = FieldType::Color32; col.keys = {{0, 0}, {10, 200}};
  srcx.component = "SpriteRenderer"; srcx.field = "srcX"; srcx.type = FieldType::I32; srcx.interp = kPropInterpStep; srcx.keys = {{0, 0}, {5, 16}, {10, 32}};
  tex.component = "SpriteRenderer"; tex.field = "texture"; tex.type = FieldType::Guid; tex.interp = kPropInterpStep; tex.keys = {{0, 0}, {5, 0}};
  tex.guids = {guidN(10), guidN(11)};
  active.path = "Arm"; active.component = "GameObject"; active.field = "active"; active.type = FieldType::Bool; active.keys = {{0, 1}, {5, 0}};
  armColor.path = "Arm"; armColor.component = "SpriteRenderer"; armColor.field = "color"; armColor.part = 3; armColor.type = FieldType::Color32; armColor.keys = {{0, 255}, {10, 0}};
  rot0.component = "Transform"; rot0.field = "localRotation"; rot0.part = 2; rot0.keys = {{0, 0}, {10, 1}};
  rot3 = rot0; rot3.part = 3; rot3.keys = {{0, 1}, {10, 1}};
  w.b.addClip(1, "c", 60, 10, false, {px, scl, col, srcx, tex, active, armColor, rot0, rot3});
  w.open();
  GameObject* go = w.world.findByName("Root");
  GameObject* armGo = w.world.findByName("Arm");
  const PropClipSource s = w.source(0);
  CHECK(PropClips::apply(w.world, go, s, q(5), 1.0f) == 9u);
  CHECK_NEAR(go->transform.localPosition().x, 5.0f, 1e-5f);
  CHECK_NEAR(go->transform.localScale().y, 2.0f, 1e-5f);
  SpriteRenderer* sr = go->GetComponent<SpriteRenderer>();
  CHECK_EQ(sr->color.r, 100);
  CHECK_EQ(sr->srcX, 16);
  CHECK(sr->texture == guidN(11));
  CHECK(!armGo->activeSelf());
  CHECK_EQ(armGo->GetComponent<SpriteRenderer>()->color.a, 128);  // 127.5 rounds up
  // Rotation z=0.5, w=1 is renormalised after the write.
  const Quaternion r = go->transform.localRotation();
  CHECK_NEAR(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w, 1.0f, 1e-5f);
  // Before tick 5 the guid is the first one and the arm is active again.
  PropClips::apply(w.world, go, s, q(1), 1.0f);
  CHECK(sr->texture == guidN(10));
  CHECK(armGo->activeSelf());
  CHECK_EQ(sr->srcX, 0);
}

TEST(prop_clip_weight_blends_from_the_current_value) {
  ClipWorld w;
  u32 root = w.b.addObject("Root");
  w.b.addComponent(root, *SpriteRenderer::staticTypeInfo());
  CurveDef px, srcx;
  px.component = "Transform"; px.field = "localPosition"; px.part = 0; px.keys = {{0, 10}, {10, 10}};
  srcx.component = "SpriteRenderer"; srcx.field = "srcX"; srcx.type = FieldType::I32; srcx.keys = {{0, 40}};
  w.b.addClip(1, "c", 60, 10, false, {px, srcx});
  w.open();
  GameObject* go = w.world.findByName("Root");
  go->transform.setLocalPosition(Vector3(2, 0, 0));
  const PropClipSource s = w.source(0);
  PropClips::apply(w.world, go, s, 0, 0.25f);
  CHECK_NEAR(go->transform.localPosition().x, 4.0f, 1e-5f);        // 2 + (10 - 2) * 0.25
  CHECK_EQ(go->GetComponent<SpriteRenderer>()->srcX, 0);            // discrete values switch at weight 0.5
  PropClips::apply(w.world, go, s, 0, 0.5f);
  CHECK_EQ(go->GetComponent<SpriteRenderer>()->srcX, 40);
  CHECK_EQ(PropClips::apply(w.world, go, s, 0, 0.0f), 0u);          // weight 0 writes nothing
}

TEST(prop_clip_events_fire_in_a_half_open_range_and_call_methods) {
  ClipWorld w;
  u32 root = w.b.addObject("Root");
  w.b.addComponent(root, *EvProbe::staticTypeInfo());
  w.b.addClip(1, "c", 60, 20, true, {}, {{0, "Footstep"}, {5, "Hit", 7}, {10, "Pitch", 0, 1.5f}, {10, "Missing"}, {20, "Footstep"}});
  w.open();
  GameObject* go = w.world.findByName("Root");
  EvProbe* p = go->GetComponent<EvProbe>();
  const PropClipSource s = w.source(0);
  CHECK_EQ(PropClips::fireEvents(go, s, -1, 0), 1u);                // tick 0 fires on entry
  CHECK_EQ(p->steps, 1);
  CHECK_EQ(PropClips::fireEvents(go, s, 0, 4 * 65536 + 65535), 0u);  // nothing strictly between 0 and tick 5
  CHECK_EQ(PropClips::fireEvents(go, s, 0, 5 * 65536), 1u);          // a tick fires when the range reaches it (inclusive end)
  CHECK_EQ(p->lastInt, 7);
  CHECK_EQ(PropClips::fireEvents(go, s, 5 * 65536, 10 * 65536), 2u);  // exclusive start: tick 5 is not repeated
  CHECK_NEAR(p->lastFloat, 1.5f, 1e-6f);
  CHECK_EQ(p->generic, 1 + 1 + 2);                                   // OnAnimationEvent got every event, named or not
  CHECK_EQ(PropClips::fireEvents(go, s, 10 * 65536, 20 * 65536), 1u);
  CHECK_EQ(p->steps, 2);
}

TEST(prop_clip_event_handler_can_be_replaced) {
  ClipWorld w;
  w.b.addObject("Root");
  w.b.addClip(1, "c", 60, 20, true, {}, {{3, "Boom", 4, 2.0f, "text"}});
  w.open();
  static int calls = 0;
  static i32 gotInt = 0;
  static Scalar gotTime = 0;
  static char gotName[16] = {}, gotStr[16] = {};
  calls = 0;
  AnimationEvents::setHandler([](GameObject*, const AnimationEvent& e) {
    ++calls;
    gotInt = e.intParameter;
    gotTime = e.time;
    strncpy(gotName, e.functionName, 15);
    strncpy(gotStr, e.stringParameter, 15);
  });
  const PropClipSource s = w.source(0);
  PropClips::fireEvents(w.world.findByName("Root"), s, -1, 20 * 65536);
  AnimationEvents::setHandler(nullptr);
  CHECK_EQ(calls, 1);
  CHECK_EQ(gotInt, 4);
  CHECK_NEAR(gotTime, 0.05f, 1e-6f);  // tick 3 at 60 fps
  CHECK(strcmp(gotName, "Boom") == 0 && strcmp(gotStr, "text") == 0);
}

TEST(prop_clip_pack_validates_and_rejects_a_bad_range) {
  ClipWorld w;
  w.b.addObject("Root");
  w.b.addClip(1, "c", 60, 20, true, {});
  w.open();
  CHECK_EQ(w.content.propClipCount, 1u);
  CHECK(w.content.findPropClip(guidN(1)) != nullptr);
  CHECK(w.content.findPropClip(guidN(2)) == nullptr);
}
