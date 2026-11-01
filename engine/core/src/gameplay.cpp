// F2.05 Engine 5: gameplay framework implementation. No heap use; everything runs on the World's pools.
#include "rensga/core/gameplay.h"

#include <string.h>

#include "rensga/core/physics3d.h"
#include "rensga/core/rigidbody2d.h"
#include "rensga/core/input.h"
#include "rensga/core/time.h"
#include "rensga/core/ui_components.h"  // F6 (D1): UI::draw
#include "rensga/core/world.h"

namespace rensga {

namespace {
constexpr Scalar kEps = 0.01f;
constexpr Scalar kMaxSubStep = 4.0f;  // pixels; tiles are at least 8 px, so a sub-step never skips a tile

const PackView* g_pack = nullptr;
PackAssets g_assets;
u32 g_handles[GfxAssets::kMaxTextures] = {};
u32 g_handleCount = 0;
bool g_bound = false;

Scalar absS(Scalar v) { return v < 0 ? -v : v; }
}  // namespace

// ---- GfxAssets --------------------------------------------------------------------------------------------------
void GfxAssets::bind(const PackView* pack, const PackAssets* assets, const u32* handles, u32 count) {
  g_pack = pack;
  g_assets = assets ? *assets : PackAssets();
  g_handleCount = count < kMaxTextures ? count : kMaxTextures;
  for (u32 i = 0; i < g_handleCount; ++i) g_handles[i] = handles ? handles[i] : 0;
  g_bound = assets != nullptr;
  UI::resetFocus();  // F6 (D1): the focus lives in ui_runtime.cpp
  Physics2D::loadFromPack(pack);
  physics3d::loadFromPack(pack);  // F6 a2: PHY3 (3D gravity and layer matrix)
}
void GfxAssets::unbind() { bind(nullptr, nullptr, nullptr, 0); }
bool GfxAssets::bound() { return g_bound; }
const PackView* GfxAssets::pack() { return g_pack; }
const PackAssets& GfxAssets::assets() { return g_assets; }
u32 GfxAssets::handle(u32 textureIndex) { return textureIndex < g_handleCount ? g_handles[textureIndex] : 0; }
u32 GfxAssets::textureIndex(const Guid& texture) {
  if (texture.isNull()) return kInvalidIndex;
  for (u32 i = 0; i < g_assets.textureCount; ++i)
    if (g_assets.textures[i].guid == texture) return i;
  return kInvalidIndex;
}

static u32 clampOrder(i32 order) { return static_cast<u32>((order < -2048 ? -2048 : (order > 2047 ? 2047 : order)) + 2048); }
u32 spriteSortKey(i32 sortingOrder) { return kSpriteSortBase + clampOrder(sortingOrder); }

// ---- SpriteAnimator ---------------------------------------------------------------------------------------------
bool SpriteAnimator::PlayGuid(const Guid& clipGuid) {
  const PackAssets& as = GfxAssets::assets();
  for (u32 i = 0; i < as.animCount; ++i) {
    if (!(as.anims[i].guid == clipGuid)) continue;
    if (animIndex != i || !playing) {
      animIndex = i;
      clock = 0;
      finished = false;
    }
    playing = true;
    ApplyFrame();
    return true;
  }
  return false;
}

bool SpriteAnimator::Play(const char* name) {
  const PackAssets& as = GfxAssets::assets();
  const PackView* pack = GfxAssets::pack();
  if (!pack || !name) return false;
  for (u32 i = 0; i < as.animCount; ++i) {
    if (strcmp(pack->string(as.anims[i].nameStr), name) != 0) continue;
    return PlayGuid(as.anims[i].guid);
  }
  return false;
}

const char* SpriteAnimator::currentClipName() const {
  const PackAssets& as = GfxAssets::assets();
  if (animIndex >= as.animCount || !GfxAssets::pack()) return "";
  return GfxAssets::pack()->string(as.anims[animIndex].nameStr);
}

u32 SpriteAnimator::currentFrame() const {
  const PackAssets& as = GfxAssets::assets();
  if (animIndex >= as.animCount) return 0;
  const PackAnimRec& a = as.anims[animIndex];
  if (a.frameCount == 0) return 0;
  u32 f = static_cast<u32>(clock * static_cast<Scalar>(a.fps));
  if (a.loop) return f % a.frameCount;
  return f >= a.frameCount ? a.frameCount - 1 : f;
}

void SpriteAnimator::ApplyFrame() {
  const PackAssets& as = GfxAssets::assets();
  if (animIndex >= as.animCount) return;
  SpriteRenderer* sr = gameObject() ? gameObject()->GetComponent<SpriteRenderer>() : nullptr;
  if (!sr) return;
  const PackAnimRec& a = as.anims[animIndex];
  if (a.frameCount == 0) return;
  const PackFrameRect& f = as.animFrames(a)[currentFrame()];
  sr->SetFrame(a.texture, f.x, f.y, f.w, f.h);
}

void SpriteAnimator::Start() {
  if (playOnStart && !clip.isNull()) PlayGuid(clip);
}

void SpriteAnimator::Update() {
  if (!playing) return;
  const PackAssets& as = GfxAssets::assets();
  if (animIndex >= as.animCount) return;
  clock += Time::deltaTime() * speed;
  const PackAnimRec& a = as.anims[animIndex];
  if (!a.loop && a.frameCount && static_cast<u32>(clock * static_cast<Scalar>(a.fps)) >= a.frameCount) {
    playing = false;
    finished = true;
  }
  ApplyFrame();
}

// ---- Tilemap ----------------------------------------------------------------------------------------------------
const PackTilemapRec* Tilemap::record() const { return GfxAssets::assets().findTilemap(map); }
u32 Tilemap::width() const { const PackTilemapRec* r = record(); return r ? r->width : 0; }
u32 Tilemap::height() const { const PackTilemapRec* r = record(); return r ? r->height : 0; }
u32 Tilemap::tileSize() const { const PackTilemapRec* r = record(); return r ? r->tileSize : 0; }
Vector2 Tilemap::origin() const {
  Vector3 p = transform()->position();
  return Vector2(p.x, p.y);
}
u16 Tilemap::TileAt(i32 tx, i32 ty) const {
  const PackTilemapRec* r = record();
  if (!r || tx < 0 || ty < 0 || static_cast<u32>(tx) >= r->width || static_cast<u32>(ty) >= r->height) return 0xFFFF;
  return GfxAssets::assets().tilemapTiles(*r)[static_cast<u32>(ty) * r->width + static_cast<u32>(tx)];
}
u8 Tilemap::TileFlags(u16 tile) const {
  const PackTilemapRec* r = record();
  if (!r || tile == 0xFFFF || tile >= r->solidCount) return 0;
  return GfxAssets::assets().tilemapSolid(*r)[tile];
}
bool Tilemap::IsSolidTile(u16 tile) const { return (TileFlags(tile) & PackTileFlag::kSolid) != 0; }
bool Tilemap::IsSolidCell(i32 tx, i32 ty, bool outside) const {
  const PackTilemapRec* r = record();
  if (!r) return false;
  if (tx < 0 || ty < 0 || static_cast<u32>(tx) >= r->width || static_cast<u32>(ty) >= r->height) return outside;
  return IsSolidTile(TileAt(tx, ty));
}

// ---- Physics2D layer matrix ---------------------------------------------------------------------------------------
namespace {
u32 g_layerMasks[Physics2D::kLayerCount];
bool g_layerInit = false;
void initMatrix() {
  if (g_layerInit) return;
  for (u32& m : g_layerMasks) m = 0xFFFFFFFFu;
  g_layerInit = true;
}
}  // namespace

void Physics2D::resetMatrix() {
  g_layerInit = false;
  initMatrix();
}
void Physics2D::IgnoreLayerCollision(u32 a, u32 b, bool ignore) {
  if (a >= kLayerCount || b >= kLayerCount) return;
  initMatrix();
  if (ignore) { g_layerMasks[a] &= ~(1u << b); g_layerMasks[b] &= ~(1u << a); }
  else { g_layerMasks[a] |= 1u << b; g_layerMasks[b] |= 1u << a; }
}
bool Physics2D::GetIgnoreLayerCollision(u32 a, u32 b) {
  if (a >= kLayerCount || b >= kLayerCount) return false;
  initMatrix();
  return (g_layerMasks[a] & (1u << b)) == 0;
}
void Physics2D::loadFromPack(const PackView* pack) {
  resetMatrix();
  if (!pack) return;
  const PackPhysics2D* ph = pack->records<PackPhysics2D>(pack_section::PH2D);
  if (!ph) return;
  for (u32 i = 0; i < kLayerCount; ++i) g_layerMasks[i] = ph->masks[i];
}
bool Physics2D::layersCollide(const GameObject* a, const GameObject* b) {
  if (!a || !b) return true;
  initMatrix();
  return (g_layerMasks[a->layer & 31u] & (1u << (b->layer & 31u))) != 0;
}

// ---- BoxCollider2D ----------------------------------------------------------------------------------------------
Rect BoxCollider2D::bounds() const {
  const Transform* t = transform();
  Vector3 p = t->position(), s = t->lossyScale();
  const Scalar sx = absS(s.x), sy = absS(s.y);
  return Rect(p.x + offsetX * sx, p.y + offsetY * sy, sizeX * sx, sizeY * sy);
}

namespace {

constexpr Scalar kLandTol = 0.5f;  // a box this far inside a surface still counts as having been above it
constexpr Scalar kMaxPush = 32.0f;  // deepest overlap with a slope that is pushed out (the box is not tunnelling further)

// Cell flags with the "outside" rule. Cells outside the map are solid walls when `outside` is set.
u8 cellFlags(const Tilemap& tm, i32 tx, i32 ty, bool outside) {
  const PackTilemapRec* r = tm.record();
  if (!r) return 0;
  if (tx < 0 || ty < 0 || static_cast<u32>(tx) >= r->width || static_cast<u32>(ty) >= r->height) return outside ? PackTileFlag::kSolid : 0;
  return tm.TileFlags(tm.TileAt(tx, ty));
}
u32 slopeOf(u8 f) { return (f & PackTileFlag::kSlopeMask) >> PackTileFlag::kSlopeShift; }
bool isFull(u8 f) { return (f & PackTileFlag::kSolid) != 0; }
bool isOneWay(u8 f) { return (f & PackTileFlag::kSolid) == 0 && (f & PackTileFlag::kOneWay) != 0 && slopeOf(f) == 0; }

// Tilemap geometry in world pixels.
struct Geo {
  const Tilemap* tm = nullptr;
  Vector2 o;
  Scalar tw = 0, th = 0;
  bool outside = true;
  i32 cellX(Scalar x) const { return static_cast<i32>(Mathf::Floor((x - o.x) / tw)); }
  i32 cellY(Scalar y) const { return static_cast<i32>(Mathf::Floor((y - o.y) / th)); }
};

// Calls fn(Geo&) for every enabled TilemapCollider2D whose layer collides with `mover`'s. fn returns true to stop.
template <class F>
void eachMap(World& world, const GameObject* mover, F fn) {
  bool done = false;
  world.forEach<TilemapCollider2D>([&](TilemapCollider2D& tc) {
    if (done || !tc.enabled()) return;
    GameObject* go = tc.gameObject();
    if (!go || !go->activeInHierarchy()) return;
    if (mover && !Physics2D::layersCollide(mover, go)) return;
    Tilemap* tm = go->GetComponent<Tilemap>();
    const PackTilemapRec* r = tm ? tm->record() : nullptr;
    if (!r || r->tileSize == 0) return;
    Geo g;
    g.tm = tm;
    g.o = tm->origin();
    const Vector3 sc = tm->transform()->lossyScale();
    const Scalar ts = static_cast<Scalar>(r->tileSize);
    g.tw = ts * absS(sc.x);
    g.th = ts * absS(sc.y);
    g.outside = tc.solidOutside;
    done = fn(g);
  });
}

// Tile range of `box` inside `g`: true if any full solid cell overlaps. Fills the min/max solid cell hit.
struct SolidHit { i32 minX = 0, maxX = 0, minY = 0, maxY = 0; };

bool solidOverlap(const Geo& g, const Rect& box, SolidHit* hit) {
  const i32 x0 = g.cellX(box.x);
  const i32 x1 = g.cellX(box.x + box.width - kEps);
  const i32 y0 = g.cellY(box.y);
  const i32 y1 = g.cellY(box.y + box.height - kEps);
  bool any = false;
  for (i32 y = y0; y <= y1; ++y)
    for (i32 x = x0; x <= x1; ++x) {
      if (!isFull(cellFlags(*g.tm, x, y, g.outside))) continue;
      if (!any) { if (hit) { hit->minX = hit->maxX = x; hit->minY = hit->maxY = y; } any = true; }
      else if (hit) {
        if (x < hit->minX) hit->minX = x;
        if (x > hit->maxX) hit->maxX = x;
        if (y < hit->minY) hit->minY = y;
        if (y > hit->maxY) hit->maxY = y;
      }
    }
  return any;
}

// First tilemap collider whose full solid cells overlap `box`.
bool anySolid(World& world, const GameObject* mover, const Rect& box, Geo* geoOut, SolidHit* hit) {
  bool found = false;
  eachMap(world, mover, [&](Geo& g) {
    if (!solidOverlap(g, box, hit)) return false;
    found = true;
    if (geoOut) *geoOut = g;
    return true;
  });
  return found;
}

// One-way landing: the highest top edge (smallest y) of a one-way cell under `box` that the box was above at
// `oldBottom`. Cells at or above `ignoreTop` are skipped while a drop-through is active.
bool oneWayTop(World& world, const GameObject* mover, const Rect& box, Scalar oldBottom, bool dropping, Scalar dropY, Scalar* topOut) {
  bool found = false;
  Scalar best = 0;
  eachMap(world, mover, [&](Geo& g) {
    const i32 x0 = g.cellX(box.x), x1 = g.cellX(box.x + box.width - kEps);
    const i32 y0 = g.cellY(box.y), y1 = g.cellY(box.y + box.height - kEps);
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x) {
        if (!isOneWay(cellFlags(*g.tm, x, y, false))) continue;
        const Scalar top = g.o.y + static_cast<Scalar>(y) * g.th;
        if (oldBottom > top + kEps) continue;
        if (dropping && top <= dropY + kEps) continue;
        if (!found || top < best) { best = top; found = true; }
      }
    return false;
  });
  if (found) *topOut = best;
  return found;
}

// Slope surface under the box. The box rests on the highest surface under its bottom edge (sampled at the left
// edge, the middle and the right edge), so a box walking up a slope is carried by its leading corner and never
// has a corner inside the next tile. above: highest surface at or above the feet; below: highest one under them.
struct SlopeProbe {
  bool hasAbove = false, hasBelow = false;
  Scalar yAbove = 0, yBelow = 0;
};

SlopeProbe probeSlope(World& world, const GameObject* mover, const Rect& box) {
  SlopeProbe sp;
  const Scalar bottom = box.y + box.height;
  const Scalar xs[3] = {box.x + kEps, box.x + box.width * 0.5f, box.x + box.width - kEps};
  eachMap(world, mover, [&](Geo& g) {
    const i32 rb = g.cellY(bottom);
    for (const Scalar footX : xs) {
      const i32 tx = g.cellX(footX);
      for (i32 ty = rb - 1; ty <= rb + 1; ++ty) {
        const u32 sl = slopeOf(cellFlags(*g.tm, tx, ty, false));
        if (!sl) continue;
        i32 hl, hr;
        packSlopeHeights(sl, &hl, &hr);
        Scalar u = (footX - (g.o.x + static_cast<Scalar>(tx) * g.tw)) / g.tw;
        u = u < 0 ? 0 : (u > 1 ? 1 : u);
        const Scalar h = (static_cast<Scalar>(hl) + static_cast<Scalar>(hr - hl) * u) * (1.0f / 16.0f);
        const Scalar y = g.o.y + static_cast<Scalar>(ty) * g.th + g.th * (1.0f - h);
        if (y <= bottom + kEps) {
          if (!sp.hasAbove || y < sp.yAbove) { sp.hasAbove = true; sp.yAbove = y; }
        } else if (!sp.hasBelow || y < sp.yBelow) { sp.hasBelow = true; sp.yBelow = y; }
      }
    }
    return false;
  });
  return sp;
}

// Full solid or one-way tile directly under the box.
bool supportedByTiles(World& world, const GameObject* mover, const Rect& b, bool dropping, Scalar dropY) {
  Rect probe = b;
  probe.y += 0.25f;
  if (anySolid(world, mover, probe, nullptr, nullptr)) return true;
  Scalar top;
  return oneWayTop(world, mover, probe, b.y + b.height, dropping, dropY, &top);
}

bool isGroundedAt(World& world, const GameObject* mover, const Rect& b, bool dropping, Scalar dropY) {
  Rect probe = b;
  probe.y += 0.25f;
  if (anySolid(world, mover, probe, nullptr, nullptr)) return true;
  Scalar top;
  if (oneWayTop(world, mover, probe, b.y + b.height, dropping, dropY, &top)) return true;
  const SlopeProbe sp = probeSlope(world, mover, b);
  const Scalar bottom = b.y + b.height;
  if (sp.hasAbove && bottom - sp.yAbove <= 0.3f) return true;
  return sp.hasBelow && sp.yBelow - bottom <= 0.3f;
}

}  // namespace

bool BoxCollider2D::CheckGround() const {
  World* w = World::active();
  if (!w) return false;
  return isGroundedAt(*w, gameObject(), bounds(), dropping, dropY);
}

bool BoxCollider2D::DropThrough() {
  World* w = World::active();
  if (!w) return false;
  const Rect b = bounds();
  Scalar top;
  Rect probe = b;
  probe.y += 0.25f;
  if (!oneWayTop(*w, gameObject(), probe, b.y + b.height, false, 0, &top)) return false;
  dropping = true;
  dropY = b.y + b.height;
  grounded = false;
  return true;
}

Move2DResult BoxCollider2D::Move(Scalar dx, Scalar dy) {
  Move2DResult res;
  World* w = World::active();
  Transform* t = transform();
  if (!w) return res;
  const GameObject* me = gameObject();
  Vector3 p = t->position();
  const Vector3 sc = t->lossyScale();
  const Scalar ox = offsetX * absS(sc.x), oy = offsetY * absS(sc.y);
  Rect b(p.x + ox, p.y + oy, sizeX * absS(sc.x), sizeY * absS(sc.y));

  if (autoDropThrough) {
    const VirtualGamepad& pad = Input::pad();
    if (pad.held.down && pad.pressed.south) DropThrough();
  }
  if (dropping && b.y + b.height > dropY + 1.0f) dropping = false;  // fell past the platform
  const bool wasGrounded = isGroundedAt(*w, me, b, dropping, dropY);

  // Raises the box by up to half a tile until it clears every full solid cell. Used to step from a slope onto
  // the level ground at its top.
  auto liftClear = [&](Rect& box, Scalar maxLift) {
    Rect r = box;
    for (Scalar l = 1; l <= maxLift; l += 1) {
      r.y = box.y - l;
      if (!anySolid(*w, me, r, nullptr, nullptr)) { box = r; return true; }
    }
    return false;
  };
  // Keeps the foot on the slope surface: pushes it out of a slope, and pulls it down onto one when it walked
  // off a slope step while grounded.
  auto settleSlope = [&](bool allowDown) {
    const Scalar bottom = b.y + b.height;
    const SlopeProbe sp = probeSlope(*w, me, b);
    Rect nb = b;
    if (sp.hasAbove && bottom > sp.yAbove && bottom - sp.yAbove <= kMaxPush) {
      nb.y = sp.yAbove - b.height;
    } else if (allowDown && !sp.hasAbove && sp.hasBelow && !supportedByTiles(*w, me, b, dropping, dropY)) {
      Geo g;
      eachMap(*w, me, [&](Geo& gg) { g = gg; return true; });
      const Scalar maxSnap = g.th > 0 ? g.th * 0.5f : 8.0f;
      if (sp.yBelow - bottom > maxSnap) return;
      nb.y = sp.yBelow - b.height;
    } else {
      return;
    }
    if (!anySolid(*w, me, nb, nullptr, nullptr)) b = nb;
  };

  // ---- horizontal
  if (dx != 0) {
    const Scalar mag = absS(dx);
    const u32 steps = static_cast<u32>(mag / kMaxSubStep) + 1;
    const Scalar step = dx / static_cast<Scalar>(steps);
    for (u32 i = 0; i < steps; ++i) {
      const Scalar before = b.x;
      const bool onSlope = probeSlope(*w, me, b).hasAbove;
      b.x += step;
      Geo g;
      SolidHit hit;
      if (!anySolid(*w, me, b, &g, &hit)) {
        if (wasGrounded) settleSlope(dy >= 0);
        continue;
      }
      if (onSlope) {
        Rect lifted = b;
        if (liftClear(lifted, g.th * 0.5f)) { b = lifted; if (wasGrounded) settleSlope(false); continue; }
      }
      (void)before;
      if (step > 0) { b.x = g.o.x + static_cast<Scalar>(hit.minX) * g.tw - b.width; res.hitRight = true; }
      else { b.x = g.o.x + static_cast<Scalar>(hit.maxX + 1) * g.tw; res.hitLeft = true; }
      break;
    }
  }
  // Airborne boxes still get pushed out of a slope they ran into; grounded ones were settled above.
  if (!wasGrounded) settleSlope(false);

  // ---- vertical
  if (dy != 0) {
    const Scalar mag = absS(dy);
    const u32 steps = static_cast<u32>(mag / kMaxSubStep) + 1;
    const Scalar step = dy / static_cast<Scalar>(steps);
    for (u32 i = 0; i < steps; ++i) {
      const Scalar oldBottom = b.y + b.height;
      b.y += step;
      Geo g;
      SolidHit hit;
      const bool full = anySolid(*w, me, b, &g, &hit);
      if (step > 0) {
        // Lands on the highest of: the top of a full block, a one-way top the box was above, a slope surface.
        // A slope surface can be higher than the block that carries it, so all three are compared.
        Scalar land = 0;
        bool landed = false;
        if (full) { land = g.o.y + static_cast<Scalar>(hit.minY) * g.th; landed = true; }
        Scalar ow = 0;
        if (oneWayTop(*w, me, b, oldBottom, dropping, dropY, &ow) && (!landed || ow < land)) { land = ow; landed = true; }
        const SlopeProbe sp = probeSlope(*w, me, b);
        if (sp.hasAbove && oldBottom <= sp.yAbove + kLandTol && (!landed || sp.yAbove < land)) { land = sp.yAbove; landed = true; }
        if (landed) { b.y = land - b.height; res.hitDown = true; break; }
      } else if (full) {
        b.y = g.o.y + static_cast<Scalar>(hit.maxY + 1) * g.th;
        res.hitUp = true;
        break;
      }
    }
  }

  t->setPosition(Vector3(b.x - ox, b.y - oy, p.z));
  if (dropping && b.y + b.height > dropY + 1.0f) dropping = false;
  grounded = isGroundedAt(*w, me, b, dropping, dropY);
  return res;
}

// ---- Camera2D ---------------------------------------------------------------------------------------------------
namespace {
Scalar g_defaultViewW = 320, g_defaultViewH = 240;
}
void Camera2D::setDefaultViewSize(Scalar w, Scalar h) {
  if (w > 0 && h > 0) { g_defaultViewW = w; g_defaultViewH = h; }
}
Scalar Camera2D::effectiveViewWidth() const { return viewWidth > 0 ? viewWidth : g_defaultViewW; }
Scalar Camera2D::effectiveViewHeight() const { return viewHeight > 0 ? viewHeight : g_defaultViewH; }

Camera2D* Camera2D::main() {
  World* w = World::active();
  if (!w) return nullptr;
  Camera2D* found = nullptr;
  w->forEach<Camera2D>([&](Camera2D& c) {
    GameObject* go = c.gameObject();
    if (!found && c.enabled() && go && go->activeInHierarchy()) found = &c;
  });
  return found;
}

void Camera2D::UpdateView() {
  World* w = World::active();
  if (!w) return;
  const Scalar vw = effectiveViewWidth(), vh = effectiveViewHeight();
  const GameObject* tgt = target.isNull() ? nullptr : w->findByGuid(target);
  if (tgt && tgt->alive && !tgt->dying) {
    const Vector3 tp = tgt->transform.position();
    const Scalar px = tp.x + targetOffsetX, py = tp.y + targetOffsetY;
    const Scalar zx0 = viewX + (vw - deadZoneW) * 0.5f, zx1 = zx0 + deadZoneW;
    const Scalar zy0 = viewY + (vh - deadZoneH) * 0.5f, zy1 = zy0 + deadZoneH;
    if (px < zx0) viewX += px - zx0;
    else if (px > zx1) viewX += px - zx1;
    if (py < zy0) viewY += py - zy0;
    else if (py > zy1) viewY += py - zy1;
  }
  if (useBounds) {
    Scalar bx = boundsX, by = boundsY, bw = boundsW, bh = boundsH;
    const GameObject* bo = boundsTilemap.isNull() ? nullptr : w->findByGuid(boundsTilemap);
    const Tilemap* tm = bo ? bo->GetComponent<Tilemap>() : nullptr;
    if (tm && tm->record()) {
      const Vector2 o = tm->origin();
      const Vector3 sc = bo->transform.lossyScale();
      bx = o.x;
      by = o.y;
      bw = static_cast<Scalar>(tm->width() * tm->tileSize()) * absS(sc.x);
      bh = static_cast<Scalar>(tm->height() * tm->tileSize()) * absS(sc.y);
    }
    // A room smaller than the view is centered on it.
    viewX = bw <= vw ? bx + (bw - vw) * 0.5f : (viewX < bx ? bx : (viewX > bx + bw - vw ? bx + bw - vw : viewX));
    viewY = bh <= vh ? by + (bh - vh) * 0.5f : (viewY < by ? by : (viewY > by + bh - vh ? by + bh - vh : viewY));
  }
  if (pixelSnap) {
    viewX = Mathf::Floor(viewX + 0.5f);
    viewY = Mathf::Floor(viewY + 0.5f);
  }
}

// ---- UI ---------------------------------------------------------------------------------------------------------
void UIText::SetText(const char* s) {
  u32 i = 0;
  if (s) for (; s[i] && i + 1 < kMaxText; ++i) text[i] = s[i];
  text[i] = 0;
}

void UIText::SetInt(const char* prefix, i32 value) {
  u32 n = 0;
  if (prefix) for (; prefix[n] && n + 12 < kMaxText; ++n) text[n] = prefix[n];
  char digits[12];
  u32 d = 0;
  u32 mag = value < 0 ? static_cast<u32>(-(value + 1)) + 1u : static_cast<u32>(value);
  do { digits[d++] = static_cast<char>('0' + mag % 10); mag /= 10; } while (mag && d < 11);
  if (value < 0 && n + 1 < kMaxText) text[n++] = '-';
  while (d && n + 1 < kMaxText) text[n++] = digits[--d];
  text[n] = 0;
}

Rect UI::resolveRect(i32 anchor, i32 x, i32 y, i32 w, i32 h, i32 viewW, i32 viewH) {
  if (anchor < 0 || anchor >= UIAnchor::kCount) anchor = UIAnchor::TopLeft;
  const i32 col = anchor % 3, row = anchor / 3;  // 0 left / top, 1 centre, 2 right / bottom
  const i32 ax = col == 0 ? 0 : (col == 1 ? viewW / 2 : viewW), ay = row == 0 ? 0 : (row == 1 ? viewH / 2 : viewH);
  const i32 px = col == 0 ? 0 : (col == 1 ? w / 2 : w), py = row == 0 ? 0 : (row == 1 ? h / 2 : h);
  return Rect(static_cast<Scalar>(ax + x - px), static_cast<Scalar>(ay + y - py), static_cast<Scalar>(w), static_cast<Scalar>(h));
}

i32 UI::textWidth(const PackFontRec& f, const char* text) {
  if (!text) return 0;
  const u32 count = f.charCount < kPackFontMaxChars ? f.charCount : kPackFontMaxChars;
  i32 best = 0, cur = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
    if (*p == '\n') { if (cur > best) best = cur; cur = 0; continue; }
    if (*p < f.firstChar || static_cast<u32>(*p - f.firstChar) >= count) continue;
    cur += f.advance[*p - f.firstChar];
  }
  return cur > best ? cur : best;
}

i32 UI::textHeight(const PackFontRec& f, const char* text) {
  i32 lines = 1;
  if (text) for (const char* p = text; *p; ++p) if (*p == '\n') ++lines;
  const i32 lh = f.lineHeight ? f.lineHeight : f.glyphH;
  return f.glyphH + (lines - 1) * lh;
}

// F6 (D1): UIButton::isFocused, UI::update (focus, navigation, sliders, toggles, layout) and UI::draw moved to ui_runtime.cpp.

// ---- 2D contact events ------------------------------------------------------------------------------------------
// Each fixed step lists the overlapping pairs and diffs them with the previous step: new pairs send Enter, pairs that
// disappeared send Exit. Triggers pair with solid boxes (OnTriggerEnter/Exit and the *2D names, on both objects);
// two overlapping solid boxes send OnCollisionEnter2D/Exit2D (solid boxes do not push each other, tiles do).
// Fixed-size tables, no heap; pairs are identified by object index + generation so reused slots do not confuse them.
namespace {
struct Pair2D {
  u32 aIdx, aGen, bIdx, bGen;
  bool trigger;
  bool operator==(const Pair2D& o) const {
    return aIdx == o.aIdx && aGen == o.aGen && bIdx == o.bIdx && bGen == o.bGen && trigger == o.trigger;
  }
};
constexpr u32 kMaxPairs2D = 256;
Pair2D g_prevPairs[kMaxPairs2D], g_curPairs[kMaxPairs2D];
u32 g_prevCount = 0, g_curCount = 0;

bool hasPair(const Pair2D* list, u32 n, const Pair2D& p) {
  for (u32 i = 0; i < n; ++i)
    if (list[i] == p) return true;
  return false;
}

// F6 (A2): the Collision2D a solid pair reports to `self`: contact point = middle of the overlap, normal = the axis of least
// penetration pointing from the other box toward `self`, relativeVelocity = self's Rigidbody2D velocity minus the other's.
Collision2D makeCollision2D(GameObject* self, GameObject* other) {
  Collision2D c;
  c.gameObject = other;
  BoxCollider2D* bs = self->GetComponent<BoxCollider2D>();
  BoxCollider2D* bo = other->GetComponent<BoxCollider2D>();
  if (bs && bo) {
    const Rect a = bs->bounds(), b = bo->bounds();
    const Scalar ox = Mathf::Min(a.x + a.width, b.x + b.width) - Mathf::Max(a.x, b.x);
    const Scalar oy = Mathf::Min(a.y + a.height, b.y + b.height) - Mathf::Max(a.y, b.y);
    const Scalar acx = a.x + a.width * 0.5f, acy = a.y + a.height * 0.5f, bcx = b.x + b.width * 0.5f, bcy = b.y + b.height * 0.5f;
    c.point = Vector2(Mathf::Max(a.x, b.x) + ox * 0.5f, Mathf::Max(a.y, b.y) + oy * 0.5f);
    if (ox < oy) c.normal = Vector2(acx >= bcx ? 1.0f : -1.0f, 0);
    else c.normal = Vector2(0, acy >= bcy ? 1.0f : -1.0f);
  }
  Rigidbody2D* rs = self->GetComponent<Rigidbody2D>();
  Rigidbody2D* ro = other->GetComponent<Rigidbody2D>();
  Vector2 vs(rs ? rs->velocityX : 0.0f, rs ? rs->velocityY : 0.0f), vo(ro ? ro->velocityX : 0.0f, ro ? ro->velocityY : 0.0f);
  c.relativeVelocity = vs - vo;
  return c;
}

// kind: 0 enter, 1 stay, 2 exit. Stay is new in F6: triggers send OnTriggerStay + OnTriggerStay2D, solid pairs the Collision2D Stay.
// Solid pairs send both forms of Enter / Exit: the old OnCollisionEnter2D(GameObject*) and the Collision2D overload.
void sendPair(World& world, const Pair2D& p, int kind) {
  GameObject* a = world.objectAt(p.aIdx);
  GameObject* b = world.objectAt(p.bIdx);
  if (!a || !b || a->generation != p.aGen || b->generation != p.bGen) return;  // one side is gone: no callback
  Ref<GameObject> ra = a->ref(), rb = b->ref();
  using C = World::Contact;
  if (p.trigger) {
    const C plain = kind == 0 ? C::TriggerEnter : (kind == 1 ? C::TriggerStay : C::TriggerExit);
    const C twoD = kind == 0 ? C::TriggerEnter2D : (kind == 1 ? C::TriggerStay2D : C::TriggerExit2D);
    if (ra) { world.sendContact(ra.get(), b, plain); }
    if (ra) { world.sendContact(ra.get(), b, twoD); }
    if (ra && rb) { world.sendContact(rb.get(), ra.get(), plain); }
    if (ra && rb) { world.sendContact(rb.get(), ra.get(), twoD); }
    return;
  }
  const World::CollisionKind ck = kind == 0 ? World::CollisionKind::Enter : (kind == 1 ? World::CollisionKind::Stay : World::CollisionKind::Exit);
  if (kind != 1) {
    const C coll = kind == 0 ? C::CollisionEnter2D : C::CollisionExit2D;
    if (ra) world.sendContact(ra.get(), b, coll);
    if (ra && rb) world.sendContact(rb.get(), ra.get(), coll);
  }
  if (ra && rb) {
    const Collision2D ca = makeCollision2D(ra.get(), rb.get());
    world.sendCollision2D(ra.get(), ca, ck);
    if (ra && rb) world.sendCollision2D(rb.get(), makeCollision2D(rb.get(), ra.get()), ck);
  }
}
}  // namespace

void Gameplay2D::resetTriggerMemory() {
  g_prevCount = 0;
  physics3d::resetState();  // F6 a2: the 3D contact pairs and kinematic history go with the 2D trigger memory
}

void Gameplay2D::fixedStep(World& world) {
  Rigidbody2DSim::fixedStep(world);  // F4: bodies move first, so this step's trigger pairs see the new positions
  g_curCount = 0;
  auto add = [](GameObject* a, GameObject* b, bool trigger) {
    if (g_curCount < kMaxPairs2D) g_curPairs[g_curCount++] = {a->index, a->generation, b->index, b->generation, trigger};
  };
  const ComponentPool* p = world.pool<BoxCollider2D>();
  if (p && p->liveCount >= 2) {
    struct Mover { GameObject* go; Rect box; };
    Mover movers[32];
    u32 n = 0;
    world.forEach<BoxCollider2D>([&](BoxCollider2D& b) {
      GameObject* go = b.gameObject();
      if (b.isTrigger || n >= 32 || !b.enabled() || !go || !go->activeInHierarchy()) return;
      movers[n++] = {go, b.bounds()};
    });
    if (n) {
      world.forEach<BoxCollider2D>([&](BoxCollider2D& b) {
        GameObject* trig = b.gameObject();
        if (!b.isTrigger || !b.enabled() || !trig || !trig->activeInHierarchy()) return;
        const Rect tb = b.bounds();
        for (u32 i = 0; i < n; ++i)
          if (movers[i].box.Overlaps(tb) && Physics2D::layersCollide(movers[i].go, trig)) add(trig, movers[i].go, true);
      });
      for (u32 i = 0; i < n; ++i)
        for (u32 j = i + 1; j < n; ++j)
          if (movers[i].box.Overlaps(movers[j].box) && Physics2D::layersCollide(movers[i].go, movers[j].go))
            add(movers[i].go, movers[j].go, false);
    }
  }
  // The callbacks may create or destroy objects but never touch these tables: work on a local copy of the counts.
  Pair2D cur[kMaxPairs2D], prev[kMaxPairs2D];
  const u32 curN = g_curCount, prevN = g_prevCount;
  for (u32 i = 0; i < curN; ++i) cur[i] = g_curPairs[i];
  for (u32 i = 0; i < prevN; ++i) prev[i] = g_prevPairs[i];
  for (u32 i = 0; i < curN; ++i) g_prevPairs[i] = cur[i];
  g_prevCount = curN;
  for (u32 i = 0; i < curN; ++i)
    sendPair(world, cur[i], hasPair(prev, prevN, cur[i]) ? 1 : 0);
  for (u32 i = 0; i < prevN; ++i)
    if (!hasPair(cur, curN, prev[i])) sendPair(world, prev[i], 2);
}

// ---- drawing ----------------------------------------------------------------------------------------------------
void renderGameplay2D(World& world, IRenderer& r) {
  if (!GfxAssets::bound()) return;
  const PackAssets& as = GfxAssets::assets();
  const Scalar vw = static_cast<Scalar>(r.width()), vh = static_cast<Scalar>(r.height());
  Camera2D::setDefaultViewSize(vw, vh);
  // World-space drawing is scrolled by the camera view; the UI below is not.
  Scalar camX = 0, camY = 0;
  if (const Camera2D* cam = Camera2D::main()) { camX = cam->viewX; camY = cam->viewY; }

  world.forEach<Tilemap>([&](Tilemap& tm) {
    GameObject* go = tm.gameObject();
    const PackTilemapRec* rec = tm.record();
    if (!go || !rec || !tm.enabled() || !go->activeInHierarchy() || rec->tilesetTexture >= as.textureCount) return;
    const u32 handle = GfxAssets::handle(rec->tilesetTexture);
    if (!handle || rec->tileSize == 0) return;
    const u32 cols = as.textures[rec->tilesetTexture].width / rec->tileSize;
    if (!cols) return;
    const Vector2 o = tm.origin();
    const Vector3 sc = go->transform.lossyScale();
    const Scalar ts = static_cast<Scalar>(rec->tileSize), tw = ts * absS(sc.x), th = ts * absS(sc.y);
    if (tw <= 0 || th <= 0) return;
    const u16* tiles = as.tilemapTiles(*rec);
    const u32 key = spriteSortKey(tm.sortingOrder);
    // Only the tiles inside the view are visited.
    i32 cx0 = static_cast<i32>(Mathf::Floor((camX - o.x) / tw)), cx1 = static_cast<i32>(Mathf::Floor((camX + vw - o.x) / tw));
    i32 cy0 = static_cast<i32>(Mathf::Floor((camY - o.y) / th)), cy1 = static_cast<i32>(Mathf::Floor((camY + vh - o.y) / th));
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= static_cast<i32>(rec->width)) cx1 = static_cast<i32>(rec->width) - 1;
    if (cy1 >= static_cast<i32>(rec->height)) cy1 = static_cast<i32>(rec->height) - 1;
    for (i32 y = cy0; y <= cy1; ++y) {
      const Scalar dy = o.y + static_cast<Scalar>(y) * th - camY;
      if (dy + th <= 0 || dy >= vh) continue;
      for (i32 x = cx0; x <= cx1; ++x) {
        const u16 t = tiles[static_cast<u32>(y) * rec->width + static_cast<u32>(x)];
        if (t == 0xFFFF) continue;
        const Scalar dx = o.x + static_cast<Scalar>(x) * tw - camX;
        if (dx + tw <= 0 || dx >= vw) continue;
        r.drawSprite(handle, Rect(static_cast<Scalar>((t % cols) * rec->tileSize), static_cast<Scalar>((t / cols) * rec->tileSize), ts, ts),
                     Rect(dx, dy, tw, th), tm.tint, key);
      }
    }
  });

  world.forEach<SpriteRenderer>([&](SpriteRenderer& sr) {
    GameObject* go = sr.gameObject();
    if (!go || !sr.enabled() || !go->activeInHierarchy()) return;
    const u32 ti = sr.overrideSrc ? sr.overrideTexture : GfxAssets::textureIndex(sr.texture);
    if (ti >= as.textureCount) return;
    const u32 handle = GfxAssets::handle(ti);
    if (!handle) return;
    Scalar sx, sy, sw, sh;
    if (sr.overrideSrc) { sx = static_cast<Scalar>(sr.curX); sy = static_cast<Scalar>(sr.curY); sw = static_cast<Scalar>(sr.curW); sh = static_cast<Scalar>(sr.curH); }
    else if (sr.srcW > 0 && sr.srcH > 0) { sx = static_cast<Scalar>(sr.srcX); sy = static_cast<Scalar>(sr.srcY); sw = static_cast<Scalar>(sr.srcW); sh = static_cast<Scalar>(sr.srcH); }
    else { sx = sy = 0; sw = static_cast<Scalar>(as.textures[ti].width); sh = static_cast<Scalar>(as.textures[ti].height); }
    const Vector3 pos = go->transform.position(), sc = go->transform.lossyScale();
    const Scalar dw = sw * absS(sc.x), dh = sh * absS(sc.y);
    const Rect dst(pos.x - camX - static_cast<Scalar>(sr.pivotX) * absS(sc.x), pos.y - camY - static_cast<Scalar>(sr.pivotY) * absS(sc.y), dw, dh);
    if (dst.x + dw <= 0 || dst.y + dh <= 0 || dst.x >= vw || dst.y >= vh) return;
    const Rect src = sr.flipX ? Rect(sx + sw, sy, -sw, sh) : Rect(sx, sy, sw, sh);
    r.drawSprite(handle, src, dst, sr.color, spriteSortKey(sr.sortingOrder));
  });

  UI::draw(world, r);  // F6 (D1): images (with fill), buttons, text, Canvas scaling (ui_runtime.cpp)
}

}  // namespace rensga
