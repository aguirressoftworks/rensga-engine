// F6.09 (B2): the CPU particle system. One static pool (no heap) is cut into one block per ParticleSystem (`maxParticles`). The
// simulation runs in world space once per fixed step; the random stream of a system is a private xorshift seeded from `seed`, so the
// same scene gives the same particles on every platform. Particles are visual only: nothing in the simulation reads them.
#include "rensga/core/gameplay.h"
#include "rensga/core/log.h"
#include "rensga/core/physics3d.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"

namespace rensga {
namespace {

struct Particle {
  Vector3 pos, vel;
  Scalar age = 0, life = 1;
};

struct Block {
  const ParticleSystem* owner = nullptr;
  u32 first = 0, cap = 0, live = 0;
  u32 rng = 1;
  Scalar time = 0, emitAcc = 0;
  u32 stamp = 0;
};

Particle g_pool[Particles::kPoolCapacity];
Block g_blocks[Particles::kMaxSystems];
u32 g_stamp = 0, g_rejected = 0, g_quads = 0;
bool g_warnedFull = false;

constexpr u32 kQuadChunk = 256;
Quad3D g_qbuf[kQuadChunk];
Scalar g_depth[512];
u16 g_order[512];

Scalar rand01(Block& b) {
  u32 x = b.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  b.rng = x ? x : 1u;
  return static_cast<Scalar>(x >> 8) * (1.0f / 16777216.0f);
}
Scalar randSigned(Block& b) { return rand01(b) * 2.0f - 1.0f; }

u32 wantedCap(const ParticleSystem& ps) {
  i32 n = ps.maxParticles;
  if (n < 1) n = 1;
  if (n > 512) n = 512;
  return static_cast<u32>(n);
}

bool rangesOverlap(u32 a, u32 an, u32 b, u32 bn) { return a < b + bn && b < a + an; }

Block* findBlock(const ParticleSystem& ps) {
  if (ps.poolIndex_ < Particles::kMaxSystems && g_blocks[ps.poolIndex_].owner == &ps) return &g_blocks[ps.poolIndex_];
  return nullptr;
}

void freeBlock(Block& b) {
  b = Block();
}

// The block of `ps`, made on first use (first fit in the pool). Null when the pool or the block table is full.
Block* acquire(ParticleSystem& ps) {
  const u32 cap = wantedCap(ps);
  if (Block* b = findBlock(ps)) {
    if (b->cap == cap) return b;
    freeBlock(*b);  // maxParticles changed (editor): the pool is cut again, the particles are gone
  }
  u32 slot = Particles::kMaxSystems;
  for (u32 i = 0; i < Particles::kMaxSystems; ++i)
    if (!g_blocks[i].owner) { slot = i; break; }
  u32 start = Particles::kPoolCapacity + 1;
  if (slot < Particles::kMaxSystems) {
    for (u32 pass = 0; pass <= Particles::kMaxSystems && start > Particles::kPoolCapacity; ++pass) {
      const u32 cand = pass == 0 ? 0 : (g_blocks[pass - 1].owner ? g_blocks[pass - 1].first + g_blocks[pass - 1].cap : Particles::kPoolCapacity + 1);
      if (cand + cap > Particles::kPoolCapacity) continue;
      bool free = true;
      for (u32 i = 0; i < Particles::kMaxSystems && free; ++i)
        if (g_blocks[i].owner && rangesOverlap(cand, cap, g_blocks[i].first, g_blocks[i].cap)) free = false;
      if (free) start = cand;
    }
  }
  if (start > Particles::kPoolCapacity) {
    if (!g_warnedFull) {
      g_warnedFull = true;
      RLOG_WARN("particles", "pool full (%u particles, %u systems): a ParticleSystem with maxParticles=%u cannot emit",
                static_cast<unsigned>(Particles::kPoolCapacity), static_cast<unsigned>(Particles::kMaxSystems), static_cast<unsigned>(cap));
    }
    return nullptr;
  }
  Block& b = g_blocks[slot];
  b = Block();
  b.owner = &ps;
  b.first = start;
  b.cap = cap;
  b.rng = static_cast<u32>(ps.seed) * 2654435761u + 0x9E3779B9u;
  if (!b.rng) b.rng = 1;
  b.stamp = g_stamp;
  ps.poolIndex_ = slot;
  return &b;
}

Vector3 unitSphere(Block& b) {
  for (int i = 0; i < 6; ++i) {
    const Vector3 v(randSigned(b), randSigned(b), randSigned(b));
    const Scalar m = v.sqrMagnitude();
    if (m > 0.01f && m <= 1.0f) return v;
  }
  return Vector3(0, 0.5f, 0);
}
void unitDisc(Block& b, Scalar& x, Scalar& z) {
  for (int i = 0; i < 6; ++i) {
    x = randSigned(b);
    z = randSigned(b);
    if (x * x + z * z <= 1.0f) return;
  }
  x = 0;
  z = 0;
}
Vector3 safeNormalize(const Vector3& v, const Vector3& fallback) {
  return v.sqrMagnitude() > 1e-8f ? v.normalized() : fallback;
}

// Adds one particle at the system's current transform.
void spawn(Block& b, const ParticleSystem& ps, const Matrix4& w) {
  if (b.live >= b.cap) { ++g_rejected; return; }
  const Scalar R = ps.shapeRadius;
  Vector3 lp, ld(0, 1, 0);
  switch (ps.shape) {
    case 1: {  // Sphere
      const Vector3 u = unitSphere(b);
      lp = u * R;
      ld = safeNormalize(u, ld);
      break;
    }
    case 2: {  // Hemisphere (+Y)
      Vector3 u = unitSphere(b);
      if (u.y < 0) u.y = -u.y;
      lp = u * R;
      ld = safeNormalize(u, ld);
      break;
    }
    case 3:  // Box: half extent R, emits up
      lp = Vector3(randSigned(b) * R, randSigned(b) * R, randSigned(b) * R);
      break;
    case 4: {  // Circle (XZ plane), emits outward
      Scalar x, z;
      unitDisc(b, x, z);
      lp = Vector3(x * R, 0, z * R);
      ld = safeNormalize(Vector3(x, 0, z), ld);
      break;
    }
    case 5:  // Point: from the origin in every direction
      ld = safeNormalize(unitSphere(b), ld);
      break;
    default: {  // Cone: base disc of radius R, opening angle shapeAngle around +Y
      Scalar x, z;
      unitDisc(b, x, z);
      lp = Vector3(x * R, 0, z * R);
      const Scalar t = Mathf::Tan(ps.shapeAngle * Mathf::Deg2Rad);
      ld = Vector3(x * t, 1.0f, z * t).normalized();
      break;
    }
  }
  Particle& p = g_pool[b.first + b.live++];
  p.pos = w.transformPoint(lp);
  p.vel = safeNormalize(w.transformVector(ld), Vector3(0, 1, 0)) * ps.startSpeed;
  p.age = 0;
  p.life = ps.startLifetime > 0.01f ? ps.startLifetime : 0.01f;
}

void emitHere(Block& b, ParticleSystem& ps, i32 n) {
  if (n <= 0 || !ps.transform()) return;
  const Matrix4 w = ps.transform()->localToWorld();
  for (i32 i = 0; i < n; ++i) spawn(b, ps, w);
}

void stepBlock(Block& b, ParticleSystem& ps, Scalar dt, const Vector3& g) {
  // Emission first (a burst at time 0 appears in the step the system starts), then the motion of everything alive.
  if (ps.playing_) {
    const Scalar t0 = b.time;
    b.time += dt;
    const u32 nb = ps.burstTimes.count < ps.burstCounts.count ? ps.burstTimes.count : ps.burstCounts.count;
    for (u32 i = 0; i < nb; ++i) {
      const Scalar bt = ps.burstTimes.items[i];
      if (bt >= t0 && bt < b.time) emitHere(b, ps, ps.burstCounts.items[i]);
    }
    if (ps.emissionRate > 0) {
      b.emitAcc += ps.emissionRate * dt;
      const i32 whole = static_cast<i32>(b.emitAcc);
      if (whole > 0) {
        b.emitAcc -= static_cast<Scalar>(whole);
        emitHere(b, ps, whole);
      }
    }
    if (b.time >= ps.duration) {
      if (ps.loop) { b.time -= ps.duration; if (b.time < 0) b.time = 0; }
      else ps.playing_ = false;  // living particles finish
    }
  }
  const Vector3 acc = g * ps.gravityModifier * dt;
  for (u32 i = 0; i < b.live;) {
    Particle& p = g_pool[b.first + i];
    p.age += dt;
    if (p.age >= p.life) {
      p = g_pool[b.first + --b.live];  // swap-remove: contiguous, order is deterministic
      continue;
    }
    p.vel += acc;
    p.pos += p.vel * dt;
    ++i;
  }
  ps.liveCount_ = b.live;
}

u8 lerpU8(u8 a, u8 b, Scalar t) {
  const Scalar v = static_cast<Scalar>(a) + (static_cast<Scalar>(b) - static_cast<Scalar>(a)) * t;
  return static_cast<u8>(v <= 0 ? 0 : (v >= 255 ? 255 : v + 0.5f));
}
u8 mulU8(u8 a, u8 b) { return static_cast<u8>((static_cast<u32>(a) * b + 127) / 255); }

}  // namespace

namespace Particles {

void reset() {
  for (Block& b : g_blocks) freeBlock(b);
  g_rejected = 0;
  g_quads = 0;
  g_warnedFull = false;
}

void play(ParticleSystem& ps) {
  if (!ps.ownerWorld()) return;  // not in a World: nothing to simulate
  if (Block* b = acquire(ps)) {
    if (!ps.playing_) { b->time = 0; b->emitAcc = 0; }
    ps.playing_ = true;
  }
}
void stop(ParticleSystem& ps) { ps.playing_ = false; }
void emit(ParticleSystem& ps, i32 count) {
  if (!ps.ownerWorld()) return;
  if (Block* b = acquire(ps)) {
    emitHere(*b, ps, count);
    ps.liveCount_ = b->live;
  } else if (count > 0) {
    g_rejected += static_cast<u32>(count);
  }
}
void clear(ParticleSystem& ps) {
  if (Block* b = findBlock(ps)) b->live = 0;
  ps.liveCount_ = 0;
}

void fixedStep(World& world, Scalar dt) {
  ++g_stamp;
  const Vector3 g = physics3d::settings().gravity;
  world.forEach<ParticleSystem>([&](ParticleSystem& ps) {
    GameObject* go = ps.gameObject();
    if (!go) return;
    Block* b = findBlock(ps);
    if (!b) {
      if (!go->activeInHierarchy() || !ps.enabled()) return;
      b = acquire(ps);
      if (!b) return;
      if (ps.playOnAwake) ps.playing_ = true;  // first sight: the system starts like Unity's Play On Awake
    }
    b->stamp = g_stamp;
    if (!go->activeInHierarchy() || !ps.enabled()) return;  // a disabled system keeps its particles frozen and hidden
    stepBlock(*b, ps, dt, g);
  });
  for (Block& b : g_blocks)
    if (b.owner && b.stamp != g_stamp) freeBlock(b);  // its component is gone
}

u32 live() {
  u32 n = 0;
  for (const Block& b : g_blocks)
    if (b.owner) n += b.live;
  return n;
}
u32 reserved() {
  u32 n = 0;
  for (const Block& b : g_blocks)
    if (b.owner) n += b.cap;
  return n;
}
u32 systems() {
  u32 n = 0;
  for (const Block& b : g_blocks) n += b.owner ? 1u : 0u;
  return n;
}
u32 rejected() { return g_rejected; }
u32 quadsDrawn() { return g_quads; }

void draw(World& world, IRenderer& renderer, const CameraView& view, i32 cullingMask) {
  g_quads = 0;
  world.forEach<ParticleSystem>([&](ParticleSystem& ps) {
    GameObject* go = ps.gameObject();
    if (!go || !go->activeInHierarchy() || !ps.enabled() || !layerInMask(cullingMask, go->layer)) return;
    Block* b = findBlock(ps);
    if (!b || b->live == 0) return;
    u32 tex = 0, texW = 0, texH = 0;
    if (GfxAssets::bound() && !ps.texture.isNull()) {
      const u32 ti = GfxAssets::textureIndex(ps.texture);
      const PackAssets& as = GfxAssets::assets();
      if (ti != kInvalidIndex && ti < as.textureCount) {
        tex = GfxAssets::handle(ti);
        texW = as.textures[ti].width;
        texH = as.textures[ti].height;
      }
    }
    const u32 n = b->live;
    // Back to front along the view direction: shell sort of the indices (no heap, no libc sort).
    for (u32 i = 0; i < n; ++i) {
      g_order[i] = static_cast<u16>(i);
      g_depth[i] = Vector3::Dot(g_pool[b->first + i].pos - view.position, view.forward);
    }
    for (u32 gap = n / 2; gap > 0; gap /= 2) {
      for (u32 i = gap; i < n; ++i) {
        const u16 t = g_order[i];
        const Scalar d = g_depth[t];
        u32 j = i;
        while (j >= gap && g_depth[g_order[j - gap]] < d) { g_order[j] = g_order[j - gap]; j -= gap; }
        g_order[j] = t;
      }
    }
    u32 inChunk = 0;
    for (u32 k = 0; k < n; ++k) {
      const Particle& p = g_pool[b->first + g_order[k]];
      if (g_depth[g_order[k]] < view.nearClip && !view.ortho) continue;
      const Scalar t = p.age / p.life;
      Scalar size = ps.startSize;
      if (ps.sizeOverLifetime) size *= ps.sizeStart + (ps.sizeEnd - ps.sizeStart) * t;
      if (size <= 0) continue;
      Color32 col = ps.startColor;
      if (ps.colorOverLifetime) {
        col = Color32(mulU8(ps.startColor.r, lerpU8(ps.colorStart.r, ps.colorEnd.r, t)), mulU8(ps.startColor.g, lerpU8(ps.colorStart.g, ps.colorEnd.g, t)),
                      mulU8(ps.startColor.b, lerpU8(ps.colorStart.b, ps.colorEnd.b, t)), mulU8(ps.startColor.a, lerpU8(ps.colorStart.a, ps.colorEnd.a, t)));
      }
      if (col.a == 0) continue;
      const Scalar h = size * 0.5f;
      const Vector3 r = view.right * h, u = view.up * h;
      Quad3D& q = g_qbuf[inChunk++];
      q.p[0] = p.pos - r + u;
      q.p[1] = p.pos + r + u;
      q.p[2] = p.pos + r - u;
      q.p[3] = p.pos - r - u;
      q.color = col;
      q.u0 = 0; q.v0 = 0; q.u1 = 1; q.v1 = 1;
      if (inChunk == kQuadChunk) {
        submitQuads(renderer, view, tex, texW, texH, g_qbuf, inChunk);
        g_quads += inChunk;
        inChunk = 0;
      }
    }
    if (inChunk) {
      submitQuads(renderer, view, tex, texW, texH, g_qbuf, inChunk);
      g_quads += inChunk;
    }
  });
}

}  // namespace Particles
}  // namespace rensga
