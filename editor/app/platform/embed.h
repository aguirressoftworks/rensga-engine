// F5.23: put the emulator's top-level window inside the editor window (docked Emulator panel).
// One interface, three backends: x11_embed.cpp (XReparentWindow), win32_embed.cpp (SetParent) and the floating fallback
// (no reparenting: the emulator window is moved and raised over the panel, or only raised where the platform cannot move it,
// e.g. native Wayland). The backend knows no console: it finds "the window of a descendant process of this editor".
#pragma once
#include <cstdint>
#include <string>

struct SDL_Window;

namespace rensga {
namespace editor {
namespace embed {

enum class Mode { Off, Docked, Floating };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;  // pixels, relative to the editor window's client area
};

// Picks the backend from the editor's SDL window. RENSGA_EMBED=auto|dock|float|off|wayland overrides (float = floating window over the panel; wayland = pretend there is no X11 window, the native-Wayland path).
// Returns the mode that will be used: Off when embedding is not possible at all.
Mode init(SDL_Window* editorWindow);
Mode mode();
const char* backendName();          // "x11" | "win32" | "none"
std::string describe();             // one line: backend, mode, reason for a fallback

// The top-level window (X11 window id / HWND) of a process that is a descendant of this editor, 0 = none yet.
// `titleHint` narrows the match when several windows are found ("" = any).
uint64_t findEmulatorWindow();

// Docked: reparents `emu` into the editor window. Floating: remembers it. False when the window vanished.
bool attach(uint64_t emu);
bool attached();
uint64_t window();                  // the attached emulator window (0 = none)
void detach();                      // gives the window back (docked: reparent to the root) and forgets it

// Every frame while attached: where the panel's content area is (editor-window pixels) and whether it is visible.
// Docked: moves/resizes the child, maps/unmaps it. Floating: moves/resizes it over the same screen rectangle and raises it.
void place(const Rect& area, bool visible);

// Focus follows the mouse: true while the pointer is over the panel area. Gives the keyboard to the emulator while it is,
// back to the editor when it leaves. Returns whether the pointer is over the emulator now.
bool followFocus(const Rect& area, bool panelVisible);

// Test aid: window tree check. Fills `out` with lines "emu=<id> parent=<id> editor=<id> descendant=<0|1> rect=x,y,w,h
// mapped=<0|1>" and returns true when the emulator is a child of the editor window (docked) or a top-level window (floating).
bool dump(std::string& out);

// Test aid: moves the mouse pointer into (true) or out of (false) the panel area, so a test can see the focus follow it.
bool warpPointer(const Rect& area, bool inside);

// Test aid: the screen rectangle the emulator window currently covers (root coordinates), false when unknown.
bool screenRect(Rect& r);

}  // namespace embed
}  // namespace editor
}  // namespace rensga
