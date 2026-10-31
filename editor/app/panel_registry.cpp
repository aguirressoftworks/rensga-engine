#include "panel_registry.h"

#include <algorithm>

#include "app.h"

namespace rensga {
namespace editor {

namespace {
struct Store {
  std::vector<EditorPanel> all;  // reserved, so pointers into it stay valid after sorting copies are built
  std::vector<const EditorPanel*> byDraw, byHook;
  bool dirty = true;
};
Store& store() {
  static Store s;
  return s;
}
void rebuild() {
  Store& s = store();
  if (!s.dirty) return;
  s.byDraw.clear();
  s.byHook.clear();
  for (const EditorPanel& p : s.all) {
    if (p.draw) s.byDraw.push_back(&p);
    if (p.hook || p.holds) s.byHook.push_back(&p);
  }
  std::stable_sort(s.byDraw.begin(), s.byDraw.end(), [](const EditorPanel* a, const EditorPanel* b) { return a->order < b->order; });
  std::stable_sort(s.byHook.begin(), s.byHook.end(), [](const EditorPanel* a, const EditorPanel* b) { return a->hookOrder < b->hookOrder; });
  s.dirty = false;
}
}  // namespace

void registerEditorPanel(const EditorPanel& p) {
  Store& s = store();
  if (s.all.capacity() == 0) s.all.reserve(256);  // never reallocates: registration happens during static init only
  s.all.push_back(p);
  s.dirty = true;
}

const std::vector<const EditorPanel*>& panelsByDrawOrder() { rebuild(); return store().byDraw; }
const std::vector<const EditorPanel*>& panelsByHookOrder() { rebuild(); return store().byHook; }

bool panelHolds(App& a, const std::string& event) {
  for (const EditorPanel* p : panelsByHookOrder()) if (p->holds && p->holds(a, event)) return true;
  return false;
}

bool panelHook(App& a, const std::string& act, const std::string& arg) {
  for (const EditorPanel* p : panelsByHookOrder()) if (p->hook && p->hook(a, act, arg)) return true;
  return false;
}

void drawPanels(App& a, PanelDraw phase) {
  for (const EditorPanel* p : panelsByDrawOrder()) {
    const bool overlay = phase == PanelDraw::Overlay;  // one pass: Overlay and OverlayEditor entries interleave by `order`
    if (overlay ? (p->phase != PanelDraw::Overlay && p->phase != PanelDraw::OverlayEditor) : p->phase != phase) continue;
    if (p->phase == PanelDraw::OverlayEditor && a.hub.mode != Mode::Editor) continue;
    if (overlay && a.hub.mode == Mode::Splash) continue;
    p->draw(a);
  }
}

}  // namespace editor
}  // namespace rensga
