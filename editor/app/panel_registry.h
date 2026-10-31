// Editor panel registry (F6 E0). A panel file registers itself with one RENSGA_EDITOR_PANEL(...) block at its end; main.cpp
// loops over the registry (holds, hooks, draws), and CMake globs panels/*.cpp, so a new panel needs no edit in main.cpp.
//
//   static void drawMyPanel(App& a) { ... }
//   static bool myHook(App& a, const std::string& act, const std::string& arg) { ... }   // --ui-event verbs
//   static bool myHolds(App& a, const std::string& event) { ... }                         // true = hold the --ui-event queue
//   RENSGA_EDITOR_PANEL(my_panel, "My Panel", drawMyPanel, myHook, myHolds, 650, 500, rensga::editor::PanelDraw::Body)
//
// Any of draw / hook / holds may be nullptr. Several registrations per file are fine (unique `id` each).
#pragma once
#include <string>
#include <vector>

namespace rensga {
namespace editor {

struct App;

// When the draw function runs (App.hub.mode is Splash, Picker or Editor):
//   Body           inside the editor body (Mode::Editor), between the menu bar and the dialogs, sorted by `order`
//   Overlay        after the body, every mode except the splash (dialogs that also show over the Hub picker)
//   OverlayEditor  after the body, Mode::Editor only (floating windows)
enum class PanelDraw { None, Body, Overlay, OverlayEditor };

struct EditorPanel {
  const char* name;
  void (*draw)(App&);
  bool (*hook)(App&, const std::string& act, const std::string& arg);  // --ui-event verb; true = handled (first hit wins)
  bool (*holds)(App&, const std::string& event);                       // true = hold the --ui-event queue this frame
  int order;                                                           // draw order inside its PanelDraw phase (ascending)
  int hookOrder;                                                       // hook precedence (ascending; a verb is offered to one hook at a time)
  PanelDraw phase = PanelDraw::Body;
};

void registerEditorPanel(const EditorPanel& p);
// Sorted copies (stable on registration order for equal keys).
const std::vector<const EditorPanel*>& panelsByDrawOrder();
const std::vector<const EditorPanel*>& panelsByHookOrder();

// Frame helpers used by main.cpp.
bool panelHolds(App& a, const std::string& event);                                  // any registered holds()
bool panelHook(App& a, const std::string& act, const std::string& arg);             // first registered hook that handles it
void drawPanels(App& a, PanelDraw phase);                                           // Body, or Overlay (= Overlay + OverlayEditor, mode-checked here)

struct PanelRegistrar {
  explicit PanelRegistrar(const EditorPanel& p) { registerEditorPanel(p); }
};

#define RENSGA_EDITOR_PANEL(id, ...) static ::rensga::editor::PanelRegistrar id##_registrar_{::rensga::editor::EditorPanel{__VA_ARGS__}};

}  // namespace editor
}  // namespace rensga
