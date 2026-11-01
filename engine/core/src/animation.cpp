// F5 (PSP Engine 6): skeletal clip player. Everything is integer time + float math with +,-,*,/ and sqrt only, so Linux and the
// PSP produce the same bits (RULES 1.9). The palette pool is a fixed array (or arena memory from init()); nothing allocates per frame.
#include "rensga/core/animation.h"
#include "rensga/core/animator_system.h"  // F6 B1

#include <string.h>

#include "rensga/core/gameplay.h"
#include "rensga/core/log.h"
#include "rensga/core/time.h"

namespace rensga {

namespace {
constexpr u32 kStarted = 0x80000000u;  // Animation::phase bit: the component has been initialised by the system
constexpr u32 kFreshPlay = 0x40000000u; // Animation::phase bit: Play() was called, show the first pose before advancing
constexpr u32 kFracMask = 0xFFFFu;     // phase low 16 bits: speed carry in 1/65536 ticks
constexpr u32 kMaxSlots = 64;
constexpr u32 kStaticPool = 1024;      // matrices in the fallback pool (64 KB)

struct Slot {
  u32 offset = 0, count = 0;
};
Matrix4 g_staticPool[kStaticPool];
Matrix4* g_pool = g_staticPool;
u32 g_poolCap = kStaticPool;
u32 g_poolUsed = 0;
Slot g_slots[kMaxSlots];
u32 g_slotCount = 0;
const PackAssets* g_assets = nullptr;
const PackAssets* g_gfx = nullptr;  // the GfxAssets table the pool was last reset for
const PackMeshLit* g_lit = nullptr;
u32 g_litCount = 0;
bool g_trace = false;
u32 g_steps = 0;
bool g_warned = false;

Quaternion nlerp(const Scalar* a, const Scalar* b, Scalar t) {
  Scalar d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  const Scalar sgn = d < 0 ? -1.0f : 1.0f;  // shortest arc
  const Scalar u = 1.0f - t;
  Quaternion q(a[0] * u + sgn * b[0] * t, a[1] * u + sgn * b[1] * t, a[2] * u + sgn * b[2] * t, a[3] * u + sgn * b[3] * t);
  return q.normalized();
}

void bindAssets() {
  if (g_assets || !GfxAssets::bound()) return;
  g_assets = &GfxAssets::assets();  // fixedStep notes g_gfx
}

const PackClip3* clipFor(const Animation& an, const SkinnedMeshRenderer* smr) {
  if (!g_assets || !g_assets->clip3Count) return nullptr;
  if (const PackClip3* c = g_assets->findClip3(an.clip)) return c;
  // The clip field may name the model asset instead of a clip: take the first clip made for the mesh's skin.
  if (smr && g_lit && smr->meshIndex < g_litCount) {
    for (u32 i = 0; i < g_assets->clip3Count; ++i)
      if (g_assets->clips3[i].skin == g_lit[smr->meshIndex].skin) return &g_assets->clips3[i];
  }
  return &g_assets->clips3[0];
}

bool allocSlot(u32 joints, u32* slotOut) {
  if (g_slotCount >= kMaxSlots || g_poolUsed + joints > g_poolCap) return false;
  g_slots[g_slotCount] = {g_poolUsed, joints};
  g_poolUsed += joints;
  *slotOut = g_slotCount++;
  return true;
}
}  // namespace

void sampleClip(const PackClip3& clip, const PackJointPose* poses, u32 jointCount, u32 tick, u32 tickHz, PackJointPose* out) {
  if (clip.frameCount == 0 || tickHz == 0 || jointCount == 0) return;
  const u64 scaled = static_cast<u64>(tick) * clip.fps;
  u64 frame = scaled / tickHz;
  const u32 rem = static_cast<u32>(scaled - frame * tickHz);
  Scalar t = static_cast<Scalar>(rem) / static_cast<Scalar>(tickHz);
  u32 f0, f1;
  if (clip.loop) {
    f0 = static_cast<u32>(frame % clip.frameCount);
    f1 = f0 + 1 == clip.frameCount ? 0 : f0 + 1;
  } else if (frame >= static_cast<u64>(clip.frameCount) - 1) {
    f0 = f1 = clip.frameCount - 1;  // one-shot: hold the last frame
    t = 0;
  } else {
    f0 = static_cast<u32>(frame);
    f1 = f0 + 1;
  }
  const PackJointPose* a = poses + static_cast<size_t>(f0) * jointCount;
  const PackJointPose* b = poses + static_cast<size_t>(f1) * jointCount;
  const Scalar u = 1.0f - t;
  for (u32 j = 0; j < jointCount; ++j) {
    if (f0 == f1 || t == 0) { out[j] = a[j]; continue; }
    Quaternion q = nlerp(a[j].r, b[j].r, t);
    out[j].r[0] = q.x; out[j].r[1] = q.y; out[j].r[2] = q.z; out[j].r[3] = q.w;
    for (int k = 0; k < 3; ++k) out[j].t[k] = a[j].t[k] * u + b[j].t[k] * t;
    out[j].s = a[j].s * u + b[j].s * t;
  }
}

void buildPalette(const PackSkin& skin, const PackJoint* joints, const PackJointPose* pose, Matrix4* palette) {
  const u32 n = skin.jointCount;
  // Model-space matrices first (parents come before children), then multiply each by its inverse bind in place.
  for (u32 i = 0; i < n; ++i) {
    const PackJointPose& p = pose[i];
    Matrix4 local = Matrix4::TRS(Vector3(p.t[0], p.t[1], p.t[2]), Quaternion(p.r[0], p.r[1], p.r[2], p.r[3]), Vector3(p.s, p.s, p.s));
    palette[i] = joints[i].parent >= 0 ? palette[joints[i].parent] * local : local;
  }
  for (u32 i = 0; i < n; ++i) {
    Matrix4 inv;
    memcpy(inv.m, joints[i].invBind, sizeof(inv.m));
    palette[i] = palette[i] * inv;
  }
}

bool AnimationSystem::init(Arena& arena, const PackAssets& assets, u32 maxMatrices) {
  g_assets = &assets;
  if (maxMatrices == 0) maxMatrices = kStaticPool;
  Matrix4* p = arena.allocateArray<Matrix4>(maxMatrices, 16);
  if (!p) return false;
  for (u32 i = 0; i < maxMatrices; ++i) p[i] = Matrix4::identity();
  g_pool = p;
  g_poolCap = maxMatrices;
  g_poolUsed = 0;
  g_slotCount = 0;
  g_steps = 0;
  return true;
}

void AnimationSystem::shutdown() {
  g_pool = g_staticPool;
  g_poolCap = kStaticPool;
  g_poolUsed = 0;
  g_slotCount = 0;
  g_assets = nullptr;
  g_gfx = nullptr;
  g_lit = nullptr;
  g_litCount = 0;
  g_steps = 0;
  g_warned = false;
  AnimatorSystem::shutdown();  // F6 B1
}

void AnimationSystem::fixedStep(World& world) {
  AnimatorSystem::fixedStep(world);  // --- F6 B1 --- Animator controllers and property clips advance on the same fixed step
  bindAssets();
  // The pack changes on a scene reload (new records, same bytes layout per pack): re-read the tables every step, they are pointers.
  if (GfxAssets::bound()) {
    if (g_gfx != &GfxAssets::assets()) { g_gfx = g_assets = &GfxAssets::assets(); g_poolUsed = 0; g_slotCount = 0; }
    if (const PackView* pv = GfxAssets::pack()) g_lit = pv->records<PackMeshLit>(pack_section::MSHL, &g_litCount);
  }
  if (!g_assets || !g_assets->clip3Count || !g_assets->skinCount) return;
  const Scalar fd = Time::fixedDeltaTime();
  const u32 tickHz = fd > 0 ? static_cast<u32>(1.0f / fd + 0.5f) : 60u;
  bool any = false, retry = false;
  for (int pass = 0; pass < 2; ++pass) {
    retry = false;
    world.forEach<Animation>([&](Animation& an) {
      GameObject* go = an.gameObject();
      if (!go || !go->activeInHierarchy() || !an.enabled()) return;
      const SkinnedMeshRenderer* smr = world.getComponent<SkinnedMeshRenderer>(go);
      const PackClip3* clip = clipFor(an, smr);
      if (!clip || clip->skin >= g_assets->skinCount) return;
      const PackSkin& skin = g_assets->skins[clip->skin];
      bool fresh = false;
      if (an.paletteSlot == kInvalidIndex || an.paletteSlot >= g_slotCount || g_slots[an.paletteSlot].count != skin.jointCount) {
        if (!allocSlot(skin.jointCount, &an.paletteSlot)) { retry = true; an.paletteSlot = kInvalidIndex; return; }
        fresh = true;
      }
      if (!(an.phase & kStarted)) {
        an.playing = an.playAutomatically;
        an.phase |= kStarted;
        fresh = true;
      }
      if (an.phase & kFreshPlay) { an.phase &= ~kFreshPlay; fresh = true; }
      if (!an.playing && !fresh) return;
      if (an.playing && !fresh) {
        // speed in 1/65536 ticks, accumulated as an integer so every platform advances identically
        const Scalar sp = an.speed < 0 ? 0 : an.speed;
        const u32 add = sp > 60000.0f ? 0xFFFF0000u : static_cast<u32>(sp * 65536.0f);
        const u32 frac = (an.phase & kFracMask) + (add & kFracMask);
        an.tick += (add >> 16) + (frac >> 16);
        an.phase = (an.phase & ~kFracMask) | (frac & kFracMask);
      }
      PackClip3 eff = *clip;
      if (an.wrapMode == 1) eff.loop = 0;
      else if (an.wrapMode == 2) eff.loop = 1;
      if (!eff.loop && an.tick > 0 && static_cast<u64>(an.tick) * eff.fps / tickHz >= eff.frameCount) {
        if (!fresh) an.playing = false;  // one-shot finished: it holds the last pose and reports isPlaying() == false
      }
      PackJointPose pose[64];
      if (skin.jointCount > 64) return;
      sampleClip(eff, g_assets->clipPoses(*clip), skin.jointCount, an.tick, tickHz, pose);
      buildPalette(skin, g_assets->skinJoints(skin), pose, g_pool + g_slots[an.paletteSlot].offset);
      any = true;
    });
    if (!retry) break;
    // Pool or slot table full (destroyed Animations leak their slots): drop them all and give every live Animation a new one.
    if (pass == 0) {
      g_poolUsed = 0;
      g_slotCount = 0;
      world.forEach<Animation>([&](Animation& a) { a.paletteSlot = kInvalidIndex; });
    } else if (!g_warned) {
      g_warned = true;
      RLOG_WARN("anim", "too many skinned characters for the palette pool (%u slots, %u matrices)", kMaxSlots, g_poolCap);
    }
  }
  (void)any;
  ++g_steps;
  if (g_trace) RLOG_INFO("game", "rensga: skin f=%u h=%08x", static_cast<unsigned>(g_steps - 1), static_cast<unsigned>(skinHash()));
}

const Matrix4* AnimationSystem::palette(const World& world, const SkinnedMeshRenderer& smr, u32* count) {
  if (count) *count = 0;
  GameObject* go = smr.gameObject();
  if (!go) return nullptr;
  const Animation* an = world.getComponent<Animation>(go);
  if (!an || an->paletteSlot == kInvalidIndex || an->paletteSlot >= g_slotCount) return AnimatorSystem::palette(world, smr, count);  // --- F6 B1 --- skeletal Animator states
  if (count) *count = g_slots[an->paletteSlot].count;
  return g_pool + g_slots[an->paletteSlot].offset;
}

u32 AnimationSystem::skinHash() {
  if (g_slotCount == 0) return 0;
  u32 h = 2166136261u;
  for (u32 s = 0; s < g_slotCount; ++s) {
    const u8* b = reinterpret_cast<const u8*>(g_pool + g_slots[s].offset);
    const u32 n = g_slots[s].count * static_cast<u32>(sizeof(Matrix4));
    for (u32 i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  }
  return h;
}

void AnimationSystem::setTrace(bool on) { g_trace = on; AnimatorSystem::setTrace(on); }  // F6 B1: also "rensga: anim f=.. h=.."
u32 AnimationSystem::stepCount() { return g_steps; }

void Animation::Play() { playing = true; tick = 0; phase = kStarted | kFreshPlay; }
void Animation::Stop() { playing = false; }
bool Animation::isPlaying() const { return playing; }

}  // namespace rensga
