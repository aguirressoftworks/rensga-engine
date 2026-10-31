// F5.23 (docked Emulator panel) and F5.10 (Profiler panel, PPSSPP debugger): state, panels and --ui-event verbs.
#pragma once
#include <string>

#include "app.h"

namespace rensga {
namespace editor {

struct DockDebugState {
  bool showEmulator = true, showProfiler = true;
};
DockDebugState& dockDebug();

void initEmbed(void* sdlWindow);            // once, after the editor window exists (SDL_Window*)
void shutdownEmbed();                       // before the window goes: gives the emulator window back
void drawEmulatorView(App& a);              // panel "Emulator": the emulator window docked inside (panels/emulator_view.cpp)
void drawProfiler(App& a);                  // panel "Profiler": frame time graph, budgets, PPSSPP debugger (panels/profiler.cpp)

// --ui-event verbs: dockemu | undockemu | dumpembed | embedpointer:in|out | emulator (focus the tab) | profiler (focus the tab) |
// profilerdump | profilerclear | profilerdebugger | profilerfeed:<raw log line> | profilerport | profilerwait:<seconds>
// Output lines start with "dock:" and "profiler:".
bool dockUiHook(App& a, const std::string& act, const std::string& arg);
bool profilerUiHook(App& a, const std::string& act, const std::string& arg);
bool profilerUiHolds(App& a, const std::string& event);   // profilerwait:<seconds> holds the --ui-event queue until the PPSSPP debugger answers

}  // namespace editor
}  // namespace rensga
