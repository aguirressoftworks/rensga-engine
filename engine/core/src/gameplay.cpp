// F2.05 Engine 5: gameplay framework implementation. No heap use; everything runs on the World's pools.
#include "rensga/core/gameplay.h"

#include <string.h>

#include "rensga/core/input.h"
#include "rensga/core/time.h"
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

UI::ClickHandler g_clickHandler = nullptr;
u32 g_focusSlot = kInvalidIndex;

Scalar absS(Scalar v) { return v < 0 ? -v : v; }
}  // namespace

// ---- GfxAssets --------------------------------------------------------------------------------------------------
void GfxAssets::bind(const PackView* pack, const PackAssets* assets, const u32* handles, u32 count) {
  g_pack = pack;
  g_assets = assets ? *assets : PackAssets();
  g_handleCount = count < kMaxTextures ? count : kMaxTextures;
  for (u32 i = 0; i < g_handleCount; ++i) g_handles[i] = handles ? handles[i] : 0;
  g_bound = assets != nullptr;
  g_focusSlot = kInvalidIndex;
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
static u32 uiSortKey(i32 order) { return kUiSortBase + clampOrder(order); }

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
bool Tilemap::IsSolidTile(u16 tile) const {
  const PackTilemapRec* r = record();
  if (!r || tile == 0xFFFF || tile >= r->solidCount) return false;
  return GfxAssets::assets().tilemapSolid(*r)[tile] != 0;
}
bool Tilemap::IsSolidCell(i32 tx, i32 ty, bool outside) const {
  const PackTilemapRec* r = record();
  if (!r) return false;
  if (tx < 0 || ty < 0 || static_cast<u32>(tx) >= r->width || static_cast<u32>(ty) >= r->height) return outside;
  return IsSolidTile(TileAt(tx, ty));
}

// ---- BoxCollider2D ----------------------------------------------------------------------------------------------
Rect BoxCollider2D::bounds() const {
  const Transform* t = transform();
  Vector3 p = t->position(), s = t->lossyScale();
  const Scalar sx = absS(s.x), sy = absS(s.y);
  return Rect(p.x + offsetX * sx, p.y + offsetY * sy, sizeX * sx, sizeY * sy);
}

namespace {

// Tile range of `box` inside `tm`: true if any solid cell overlaps. Fills the min/max solid cell hit.
struct SolidHit { i32 minX = 0, maxX = 0, minY = 0, maxY = 0; };

bool solidOverlap(const Tilemap& tm, bool outside, const Rect& box, SolidHit* hit) {
  const PackTilemapRec* r = tm.record();
  if (!r || r->tileSize == 0) return false;
  const Vector2 o = tm.origin();
  const Vector3 sc = tm.transform()->lossyScale();
  const Scalar ts = static_cast<Scalar>(r->tileSize);
  const Scalar tw = ts * absS(sc.x), th = ts * absS(sc.y);
  const i32 x0 = static_cast<i32>(Mathf::Floor((box.x - o.x) / tw));
  const i32 x1 = static_cast<i32>(Mathf::Floor((box.x + box.width - kEps - o.x) / tw));
  const i32 y0 = static_cast<i32>(Mathf::Floor((box.y - o.y) / th));
  const i32 y1 = static_cast<i32>(Mathf::Floor((box.y + box.height - kEps - o.y) / th));
  bool any = false;
  for (i32 y = y0; y <= y1; ++y)
    for (i32 x = x0; x <= x1; ++x) {
      if (!tm.IsSolidCell(x, y, outside)) continue;
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

// First tilemap collider whose solid cells overlap `box`.
bool anySolid(World& world, const Rect& box, const Tilemap** mapOut, SolidHit* hit) {
  bool found = false;
  world.forEach<TilemapCollider>([&](TilemapCollider& tc) {
    if (found || !tc.enabled()) return;
    GameObject* go = tc.gameObject();
    if (!go || !go->activeInHierarchy()) return;
    Tilemap* tm = go->GetComponent<Tilemap>();
    if (!tm) return;
    if (solidOverlap(*tm, tc.solidOutside, box, hit)) {
      found = true;
      if (mapOut) *mapOut = tm;
    }
  });
  return found;
}

}  // namespace

bool BoxCollider2D::CheckGround() const {
  World* w = World::active();
  if (!w) return false;
  Rect b = bounds();
  b.y += 0.25f;
  return anySolid(*w, b, nullptr, nullptr);
}

Move2DResult BoxCollider2D::Move(Scalar dx, Scalar dy) {
  Move2DResult res;
  World* w = World::active();
  Transform* t = transform();
  if (!w) return res;
  Vector3 p = t->position();
  const Vector3 sc = t->lossyScale();
  const Scalar ox = offsetX * absS(sc.x), oy = offsetY * absS(sc.y);
  Rect b(p.x + ox, p.y + oy, sizeX * absS(sc.x), sizeY * absS(sc.y));

  auto resolveAxis = [&](Scalar total, bool horizontal) {
    if (total == 0) return;
    const Scalar mag = absS(total);
    const u32 steps = static_cast<u32>(mag / kMaxSubStep) + 1;
    const Scalar step = total / static_cast<Scalar>(steps);
    for (u32 i = 0; i < steps; ++i) {
      if (horizontal) b.x += step; else b.y += step;
      const Tilemap* tm = nullptr;
      SolidHit hit;
      if (!anySolid(*w, b, &tm, &hit)) continue;
      const Vector2 org = tm->origin();
      const Vector3 s = tm->transform()->lossyScale();
      const Scalar ts = static_cast<Scalar>(tm->tileSize());
      if (horizontal) {
        const Scalar tw = ts * absS(s.x);
        if (step > 0) { b.x = org.x + static_cast<Scalar>(hit.minX) * tw - b.width; res.hitRight = true; }
        else { b.x = org.x + static_cast<Scalar>(hit.maxX + 1) * tw; res.hitLeft = true; }
      } else {
        const Scalar th = ts * absS(s.y);
        if (step > 0) { b.y = org.y + static_cast<Scalar>(hit.minY) * th - b.height; res.hitDown = true; }
        else { b.y = org.y + static_cast<Scalar>(hit.maxY + 1) * th; res.hitUp = true; }
      }
      return;
    }
  };
  resolveAxis(dx, true);
  resolveAxis(dy, false);

  t->setPosition(Vector3(b.x - ox, b.y - oy, p.z));
  grounded = CheckGround();
  return res;
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

bool UIButton::isFocused() const {
  World* w = gameObject() ? gameObject()->world : nullptr;
  const ComponentPool* p = w ? w->pool<UIButton>() : nullptr;
  if (!p || g_focusSlot >= p->highWater) return false;
  return p->at(g_focusSlot) == static_cast<const void*>(this);
}

void UI::setClickHandler(ClickHandler handler) { g_clickHandler = handler; }
void UI::resetFocus() { g_focusSlot = kInvalidIndex; }

void UI::update(World& world) {
  const ComponentPool* p = world.pool<UIButton>();
  if (!p || p->liveCount == 0) return;
  auto usable = [&](u32 i) {
    if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) return false;
    const UIButton* b = static_cast<const UIButton*>(p->at(i));
    const GameObject* go = b->gameObject();
    return b->interactable && b->enabled() && go && go->activeInHierarchy();
  };
  if (g_focusSlot >= p->highWater || !usable(g_focusSlot)) {
    g_focusSlot = kInvalidIndex;
    for (u32 i = 0; i < p->highWater; ++i)
      if (usable(i)) { g_focusSlot = i; break; }
    if (g_focusSlot == kInvalidIndex) return;
  }
  const VirtualGamepad& pad = Input::pad();
  const bool next = pad.pressed.down || pad.pressed.right, prev = pad.pressed.up || pad.pressed.left;
  if (next || prev) {
    for (u32 n = 1; n <= p->highWater; ++n) {
      const u32 i = next ? (g_focusSlot + n) % p->highWater : (g_focusSlot + p->highWater - n % p->highWater) % p->highWater;
      if (usable(i)) { g_focusSlot = i; break; }
    }
  }
  if (pad.pressed.south) {
    GameObject* go = static_cast<UIButton*>(p->at(g_focusSlot))->gameObject();
    if (go) {
      Ref<GameObject> ref = go->ref();
      world.sendUIClick(go);
      if (g_clickHandler && ref) g_clickHandler(ref.get());
    }
  }
}

// ---- 2D trigger events ------------------------------------------------------------------------------------------
namespace {
struct TriggerMemory2D { u32 generation = 0; u32 mask = 0; };
constexpr u32 kMemory2D = 1024;
TriggerMemory2D g_memory2D[kMemory2D];
}  // namespace

void Gameplay2D::resetTriggerMemory() {
  for (TriggerMemory2D& m : g_memory2D) m = TriggerMemory2D();
}

void Gameplay2D::fixedStep(World& world) {
  const ComponentPool* p = world.pool<BoxCollider2D>();
  if (!p || p->liveCount < 2) return;
  struct Mover { GameObject* go; Rect box; };
  Mover movers[32];
  u32 n = 0;
  world.forEach<BoxCollider2D>([&](BoxCollider2D& b) {
    GameObject* go = b.gameObject();
    if (b.isTrigger || n >= 32 || !b.enabled() || !go || !go->activeInHierarchy()) return;
    movers[n++] = {go, b.bounds()};
  });
  if (!n) return;
  world.forEach<BoxCollider2D>([&](BoxCollider2D& b) {
    GameObject* trig = b.gameObject();
    if (!b.isTrigger || !b.enabled() || !trig || !trig->activeInHierarchy() || trig->index >= kMemory2D) return;
    const Rect tb = b.bounds();
    TriggerMemory2D& m = g_memory2D[trig->index];
    if (m.generation != trig->generation) { m.generation = trig->generation; m.mask = 0; }
    u32 now = 0;
    for (u32 i = 0; i < n; ++i)
      if (movers[i].box.Overlaps(tb)) now |= 1u << i;
    const u32 entered = now & ~m.mask;
    m.mask = now;
    for (u32 i = 0; i < n; ++i) {
      if (!(entered & (1u << i))) continue;
      Ref<GameObject> tr = trig->ref(), pl = movers[i].go->ref();
      if (tr) world.sendTriggerEnter(tr.get(), movers[i].go);
      if (tr && pl) world.sendTriggerEnter(pl.get(), tr.get());
    }
  });
}

// ---- drawing ----------------------------------------------------------------------------------------------------
void renderGameplay2D(World& world, IRenderer& r) {
  if (!GfxAssets::bound()) return;
  const PackAssets& as = GfxAssets::assets();
  const Scalar vw = static_cast<Scalar>(r.width()), vh = static_cast<Scalar>(r.height());

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
    const u16* tiles = as.tilemapTiles(*rec);
    const u32 key = spriteSortKey(tm.sortingOrder);
    for (u32 y = 0; y < rec->height; ++y) {
      const Scalar dy = o.y + static_cast<Scalar>(y) * th;
      if (dy + th <= 0 || dy >= vh) continue;
      for (u32 x = 0; x < rec->width; ++x) {
        const u16 t = tiles[y * rec->width + x];
        if (t == 0xFFFF) continue;
        const Scalar dx = o.x + static_cast<Scalar>(x) * tw;
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
    const Rect dst(pos.x - static_cast<Scalar>(sr.pivotX) * absS(sc.x), pos.y - static_cast<Scalar>(sr.pivotY) * absS(sc.y), dw, dh);
    if (dst.x + dw <= 0 || dst.y + dh <= 0 || dst.x >= vw || dst.y >= vh) return;
    const Rect src = sr.flipX ? Rect(sx + sw, sy, -sw, sh) : Rect(sx, sy, sw, sh);
    r.drawSprite(handle, src, dst, sr.color, spriteSortKey(sr.sortingOrder));
  });

  world.forEach<UIImage>([&](UIImage& im) {
    GameObject* go = im.gameObject();
    if (!go || !im.enabled() || !go->activeInHierarchy()) return;
    const Rect dst(static_cast<Scalar>(im.x), static_cast<Scalar>(im.y), static_cast<Scalar>(im.w), static_cast<Scalar>(im.h));
    const u32 ti = GfxAssets::textureIndex(im.texture);
    if (ti >= as.textureCount) { r.drawRect(dst, im.color, uiSortKey(im.sortingOrder)); return; }
    const u32 handle = GfxAssets::handle(ti);
    if (!handle) return;
    const Scalar sw = im.srcW > 0 ? static_cast<Scalar>(im.srcW) : static_cast<Scalar>(as.textures[ti].width);
    const Scalar sh = im.srcH > 0 ? static_cast<Scalar>(im.srcH) : static_cast<Scalar>(as.textures[ti].height);
    r.drawSprite(handle, Rect(static_cast<Scalar>(im.srcX), static_cast<Scalar>(im.srcY), sw, sh), dst, im.color, uiSortKey(im.sortingOrder));
  });

  world.forEach<UIButton>([&](UIButton& b) {
    GameObject* go = b.gameObject();
    if (!go || !b.enabled() || !go->activeInHierarchy()) return;
    Color32 c = b.isFocused() ? b.focusColor : b.normalColor;
    if (!b.interactable) c = Color32(c.r / 2, c.g / 2, c.b / 2, c.a);
    r.drawRect(Rect(static_cast<Scalar>(b.x), static_cast<Scalar>(b.y), static_cast<Scalar>(b.w), static_cast<Scalar>(b.h)), c, uiSortKey(b.sortingOrder));
  });

  world.forEach<UIText>([&](UIText& t) {
    GameObject* go = t.gameObject();
    if (!go || !t.enabled() || !go->activeInHierarchy() || !t.text[0] || as.fontCount == 0) return;
    const PackFontRec* font = t.font.isNull() ? &as.fonts[0] : as.findFont(t.font);
    if (!font) font = &as.fonts[0];
    if (font->texture >= as.textureCount) return;
    const u32 handle = GfxAssets::handle(font->texture);
    if (!handle) return;
    r.drawText(handle, *font, t.x, t.y, t.text, t.color, uiSortKey(t.sortingOrder) + 1);
  });
}

}  // namespace rensga
