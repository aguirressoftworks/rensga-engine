// F6.04 / F6.12 (D1): the UI runtime. One focus for buttons, sliders and toggles (the EventSystem), explicit and automatic spatial
// navigation, Canvas scaling, layout groups, slider fills and handles, toggle graphics, image fill, and the drawing of all of it.
//
// Rules this file keeps:
//  - No heap: fixed candidate tables (kMaxSelectable), everything lives on the World's pools.
//  - A scene without Canvas / Slider / Toggle / LayoutGroup / EventSystem behaves exactly as before F6 (the 2D determinism hash does
//    not change): the old reading-order focus is the fallback of the spatial navigation, and nothing here writes to a component
//    unless one of the new components asks for it.
//  - The elements keep their own anchor / x / y / w / h (the Transform is ignored, see gameplay.h). Layout groups, slider fills and
//    handles move other elements by writing those fields at run time.
#include "rensga/core/application.h"
#include "rensga/core/debug.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/input.h"
#include "rensga/core/time.h"
#include "rensga/core/ui_components.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {

UI::ClickHandler g_clickHandler = nullptr;
UI::CancelHandler g_cancelHandler = nullptr;
Ref<GameObject> g_focus;
Ref<GameObject> g_drag;  // the slider the pointer is dragging
i32 g_viewW = 320, g_viewH = 240;
u32 g_virtualNav = 0;
// Held-direction repeat (only with an EventSystem in the scene).
u32 g_holdDir = 0;
Scalar g_holdTime = 0;
Scalar g_holdNext = 0;

constexpr u32 kMaxSelectable = 48;

u32 clampOrder(i32 order) { return static_cast<u32>((order < -2048 ? -2048 : (order > 2047 ? 2047 : order)) + 2048); }
u32 uiKey(i32 order, i32 canvasOrder) { return kUiSortBase + clampOrder(order + canvasOrder); }

// ---- element geometry ---------------------------------------------------------------------------------------------------
// Where an element keeps its placement. UIText has no size fields: its size is the text size.
struct Geom {
  i32* anchor = nullptr;
  i32* x = nullptr;
  i32* y = nullptr;
  i32* w = nullptr;
  i32* h = nullptr;
  i32 tw = 0, th = 0;  // UIText size in virtual pixels
};

bool textSize(const UIText& t, const UI::CanvasView& cv, i32& w, i32& h) {
  if (!GfxAssets::bound() || GfxAssets::assets().fontCount == 0 || !t.text[0]) { w = h = 0; return t.text[0] != 0; }
  const PackAssets& as = GfxAssets::assets();
  const PackFontRec* font = t.font.isNull() ? &as.fonts[0] : as.findFont(t.font);
  if (!font) font = &as.fonts[0];
  // Glyphs are not scaled: in virtual pixels the text is smaller by the scale.
  const Scalar inv = cv.scale > 0 ? 1.0f / cv.scale : 1.0f;
  w = static_cast<i32>(static_cast<Scalar>(UI::textWidth(*font, t.text)) * inv + 0.5f);
  h = static_cast<i32>(static_cast<Scalar>(UI::textHeight(*font, t.text)) * inv + 0.5f);
  return true;
}

bool geomOf(GameObject* go, const UI::CanvasView& cv, Geom& g) {
  if (!go) return false;
  if (UIButton* b = go->GetComponent<UIButton>()) { g.anchor = &b->anchor; g.x = &b->x; g.y = &b->y; g.w = &b->w; g.h = &b->h; return true; }
  if (UIImage* im = go->GetComponent<UIImage>()) { g.anchor = &im->anchor; g.x = &im->x; g.y = &im->y; g.w = &im->w; g.h = &im->h; return true; }
  if (UIText* t = go->GetComponent<UIText>()) {
    g.anchor = &t->anchor; g.x = &t->x; g.y = &t->y;
    textSize(*t, cv, g.tw, g.th);
    return true;
  }
  return false;
}

i32 gw(const Geom& g) { return g.w ? *g.w : g.tw; }
i32 gh(const Geom& g) { return g.h ? *g.h : g.th; }

Rect rectOfGeom(const Geom& g, const UI::CanvasView& cv) {
  return UI::resolveRect(*g.anchor, *g.x, *g.y, gw(g), gh(g), cv.vw, cv.vh);
}

// Sets x / y so the element's top-left corner is (left, top) in virtual pixels (the inverse of UI::resolveRect).
void placeTopLeft(Geom& g, i32 left, i32 top, const UI::CanvasView& cv) {
  i32 anchor = *g.anchor;
  if (anchor < 0 || anchor >= UIAnchor::kCount) anchor = UIAnchor::TopLeft;
  const i32 col = anchor % 3, row = anchor / 3;
  const i32 w = gw(g), h = gh(g);
  const i32 ax = col == 0 ? 0 : (col == 1 ? cv.vw / 2 : cv.vw), ay = row == 0 ? 0 : (row == 1 ? cv.vh / 2 : cv.vh);
  const i32 px = col == 0 ? 0 : (col == 1 ? w / 2 : w), py = row == 0 ? 0 : (row == 1 ? h / 2 : h);
  *g.x = left - ax + px;
  *g.y = top - ay + py;
}

i32 roundI(Scalar v) { return static_cast<i32>(v < 0 ? v - 0.5f : v + 0.5f); }

}  // namespace

// ---- Canvas -------------------------------------------------------------------------------------------------------------
namespace UI {

void viewSize(i32& w, i32& h) { w = g_viewW; h = g_viewH; }

CanvasView canvasView(World& world, i32 viewW, i32 viewH) {
  CanvasView cv;
  cv.viewW = cv.vw = viewW;
  cv.viewH = cv.vh = viewH;
  const ComponentPool* p = world.pool<Canvas>();
  if (!p) return cv;
  for (u32 i = 0; i < p->highWater; ++i) {
    if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    const Canvas* c = static_cast<const Canvas*>(p->at(i));
    const GameObject* go = c->gameObject();
    if (!c->enabled() || !go || !go->activeInHierarchy()) continue;
    cv.order = c->sortingOrder;
    if (c->scaleMode == 1 && c->referenceWidth > 0 && c->referenceHeight > 0 && viewW > 0 && viewH > 0) {
      const Scalar sw = static_cast<Scalar>(viewW) / static_cast<Scalar>(c->referenceWidth);
      const Scalar sh = static_cast<Scalar>(viewH) / static_cast<Scalar>(c->referenceHeight);
      Scalar m = c->matchWidthOrHeight;
      m = m < 0 ? 0 : (m > 1 ? 1 : m);
      cv.scale = sw + (sh - sw) * m;
      if (cv.scale <= 0.01f) cv.scale = 1.0f;
      cv.vw = roundI(static_cast<Scalar>(viewW) / cv.scale);
      cv.vh = roundI(static_cast<Scalar>(viewH) / cv.scale);
    }
    break;  // the first active Canvas decides
  }
  return cv;
}

bool rectOf(GameObject* go, const CanvasView& cv, Rect& out) {
  if (!go) return false;
  Geom g;
  if (go->GetComponent<UIButton>() || go->GetComponent<UIImage>() || go->GetComponent<UIText>()) {
    if (!geomOf(go, cv, g)) return false;
    out = rectOfGeom(g, cv);
    return true;
  }
  return false;
}

}  // namespace UI

// ---- members of the F6 components ---------------------------------------------------------------------------------------
void Slider::SetValue(Scalar v) {
  if (maxValue < minValue) return;
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  if (wholeNumbers) v = static_cast<Scalar>(roundI(v));
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  if (v == value) return;
  value = v;
  World* w = ownerWorld();
  if (!w) return;
  MethodArg a;
  a.s = value;
  onValueChanged.InvokeWith(*w, MethodArgKind::Scalar, a);
  const GameObject* go = gameObject();
  Debug::Log("rensga: ui slider %s value_x100=%d", go ? go->name : "?", static_cast<int>(value * 100.0f + (value < 0 ? -0.5f : 0.5f)));
}

void Toggle::SetIsOn(bool on) {
  if (on == isOn) return;
  isOn = on;
  World* w = ownerWorld();
  if (!w) return;
  MethodArg a;
  a.b = on;
  onValueChanged.InvokeWith(*w, MethodArgKind::Bool, a);
  const GameObject* go = gameObject();
  Debug::Log("rensga: ui toggle %s isOn=%d", go ? go->name : "?", on ? 1 : 0);
}

EventSystem* EventSystem::current() {
  World* w = World::active();
  const ComponentPool* p = w ? w->pool<EventSystem>() : nullptr;
  if (!p) return nullptr;
  for (u32 i = 0; i < p->highWater; ++i) {
    if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    EventSystem* e = static_cast<EventSystem*>(p->at(i));
    const GameObject* go = e->gameObject();
    if (e->enabled() && go && go->activeInHierarchy()) return e;
  }
  return nullptr;
}
GameObject* EventSystem::currentSelectedGameObject() const { return UI::focused(); }
void EventSystem::SetSelectedGameObject(GameObject* go) { UI::setFocused(go); }

bool UIButton::isFocused() const { return UI::focused() == gameObject() && gameObject() != nullptr; }

namespace UI {

void setClickHandler(ClickHandler handler) { g_clickHandler = handler; }
void setCancelHandler(CancelHandler handler) { g_cancelHandler = handler; }
void resetFocus() {
  g_focus.reset();
  g_drag.reset();
  g_holdDir = 0;
}
void pushNav(u32 bits) { g_virtualNav |= bits; }
GameObject* focused() { return g_focus.get(); }
void setFocused(GameObject* go) {
  if (go == g_focus.get()) return;
  if (go) g_focus = go->ref(); else g_focus.reset();
  if (go) Debug::Log("rensga: ui focus %s", go->name);
}

// ---- layout, slider and toggle visuals ---------------------------------------------------------------------------------
namespace {

bool hasUi(GameObject* go) {
  return go->GetComponent<UIButton>() || go->GetComponent<UIImage>() || go->GetComponent<UIText>();
}

void layoutGroup(World& world, LayoutGroup& lg, const CanvasView& cv) {
  GameObject* go = lg.gameObject();
  if (!go) return;
  (void)world;
  // The container is the group object's own element, or the whole view.
  Rect box(0, 0, static_cast<Scalar>(cv.vw), static_cast<Scalar>(cv.vh));
  Rect own;
  if (rectOf(go, cv, own)) box = own;
  struct Item { Geom g; i32 w, h; };
  constexpr u32 kMax = 32;
  Item items[kMax];
  u32 n = 0;
  for (u32 i = 0, c = go->transform.childCount(); i < c && n < kMax; ++i) {
    Transform* t = go->transform.GetChild(i);
    GameObject* child = t ? t->gameObject : nullptr;
    if (!child || !child->activeSelf() || !hasUi(child)) continue;
    Geom g;
    if (!geomOf(child, cv, g)) continue;
    items[n].g = g;
    items[n].w = lg.type == 2 ? lg.cellWidth : gw(g);
    items[n].h = lg.type == 2 ? lg.cellHeight : gh(g);
    ++n;
  }
  if (n == 0) return;
  const i32 left = static_cast<i32>(box.x) + lg.paddingLeft, top = static_cast<i32>(box.y) + lg.paddingTop;
  const i32 innerW = static_cast<i32>(box.width) - lg.paddingLeft - lg.paddingRight;
  const i32 innerH = static_cast<i32>(box.height) - lg.paddingTop - lg.paddingBottom;
  const i32 acol = lg.childAlignment % 3, arow = lg.childAlignment / 3;  // 0 left / upper, 1 centre / middle, 2 right / lower
  auto offset = [](i32 inner, i32 content, i32 align) { return align == 0 ? 0 : (align == 1 ? (inner - content) / 2 : inner - content); };

  if (lg.type == 2) {  // grid
    const i32 cols = lg.columns < 1 ? 1 : lg.columns;
    const i32 rows = (static_cast<i32>(n) + cols - 1) / cols;
    const i32 usedCols = static_cast<i32>(n) < cols ? static_cast<i32>(n) : cols;
    const i32 totalW = usedCols * lg.cellWidth + (usedCols - 1) * lg.spacing, totalH = rows * lg.cellHeight + (rows - 1) * lg.spacing;
    const i32 ox = left + offset(innerW, totalW, acol), oy = top + offset(innerH, totalH, arow);
    for (u32 i = 0; i < n; ++i) {
      const i32 cx = static_cast<i32>(i) % cols, cy = static_cast<i32>(i) / cols;
      placeTopLeft(items[i].g, ox + cx * (lg.cellWidth + lg.spacing), oy + cy * (lg.cellHeight + lg.spacing), cv);
    }
    return;
  }
  i32 total = 0;
  for (u32 i = 0; i < n; ++i) total += lg.type == 0 ? items[i].w : items[i].h;
  total += (static_cast<i32>(n) - 1) * lg.spacing;
  if (lg.type == 0) {  // horizontal
    i32 cx = left + offset(innerW, total, acol);
    for (u32 i = 0; i < n; ++i) {
      placeTopLeft(items[i].g, cx, top + offset(innerH, items[i].h, arow), cv);
      cx += items[i].w + lg.spacing;
    }
  } else {  // vertical
    i32 cy = top + offset(innerH, total, arow);
    for (u32 i = 0; i < n; ++i) {
      placeTopLeft(items[i].g, left + offset(innerW, items[i].w, acol), cy, cv);
      cy += items[i].h + lg.spacing;
    }
  }
}

void sliderParts(World& world, Slider& s, const CanvasView& cv) {
  GameObject* go = s.gameObject();
  if (!go) return;
  UIImage* track = go->GetComponent<UIImage>();
  if (!track) return;
  const Rect t = UI::resolveRect(track->anchor, track->x, track->y, track->w, track->h, cv.vw, cv.vh);
  Scalar n = s.normalizedValue();
  n = n < 0 ? 0 : (n > 1 ? 1 : n);
  const bool horizontal = s.direction <= SliderDirection::RightToLeft;
  const bool reversed = s.direction == SliderDirection::RightToLeft || s.direction == SliderDirection::TopToBottom;
  // For vertical sliders BottomToTop grows from the bottom edge, TopToBottom from the top.
  const i32 tx = static_cast<i32>(t.x), ty = static_cast<i32>(t.y), tw = static_cast<i32>(t.width), th = static_cast<i32>(t.height);
  if (!s.fillRect.isNull()) {
    GameObject* fo = world.findByGuid(s.fillRect);
    UIImage* fill = fo ? fo->GetComponent<UIImage>() : nullptr;
    if (fill) {
      Geom g;
      g.anchor = &fill->anchor; g.x = &fill->x; g.y = &fill->y; g.w = &fill->w; g.h = &fill->h;
      if (horizontal) {
        const i32 fw = roundI(static_cast<Scalar>(tw) * n);
        fill->w = fw; fill->h = th;
        placeTopLeft(g, reversed ? tx + tw - fw : tx, ty, cv);
      } else {
        const i32 fh = roundI(static_cast<Scalar>(th) * n);
        fill->w = tw; fill->h = fh;
        placeTopLeft(g, tx, reversed ? ty : ty + th - fh, cv);
      }
    }
  }
  if (!s.handleRect.isNull()) {
    GameObject* ho = world.findByGuid(s.handleRect);
    Geom g;
    if (ho && geomOf(ho, cv, g)) {
      const i32 hw = gw(g), hh = gh(g);
      if (horizontal) {
        const i32 at = tx + roundI(static_cast<Scalar>(tw) * (reversed ? 1.0f - n : n));
        placeTopLeft(g, at - hw / 2, ty + th / 2 - hh / 2, cv);
      } else {
        const i32 at = ty + roundI(static_cast<Scalar>(th) * (reversed ? n : 1.0f - n));
        placeTopLeft(g, tx + tw / 2 - hw / 2, at - hh / 2, cv);
      }
    }
  }
}

void toggleGraphic(World& world, Toggle& t) {
  if (t.graphic.isNull()) return;
  GameObject* go = world.findByGuid(t.graphic);
  if (!go) return;
  if (UIImage* im = go->GetComponent<UIImage>()) { if (im->enabled() != t.isOn) im->setEnabled(t.isOn); }
  else if (UIText* tx = go->GetComponent<UIText>()) { if (tx->enabled() != t.isOn) tx->setEnabled(t.isOn); }
  else if (go->activeSelf() != t.isOn) go->SetActive(t.isOn);
}

}  // namespace

void prepare(World& world) { prepare(world, g_viewW, g_viewH); }

void prepare(World& world, i32 viewW, i32 viewH) {
  const ComponentPool* up = world.pool<UIText>();
  if (up && up->liveCount) {
    world.forEach<UIText>([](UIText& t) {
      if (t.labelApplied_) return;
      t.labelApplied_ = true;
      if (!t.text[0]) t.SetText(t.label.c_str());
    });
  }
  const ComponentPool* lp = world.pool<LayoutGroup>();
  const ComponentPool* sp = world.pool<Slider>();
  const ComponentPool* tp = world.pool<Toggle>();
  if ((!lp || lp->liveCount == 0) && (!sp || sp->liveCount == 0) && (!tp || tp->liveCount == 0)) return;
  const CanvasView cv = canvasView(world, viewW, viewH);
  world.forEach<LayoutGroup>([&](LayoutGroup& lg) {
    const GameObject* go = lg.gameObject();
    if (lg.enabled() && go && go->activeInHierarchy()) layoutGroup(world, lg, cv);
  });
  world.forEach<Slider>([&](Slider& s) {
    const GameObject* go = s.gameObject();
    if (s.enabled() && go && go->activeInHierarchy()) sliderParts(world, s, cv);
  });
  world.forEach<Toggle>([&](Toggle& t) {
    const GameObject* go = t.gameObject();
    if (t.enabled() && go && go->activeInHierarchy()) toggleGraphic(world, t);
  });
}

// ---- focus and navigation ----------------------------------------------------------------------------------------------
namespace {

struct Cand {
  GameObject* go;
  Rect r;
  i32 y, x;
};

bool selectableRect(GameObject* go, const CanvasView& cv, Rect& r) {
  if (UIButton* b = go->GetComponent<UIButton>()) {
    r = UI::resolveRect(b->anchor, b->x, b->y, b->w, b->h, cv.vw, cv.vh);
    return true;
  }
  if (UIImage* im = go->GetComponent<UIImage>()) {
    r = UI::resolveRect(im->anchor, im->x, im->y, im->w, im->h, cv.vw, cv.vh);
    return true;
  }
  return false;
}

void addCand(Cand* cand, u32& n, GameObject* go, const CanvasView& cv) {
  if (n >= kMaxSelectable || !go || !go->activeInHierarchy()) return;
  for (u32 i = 0; i < n; ++i) if (cand[i].go == go) return;  // an object with a button and a slider is listed once
  Rect r;
  if (!selectableRect(go, cv, r)) return;
  Cand c{go, r, static_cast<i32>(r.y), static_cast<i32>(r.x)};
  u32 j = n++;
  // Reading order on screen (top to bottom, then left to right); object index breaks ties so the order never depends on pool slots.
  while (j > 0 && (cand[j - 1].y > c.y || (cand[j - 1].y == c.y && (cand[j - 1].x > c.x || (cand[j - 1].x == c.x && cand[j - 1].go->index > c.go->index))))) {
    cand[j] = cand[j - 1];
    --j;
  }
  cand[j] = c;
}

u32 collect(World& world, Cand* cand, const CanvasView& cv) {
  u32 n = 0;
  world.forEach<UIButton>([&](UIButton& b) {
    if (b.interactable && b.enabled()) addCand(cand, n, b.gameObject(), cv);
  });
  world.forEach<Slider>([&](Slider& s) {
    if (s.interactable && s.enabled()) addCand(cand, n, s.gameObject(), cv);
  });
  world.forEach<Toggle>([&](Toggle& t) {
    if (t.interactable && t.enabled()) addCand(cand, n, t.gameObject(), cv);
  });
  return n;
}

enum Dir : u32 { kUp = 0, kDown = 1, kLeft = 2, kRight = 3 };

// Nearest selectable in a direction (Unity's automatic navigation): it must lie in that half plane, off-axis distance counts double.
i32 spatial(const Cand* cand, u32 n, u32 from, Dir d) {
  const Scalar cx = cand[from].r.x + cand[from].r.width * 0.5f, cy = cand[from].r.y + cand[from].r.height * 0.5f;
  i32 best = -1;
  Scalar bestScore = 0;
  for (u32 i = 0; i < n; ++i) {
    if (i == from) continue;
    const Scalar dx = cand[i].r.x + cand[i].r.width * 0.5f - cx, dy = cand[i].r.y + cand[i].r.height * 0.5f - cy;
    Scalar along, off;
    switch (d) {
      case kUp: along = -dy; off = dx; break;
      case kDown: along = dy; off = dx; break;
      case kLeft: along = -dx; off = dy; break;
      default: along = dx; off = dy; break;
    }
    if (along <= 0.5f) continue;
    if (off < 0) off = -off;
    const Scalar score = along + 2.0f * off;
    if (best < 0 || score < bestScore) { best = static_cast<i32>(i); bestScore = score; }
  }
  return best;
}

void activate(World& world, GameObject* go) {
  if (!go) return;
  Ref<GameObject> ref = go->ref();
  if (UIButton* b = go->GetComponent<UIButton>()) {
    Debug::Log("rensga: ui click %s", go->name);
    world.sendUIClick(go);
    if (ref) b->onClick.Invoke(world);
    if (g_clickHandler && ref) g_clickHandler(ref.get());
    return;
  }
  if (Toggle* t = go->GetComponent<Toggle>()) t->SetIsOn(!t->isOn);
}

// Left / right (or up / down for vertical sliders) on a focused slider. Returns true when the slider consumed the direction.
bool slideFocused(GameObject* go, Dir d) {
  Slider* s = go ? go->GetComponent<Slider>() : nullptr;
  if (!s || !s->enabled()) return false;
  const bool horizontal = s->direction <= SliderDirection::RightToLeft;
  if (horizontal != (d == kLeft || d == kRight)) return false;
  const bool reversed = s->direction == SliderDirection::RightToLeft || s->direction == SliderDirection::TopToBottom;
  // Up / right raise the value; reversed sliders swap that. Unity's keyboard step is a tenth of the range (1 with whole numbers).
  bool increase = (d == kRight || d == kUp);
  if (reversed) increase = !increase;
  const Scalar range = s->maxValue - s->minValue;
  Scalar step = s->wholeNumbers ? 1.0f : range * 0.1f;
  if (step <= 0) step = 1.0f;
  s->SetValue(s->value + (increase ? step : -step));
  return true;
}

GameObject* explicitTarget(World& world, const UIButton& b, Dir d) {
  const Guid& g = d == kUp ? b.selectOnUp : (d == kDown ? b.selectOnDown : (d == kLeft ? b.selectOnLeft : b.selectOnRight));
  return g.isNull() ? nullptr : world.findByGuid(g);
}

}  // namespace

void update(World& world) {
  prepare(world);
  const ComponentPool* bp = world.pool<UIButton>();
  const ComponentPool* sp = world.pool<Slider>();
  const ComponentPool* tp = world.pool<Toggle>();
  if ((!bp || bp->liveCount == 0) && (!sp || sp->liveCount == 0) && (!tp || tp->liveCount == 0)) { g_virtualNav = 0; return; }

  const CanvasView cv = canvasView(world, g_viewW, g_viewH);
  Cand cand[kMaxSelectable];
  const u32 n = collect(world, cand, cv);
  if (n == 0) { g_focus.reset(); g_virtualNav = 0; return; }

  EventSystem* es = EventSystem::current();
  u32 pos = 0;
  bool found = false;
  GameObject* cur = g_focus.get();
  for (u32 k = 0; k < n; ++k) if (cand[k].go == cur) { pos = k; found = true; break; }
  if (!found && es && !es->firstSelected.isNull()) {
    GameObject* first = world.findByGuid(es->firstSelected);
    for (u32 k = 0; k < n; ++k) if (cand[k].go == first) { pos = k; break; }
  }
  const u32 virt = g_virtualNav;
  g_virtualNav = 0;
  const VirtualGamepad& pad = Input::pad();
  const bool east = Application::confirmButtonEast();
  const bool submit = (east ? pad.pressed.east : pad.pressed.south) || (virt & NavSubmit);
  const bool cancel = (east ? pad.pressed.south : pad.pressed.east) || (virt & NavCancel);
  // Pressed this frame, plus the repeat of a held direction (only when the scene has an EventSystem).
  bool dirs[4] = {pad.pressed.up || (virt & NavUp) != 0, pad.pressed.down || (virt & NavDown) != 0,
                  pad.pressed.left || (virt & NavLeft) != 0, pad.pressed.right || (virt & NavRight) != 0};
  if (es) {
    const bool held[4] = {pad.held.up, pad.held.down, pad.held.left, pad.held.right};
    u32 heldDir = 0;
    for (u32 d = 0; d < 4; ++d) if (held[d]) { heldDir = d + 1; break; }
    if (heldDir && heldDir == g_holdDir) {
      g_holdTime += Time::unscaledDeltaTime();
      if (g_holdTime >= g_holdNext) {
        dirs[heldDir - 1] = true;
        g_holdNext = g_holdTime + (es->repeatRate > 0.01f ? es->repeatRate : 0.01f);
      }
    } else {
      g_holdDir = heldDir;
      g_holdTime = 0;
      g_holdNext = es->repeatDelay;
    }
  } else {
    g_holdDir = 0;
  }

  const bool wasFocused = found;
  // An unfocused menu starts on its first selectable and ignores the direction of that first frame (a press is still a press).
  if (found) {
    GameObject* focus = cand[pos].go;
    UIButton* btn = focus->GetComponent<UIButton>();
    const u32 navMode = btn ? static_cast<u32>(btn->navigation) : 0u;
    // Priority: down / right first, like the original two-way focus.
    static const Dir kOrder[4] = {kDown, kRight, kUp, kLeft};
    for (u32 k = 0; k < 4; ++k) {
      const Dir d = kOrder[k];
      if (!dirs[d]) continue;
      if (slideFocused(focus, d)) break;
      if (navMode == 2) break;  // None: the pad never leaves this button
      if (navMode == 1) {       // Explicit: only the select* targets; null = stay
        GameObject* t = btn ? explicitTarget(world, *btn, d) : nullptr;
        for (u32 j = 0; t && j < n; ++j) if (cand[j].go == t) { pos = j; break; }
        break;
      }
      const i32 s = spatial(cand, n, pos, d);
      if (s >= 0) pos = static_cast<u32>(s);
      else if (d == kDown || d == kRight) pos = (pos + 1) % n;   // no neighbour that way: reading order, wrapping (the original rule)
      else pos = (pos + n - 1) % n;
      break;
    }
  }
  GameObject* now = cand[pos].go;
  if (now != cur || !wasFocused) {
    g_focus = now->ref();
    Debug::Log("rensga: ui focus %s", now->name);
  }

  // Pointer (mouse or finger): select what is under it; press activates; a slider follows the pointer while it is held.
  if (Input::mousePresent()) {
    const Vector2 mp = Input::mousePosition();
    const Scalar inv = cv.scale > 0 ? 1.0f / cv.scale : 1.0f;
    const Scalar px = mp.x * inv, py = mp.y * inv;
    if (Input::GetMouseButtonDown(0)) {
      for (i32 k = static_cast<i32>(n) - 1; k >= 0; --k) {
        const Rect& r = cand[k].r;
        if (px < r.x || px >= r.x + r.width || py < r.y || py >= r.y + r.height) continue;
        if (g_focus.get() != cand[k].go) { g_focus = cand[k].go->ref(); Debug::Log("rensga: ui focus %s", cand[k].go->name); }
        if (cand[k].go->GetComponent<Slider>()) g_drag = cand[k].go->ref();
        else activate(world, cand[k].go);
        break;
      }
    }
    if (!Input::GetMouseButton(0)) g_drag.reset();
    if (GameObject* dg = g_drag.get()) {
      Slider* s = dg->GetComponent<Slider>();
      UIImage* track = dg->GetComponent<UIImage>();
      if (s && track && track->w > 0 && track->h > 0) {
        const Rect t = UI::resolveRect(track->anchor, track->x, track->y, track->w, track->h, cv.vw, cv.vh);
        const bool horizontal = s->direction <= SliderDirection::RightToLeft;
        const bool reversed = s->direction == SliderDirection::RightToLeft || s->direction == SliderDirection::TopToBottom;
        Scalar f = horizontal ? (px - t.x) / t.width : (py - t.y) / t.height;
        if (!horizontal) f = 1.0f - f;
        if (reversed) f = 1.0f - f;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        s->SetValue(s->minValue + (s->maxValue - s->minValue) * f);
      }
    }
  }

  if (submit) activate(world, g_focus.get());
  if (cancel && g_cancelHandler) g_cancelHandler(g_focus.get());
  prepare(world);  // a value that just changed moves its fill and handle in the same frame
}

// ---- drawing -----------------------------------------------------------------------------------------------------------
namespace {
Rect scaled(const Rect& r, Scalar s) {
  return s == 1.0f ? r : Rect(r.x * s, r.y * s, r.width * s, r.height * s);
}
}  // namespace

void draw(World& world, IRenderer& r) {
  if (!GfxAssets::bound()) return;
  const PackAssets& as = GfxAssets::assets();
  g_viewW = static_cast<i32>(r.width());
  g_viewH = static_cast<i32>(r.height());
  prepare(world);
  const CanvasView cv = canvasView(world, g_viewW, g_viewH);
  const Scalar sc = cv.scale;

  world.forEach<UIImage>([&](UIImage& im) {
    GameObject* go = im.gameObject();
    if (!go || !im.enabled() || !go->activeInHierarchy()) return;
    Rect dst = UI::resolveRect(im.anchor, im.x, im.y, im.w, im.h, cv.vw, cv.vh);
    const u32 ti = GfxAssets::textureIndex(im.texture);
    const bool tex = ti < as.textureCount;
    Scalar sx = static_cast<Scalar>(im.srcX), sy = static_cast<Scalar>(im.srcY);
    Scalar sw = 0, sh = 0;
    if (tex) {
      sw = im.srcW > 0 ? static_cast<Scalar>(im.srcW) : static_cast<Scalar>(as.textures[ti].width);
      sh = im.srcH > 0 ? static_cast<Scalar>(im.srcH) : static_cast<Scalar>(as.textures[ti].height);
    }
    if (im.fillMethod != 0) {
      Scalar f = im.fillAmount;
      f = f < 0 ? 0 : (f > 1 ? 1 : f);
      if (f <= 0) return;
      if (im.fillMethod == 1) {
        const Scalar full = dst.width;
        dst.width = full * f;
        sw *= f;
        if (im.fillFromEnd) { dst.x += full - dst.width; sx += static_cast<Scalar>(im.srcW > 0 ? im.srcW : (tex ? as.textures[ti].width : 0)) - sw; }
      } else {
        const Scalar full = dst.height;
        dst.height = full * f;
        const Scalar fullSrc = sh;
        sh *= f;
        if (!im.fillFromEnd) { dst.y += full - dst.height; sy += fullSrc - sh; }  // from the bottom: the lower part stays
      }
    }
    const u32 key = uiKey(im.sortingOrder, cv.order);
    if (!tex) { r.drawRect(scaled(dst, sc), im.color, key); return; }
    const u32 handle = GfxAssets::handle(ti);
    if (!handle) return;
    r.drawSprite(handle, Rect(sx, sy, sw, sh), scaled(dst, sc), im.color, key);
  });

  world.forEach<UIButton>([&](UIButton& b) {
    GameObject* go = b.gameObject();
    if (!go || !b.enabled() || !go->activeInHierarchy()) return;
    Color32 c = b.isFocused() ? b.focusColor : b.normalColor;
    if (!b.interactable) c = Color32(c.r / 2, c.g / 2, c.b / 2, c.a);
    r.drawRect(scaled(UI::resolveRect(b.anchor, b.x, b.y, b.w, b.h, cv.vw, cv.vh), sc), c, uiKey(b.sortingOrder, cv.order));
  });

  // The focus ring of a slider or toggle (a button shows its focus by its colour).
  if (GameObject* fg = g_focus.get()) {
    if (fg->activeInHierarchy() && (fg->GetComponent<Slider>() || fg->GetComponent<Toggle>()) && !fg->GetComponent<UIButton>()) {
      Rect fr;
      if (const UIImage* im = fg->GetComponent<UIImage>()) {
        fr = UI::resolveRect(im->anchor, im->x, im->y, im->w, im->h, cv.vw, cv.vh);
        const Scalar pad = 2, t = 2;
        const Color32 ring(240, 160, 30, 255);
        const u32 key = uiKey(2047, 0);
        Rect o(fr.x - pad - t, fr.y - pad - t, fr.width + 2 * (pad + t), fr.height + 2 * (pad + t));
        r.drawRect(scaled(Rect(o.x, o.y, o.width, t), sc), ring, key);
        r.drawRect(scaled(Rect(o.x, o.y + o.height - t, o.width, t), sc), ring, key);
        r.drawRect(scaled(Rect(o.x, o.y + t, t, o.height - 2 * t), sc), ring, key);
        r.drawRect(scaled(Rect(o.x + o.width - t, o.y + t, t, o.height - 2 * t), sc), ring, key);
      }
    }
  }

  world.forEach<UIText>([&](UIText& t) {
    GameObject* go = t.gameObject();
    if (!go || !t.enabled() || !go->activeInHierarchy() || !t.text[0] || as.fontCount == 0) return;
    const PackFontRec* font = t.font.isNull() ? &as.fonts[0] : as.findFont(t.font);
    if (!font) font = &as.fonts[0];
    if (font->texture >= as.textureCount) return;
    const u32 handle = GfxAssets::handle(font->texture);
    if (!handle) return;
    i32 tw, th;
    textSize(t, cv, tw, th);
    const Rect box = scaled(UI::resolveRect(t.anchor, t.x, t.y, tw, th, cv.vw, cv.vh), sc);
    r.drawText(handle, *font, static_cast<i32>(box.x), static_cast<i32>(box.y), t.text, t.color, uiKey(t.sortingOrder, cv.order) + 1);
  });
}

}  // namespace UI

}  // namespace rensga
