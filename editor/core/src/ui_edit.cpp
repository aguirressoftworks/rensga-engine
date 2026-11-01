#include "rensga/editor/ui_edit.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>

#include "rensga/Rensga.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/ui_components.h"
#include "rensga/core/world.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {
bool isUiType(const std::string& t) {
  return t == "UIText" || t == "UIImage" || t == "UIButton" || t == "Canvas" || t == "Slider" || t == "Toggle" || t == "LayoutGroup" ||
         t == "EventSystem";
}
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
const SceneObject* findObj(const SceneDocument& doc, const std::string& guid) {
  for (const SceneObject& o : doc.scene().objects) if (o.guid == guid) return &o;
  return nullptr;
}
bool sceneHas(const SceneDocument& doc, const char* type) {
  for (const SceneObject& o : doc.scene().objects)
    for (const SceneComponent& c : o.components) if (c.type == type) return true;
  return false;
}
std::string firstOf(const SceneDocument& doc, const char* type) {
  for (const SceneObject& o : doc.scene().objects)
    for (const SceneComponent& c : o.components) if (c.type == type) return o.guid;
  return std::string();
}
json color(int r, int g, int b, int a = 255) { return json::array({r, g, b, a}); }
}  // namespace

const char* uiKindName(UiKind k) {
  switch (k) {
    case UiKind::Canvas: return "Canvas";
    case UiKind::Text: return "Text";
    case UiKind::Image: return "Image";
    case UiKind::Button: return "Button";
    case UiKind::Slider: return "Slider";
    case UiKind::Toggle: return "Toggle";
    case UiKind::LayoutGroup: return "Layout Group";
    case UiKind::EventSystem: return "EventSystem";
  }
  return "";
}

bool uiKindFromName(const std::string& name, UiKind& out) {
  const std::string n = lower(name);
  static const struct { const char* n; UiKind k; } kAll[] = {{"canvas", UiKind::Canvas}, {"text", UiKind::Text}, {"image", UiKind::Image},
                                                              {"button", UiKind::Button}, {"slider", UiKind::Slider}, {"toggle", UiKind::Toggle},
                                                              {"layout", UiKind::LayoutGroup}, {"layoutgroup", UiKind::LayoutGroup},
                                                              {"layout_group", UiKind::LayoutGroup}, {"eventsystem", UiKind::EventSystem}};
  for (const auto& e : kAll) if (n == e.n) { out = e.k; return true; }
  return false;
}

bool sceneHasUi(const SceneDocument& doc) {
  for (const SceneObject& o : doc.scene().objects)
    for (const SceneComponent& c : o.components) if (isUiType(c.type)) return true;
  return false;
}

void uiPlaceTopLeft(int anchor, int left, int top, int w, int h, int vw, int vh, int& x, int& y) {
  if (anchor < 0 || anchor >= UIAnchor::kCount) anchor = 0;
  const int col = anchor % 3, row = anchor / 3;
  const int ax = col == 0 ? 0 : (col == 1 ? vw / 2 : vw), ay = row == 0 ? 0 : (row == 1 ? vh / 2 : vh);
  const int px = col == 0 ? 0 : (col == 1 ? w / 2 : w), py = row == 0 ? 0 : (row == 1 ? h / 2 : h);
  x = left - ax + px;
  y = top - ay + py;
}

void uiResolve(int anchor, int x, int y, int w, int h, int vw, int vh, int out[4]) {
  const Rect r = UI::resolveRect(anchor, x, y, w, h, vw, vh);
  out[0] = static_cast<int>(r.x); out[1] = static_cast<int>(r.y); out[2] = static_cast<int>(r.width); out[3] = static_cast<int>(r.height);
}

// ---- creation ----------------------------------------------------------------------------------------------------------------
namespace {
struct Maker {
  SceneDocument& doc;
  int vw, vh;
  std::string make(const std::string& name, const std::string& parent, std::vector<SceneComponent> comps) {
    return doc.createObject(name, parent, comps);
  }
  static SceneComponent comp(const char* type, json fields) { SceneComponent c; c.type = type; c.fields = std::move(fields); return c; }
  int indexOf(const std::string& guid, const char* type) {
    const SceneObject* o = findObj(doc, guid);
    if (!o) return -1;
    for (size_t i = 0; i < o->components.size(); ++i) if (o->components[i].type == type) return static_cast<int>(i);
    return -1;
  }
  std::string canvas() {
    return make("Canvas", "", {comp("Canvas", json::object({{"scaleMode", 1}, {"referenceWidth", vw}, {"referenceHeight", vh}, {"matchWidthOrHeight", 0.5}}))});
  }
  std::string eventSystem() { return make("EventSystem", "", {comp("EventSystem", json::object())}); }
};
}  // namespace

std::string createUiObject(SceneDocument& doc, UiKind kind, const std::string& parentIn, int vw, int vh, const std::string& nameIn) {
  if (!doc.loaded()) return "";
  Maker m{doc, vw > 0 ? vw : 480, vh > 0 ? vh : 272};
  doc.beginGesture();
  std::string result;
  std::string parent = parentIn;
  auto nm = [&](const char* def) { return nameIn.empty() ? std::string(def) : nameIn; };
  auto sub = [&](const char* suffix, const char* def) { return nameIn.empty() ? std::string(def) : nameIn + suffix; };
  if (kind == UiKind::Canvas) {
    result = m.canvas();  // a second Canvas is allowed; the first active one decides the scale
    if (!nameIn.empty()) doc.rename(result, nameIn);
  } else if (kind == UiKind::EventSystem) {
    result = sceneHas(doc, "EventSystem") ? firstOf(doc, "EventSystem") : m.eventSystem();
    if (!nameIn.empty()) doc.rename(result, nameIn);
  } else {
    std::string canvas = firstOf(doc, "Canvas");
    if (canvas.empty()) canvas = m.canvas();
    if (parent.empty()) parent = canvas;
    const bool interactive = kind == UiKind::Button || kind == UiKind::Slider || kind == UiKind::Toggle;
    if (interactive && !sceneHas(doc, "EventSystem")) m.eventSystem();
    // The new element sits at the middle of the view, a little below the last one of its kind so repeated creates do not stack.
    int n = 0;
    for (const SceneObject& o : doc.scene().objects) for (const SceneComponent& c : o.components) if (c.type == "UIButton" || c.type == "Slider" || c.type == "Toggle") ++n;
    const int dy = (n % 6) * 30 - 60;
    switch (kind) {
      case UiKind::Text:
        result = m.make(nm("Text"), parent, {Maker::comp("UIText", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"label", "New Text"}}))});
        break;
      case UiKind::Image:
        result = m.make(nm("Image"), parent, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"w", 64}, {"h", 64}, {"color", color(200, 200, 210)}}))});
        break;
      case UiKind::Button: {
        result = m.make(nm("Button"), parent, {Maker::comp("UIButton", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"w", 120}, {"h", 24}}))});
        m.make(sub("Label", "Label"), result, {Maker::comp("UIText", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"sortingOrder", 1}, {"label", "Button"}}))});
        break;
      }
      case UiKind::Slider: {
        const int w = 120, h = 8;
        result = m.make(nm("Slider"), parent, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"w", w}, {"h", h}, {"color", color(40, 40, 60)}, {"sortingOrder", 0}})),
                                           Maker::comp("Slider", json::object({{"value", 0.5}}))});
        const std::string fill = m.make(sub("Fill", "Fill"), result, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"w", w / 2}, {"h", h}, {"color", color(240, 160, 30)}, {"sortingOrder", 1}}))});
        const std::string handle = m.make(sub("Handle", "Handle"), result, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", 0}, {"y", dy}, {"w", 8}, {"h", 16}, {"color", color(240, 240, 240)}, {"sortingOrder", 2}}))});
        const int si = m.indexOf(result, "Slider");
        doc.setField(result, si, "fillRect", fill);
        doc.setField(result, si, "handleRect", handle);
        break;
      }
      case UiKind::Toggle: {
        result = m.make(nm("Toggle"), parent, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", -40}, {"y", dy}, {"w", 16}, {"h", 16}, {"color", color(40, 40, 60)}})),
                                           Maker::comp("Toggle", json::object({{"isOn", true}}))});
        const std::string check = m.make(sub("Check", "Check"), result, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", -40}, {"y", dy}, {"w", 10}, {"h", 10}, {"color", color(240, 160, 30)}, {"sortingOrder", 1}}))});
        m.make(sub("Label", "Label"), result, {Maker::comp("UIText", json::object({{"anchor", 3}, {"x", 0}, {"y", dy}, {"label", "Toggle"}}))});
        doc.setField(result, m.indexOf(result, "Toggle"), "graphic", check);
        // The label sits to the right of the box: anchor Center with an offset keeps it independent of the view size.
        const SceneObject* lo = nullptr;
        for (const std::string& c : doc.childrenOf(result)) { const SceneObject* o = findObj(doc, c); if (o && o->name == sub("Label", "Label")) lo = o; }
        if (lo) { doc.setField(lo->guid, 0, "anchor", 4); doc.setField(lo->guid, 0, "x", 20); }
        break;
      }
      case UiKind::LayoutGroup: {
        result = m.make(nm("Layout Group"), parent, {Maker::comp("UIImage", json::object({{"anchor", 4}, {"x", 0}, {"y", 0}, {"w", 220}, {"h", 100}, {"color", color(40, 40, 60, 90)}, {"sortingOrder", -2}})),
                                                  Maker::comp("LayoutGroup", json::object({{"type", 1}, {"spacing", 6}, {"childAlignment", 4}}))});
        break;
      }
      default: break;
    }
  }
  doc.endGesture();
  if (!result.empty()) doc.select(result);
  return result;
}

// ---- preview ----------------------------------------------------------------------------------------------------------------
struct UiPreview::Impl {
  Arena arena;
  World world;
  bool arenaReady = false;
};

UiPreview::UiPreview() : impl_(new Impl) {}
UiPreview::~UiPreview() {
  World* prev = World::active();
  impl_->world.shutdown();
  if (prev != &impl_->world) World::setActive(prev);
}

const UiElement* UiPreview::find(const std::string& guid) const {
  for (const UiElement& e : elements_) if (e.guid == guid) return &e;
  return nullptr;
}

bool UiPreview::rebuild(const SceneDocument& doc, int viewW, int viewH) {
  elements_.clear();
  view_ = UiPreviewView();
  view_.viewW = view_.vw = viewW;
  view_.viewH = view_.vh = viewH;
  if (!doc.loaded() || !sceneHasUi(doc)) return false;
  World* prev = World::active();
  World& world = impl_->world;
  world.shutdown();
  if (!impl_->arenaReady) { impl_->arena.create(8u << 20); impl_->arenaReady = true; }
  impl_->arena.reset();
  WorldConfig cfg;
  cfg.maxObjects = 768;
  cfg.maxComponentsPerType = 512;
  cfg.maxCoroutines = 0;
  cfg.maxInvokes = 0;
  if (!world.init(cfg, impl_->arena)) { World::setActive(prev); return false; }

  struct Made { GameObject* go; const SceneObject* so; };
  std::map<std::string, Made> byGuid;
  std::vector<Made> order;
  for (const SceneObject& so : doc.scene().objects) {
    bool ui = false;
    for (const SceneComponent& c : so.components) ui = ui || isUiType(c.type);
    GameObject* go = world.createObject(so.name.c_str());
    if (!go) continue;
    Guid g;
    if (Guid::parse(so.guid.c_str(), g)) go->guid = g;
    for (const SceneComponent& c : so.components) {
      if (!isUiType(c.type)) continue;
      const TypeInfo* ti = TypeRegistry::instance().find(c.type.c_str());
      Component* comp = ti ? world.addComponent(go, *ti) : nullptr;
      std::string err;
      if (comp) fieldsFromJson(*ti, comp, c.fields, &err);
    }
    (void)ui;
    byGuid[so.guid] = {go, &so};
    order.push_back({go, &so});
  }
  for (const Made& m : order) {
    if (!m.so->parentGuid.empty()) {
      auto it = byGuid.find(m.so->parentGuid);
      if (it != byGuid.end()) m.go->transform.setParent(&it->second.go->transform);
    }
    if (!m.so->active) m.go->selfActive_ = false;
  }
  UI::prepare(world, viewW, viewH);
  const UI::CanvasView cv = UI::canvasView(world, viewW, viewH);
  view_.scale = static_cast<float>(cv.scale);
  view_.vw = cv.vw;
  view_.vh = cv.vh;
  if (const ComponentPool* p = world.pool<Canvas>()) {
    for (u32 i = 0; i < p->highWater && !view_.hasCanvas; ++i) {
      if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
      const Canvas* c = static_cast<const Canvas*>(p->at(i));
      view_.hasCanvas = true;
      view_.referenceW = c->referenceWidth;
      view_.referenceH = c->referenceHeight;
      view_.scaleMode = c->scaleMode;
    }
  }

  // What drives an element: a LayoutGroup parent or a Slider's fill / handle.
  std::map<std::string, std::string> drivenBy;
  for (const Made& m : order) {
    for (const SceneComponent& c : m.so->components) {
      if (c.type == "Slider") {
        for (const char* f : {"fillRect", "handleRect"})
          if (c.fields.contains(f) && c.fields[f].is_string() && !c.fields[f].get<std::string>().empty()) drivenBy[c.fields[f].get<std::string>()] = "Slider " + m.so->name;
      }
      if (c.type == "LayoutGroup" && m.so->active)
        for (const SceneObject& o : doc.scene().objects) if (o.parentGuid == m.so->guid) drivenBy[o.guid] = "LayoutGroup " + m.so->name;
    }
  }

  const Scalar s = cv.scale;
  auto addElem = [&](UiElement& e, const Made& m, const Rect& r) {
    e.guid = m.so->guid;
    e.name = m.so->name;
    e.vx = static_cast<float>(r.x); e.vy = static_cast<float>(r.y); e.vw = static_cast<float>(r.width); e.vh = static_cast<float>(r.height);
    e.x = e.vx * static_cast<float>(s); e.y = e.vy * static_cast<float>(s); e.w = e.vw * static_cast<float>(s); e.h = e.vh * static_cast<float>(s);
    auto d = drivenBy.find(e.guid);
    if (d != drivenBy.end()) { e.driven = true; e.drivenBy = d->second; }
    for (const SceneComponent& c : m.so->components) { if (c.type == "Slider") e.hasSlider = true; if (c.type == "Toggle") e.hasToggle = true; }
    elements_.push_back(e);
  };
  auto compIndex = [&](const Made& m, const char* type) {
    for (size_t i = 0; i < m.so->components.size(); ++i) if (m.so->components[i].type == type) return static_cast<int>(i);
    return -1;
  };
  for (const Made& m : order) {
    GameObject* go = m.go;
    if (!go->activeInHierarchy()) continue;
    if (UIImage* im = go->GetComponent<UIImage>()) {
      if (!im->enabled()) continue;
      UiElement e;
      e.kind = UiElement::Image;
      e.component = compIndex(m, "UIImage");
      e.anchor = im->anchor;
      e.sorting = im->sortingOrder;
      e.color[0] = im->color.r; e.color[1] = im->color.g; e.color[2] = im->color.b; e.color[3] = im->color.a;
      e.fillMethod = im->fillMethod; e.fillAmount = static_cast<float>(im->fillAmount); e.fillFromEnd = im->fillFromEnd;
      e.textured = !im->texture.isNull();
      Rect r = UI::resolveRect(im->anchor, im->x, im->y, im->w, im->h, cv.vw, cv.vh);
      e.focusable = go->GetComponent<Slider>() || go->GetComponent<Toggle>();
      addElem(e, m, r);
    }
    if (UIButton* b = go->GetComponent<UIButton>()) {
      UiElement e;
      e.kind = UiElement::Button;
      e.component = compIndex(m, "UIButton");
      e.anchor = b->anchor;
      e.sorting = b->sortingOrder;
      e.color[0] = b->normalColor.r; e.color[1] = b->normalColor.g; e.color[2] = b->normalColor.b; e.color[3] = b->normalColor.a;
      e.focusable = b->interactable;
      addElem(e, m, UI::resolveRect(b->anchor, b->x, b->y, b->w, b->h, cv.vw, cv.vh));
    }
    if (UIText* t = go->GetComponent<UIText>()) {
      if (!t->enabled()) continue;
      UiElement e;
      e.kind = UiElement::Text;
      e.component = compIndex(m, "UIText");
      e.anchor = t->anchor;
      e.sorting = t->sortingOrder;
      e.hasSize = false;
      e.color[0] = t->color.r; e.color[1] = t->color.g; e.color[2] = t->color.b; e.color[3] = t->color.a;
      e.text = t->label.c_str();
      const int chars = static_cast<int>(e.text.size());
      const int tw = static_cast<int>(static_cast<float>(chars * 8) / static_cast<float>(s) + 0.5f), th = static_cast<int>(8.0f / static_cast<float>(s) + 0.5f);
      addElem(e, m, UI::resolveRect(t->anchor, t->x, t->y, tw, th, cv.vw, cv.vh));
    }
  }
  World::setActive(prev);
  return true;
}

}  // namespace editor
}  // namespace rensga
