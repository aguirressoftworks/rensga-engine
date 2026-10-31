// F5.23: X11 backend of the emulator embed (platform/embed.h). Docked = XReparentWindow of the emulator's top-level window
// into the editor window, placed over the Emulator panel. Floating = the emulator keeps its own top-level window, which is
// moved over the panel's screen rectangle and raised. The emulator's window is found by _NET_WM_PID: any process that
// descends from this editor.
#if !defined(_WIN32)
#include "platform/embed.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#if defined(RENSGA_HAVE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dirent.h>
#include <unistd.h>

#include <fstream>
#endif

namespace rensga {
namespace editor {
namespace embed {

namespace {
Mode g_mode = Mode::Off;
std::string g_why = "not initialised";
const char* g_backend = "none";
}  // namespace

Mode mode() { return g_mode; }
const char* backendName() { return g_backend; }
std::string describe() {
  const char* m = g_mode == Mode::Docked ? "docked" : g_mode == Mode::Floating ? "floating" : "off";
  return std::string("embed: backend=") + g_backend + " mode=" + m + (g_why.empty() ? "" : " (" + g_why + ")");
}

#if defined(RENSGA_HAVE_X11)

namespace {

Display* g_dpy = nullptr;
Window g_editor = 0, g_emu = 0, g_root = 0, g_prev = 0;  // g_prev: the window given back by detach()
bool g_mapped = false, g_focusEmu = false, g_nativeX11 = false;
Rect g_last;
int g_tick = 0;

// Errors on windows that vanish (the emulator quit) must not kill the editor: swallow them around our own requests.
int g_errors = 0;
int swallow(Display*, XErrorEvent*) { ++g_errors; return 0; }
struct Guard {
  XErrorHandler old;
  Guard() : old(XSetErrorHandler(swallow)) { g_errors = 0; }
  ~Guard() {
    XSync(g_dpy, False);
    XSetErrorHandler(old);
  }
};

// Process ids that descend from this process, read from /proc (parent links).
std::set<long> descendants() {
  std::set<long> all;
  std::set<std::pair<long, long>> links;  // pid, ppid
  if (DIR* dir = opendir("/proc")) {
    while (dirent* e = readdir(dir)) {
      char* end = nullptr;
      long pid = std::strtol(e->d_name, &end, 10);
      if (!end || *end || pid <= 0) continue;
      std::ifstream f(std::string("/proc/") + e->d_name + "/stat");
      std::string s;
      std::getline(f, s);
      size_t rp = s.rfind(')');
      if (rp == std::string::npos) continue;
      char st;
      long ppid = 0;
      if (std::sscanf(s.c_str() + rp + 1, " %c %ld", &st, &ppid) == 2) links.insert({pid, ppid});
    }
    closedir(dir);
  }
  std::set<long> frontier{static_cast<long>(getpid())};
  while (!frontier.empty()) {
    std::set<long> next;
    for (const auto& l : links)
      if (frontier.count(l.second) && !all.count(l.first)) { all.insert(l.first); next.insert(l.first); }
    frontier = next;
  }
  return all;
}

long windowPid(Window w) {
  static Atom pidAtom = XInternAtom(g_dpy, "_NET_WM_PID", False);
  Atom type;
  int fmt;
  unsigned long n, after;
  unsigned char* data = nullptr;
  long pid = -1;
  if (XGetWindowProperty(g_dpy, w, pidAtom, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data) {
    if (type == XA_CARDINAL && fmt == 32 && n == 1) pid = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
    XFree(data);
  }
  return pid;
}

bool viableTop(Window w) {
  XWindowAttributes a;
  return XGetWindowAttributes(g_dpy, w, &a) && a.c_class == InputOutput && a.width > 16 && a.height > 16;
}

Window search(Window parent, const std::set<long>& pids, int depth) {
  Window root, par, *kids = nullptr;
  unsigned n = 0;
  if (!XQueryTree(g_dpy, parent, &root, &par, &kids, &n)) return 0;
  Window found = 0;
  for (unsigned i = 0; i < n && !found; ++i) {
    if (kids[i] == g_editor) continue;
    long pid = windowPid(kids[i]);
    if (pid > 0 && pids.count(pid) && viableTop(kids[i])) found = kids[i];
  }
  for (unsigned i = 0; i < n && !found && depth > 0; ++i)
    if (kids[i] != g_editor) found = search(kids[i], pids, depth - 1);
  if (kids) XFree(kids);
  return found;
}

void rootOrigin(Window w, int& x, int& y) {
  Window child;
  x = y = 0;
  XTranslateCoordinates(g_dpy, w, g_root, 0, 0, &x, &y, &child);
}

bool alive() {
  if (!g_emu) return false;
  Guard guard;
  XWindowAttributes a;
  const Status ok = XGetWindowAttributes(g_dpy, g_emu, &a);
  XSync(g_dpy, False);
  return ok && g_errors == 0;
}

}  // namespace

Mode init(SDL_Window* win) {
  const char* env = std::getenv("RENSGA_EMBED");
  const std::string want = env && *env ? env : "auto";
  g_mode = Mode::Off;
  g_backend = "none";
  if (want == "off") { g_why = "RENSGA_EMBED=off"; return g_mode; }
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  g_nativeX11 = false;
  if (want != "wayland" && win && SDL_GetWindowWMInfo(win, &info) && info.subsystem == SDL_SYSWM_X11) {  // RENSGA_EMBED=wayland forces the no-X11 branch
    g_nativeX11 = true;
    g_editor = info.info.x11.window;
  }
  if (!g_nativeX11) {
    // Native Wayland (or no windowing info): a client cannot reparent or position foreign windows. The emulator stays its own
    // window, next to the editor; the panel says so.
    g_backend = "x11";
    g_mode = Mode::Floating;
    g_why = "no X11 window for the editor (native Wayland): the emulator stays a separate window, no reparenting";
    return g_mode;
  }
  if (!g_dpy) g_dpy = XOpenDisplay(nullptr);
  if (!g_dpy) { g_why = "cannot open the X display"; return g_mode; }
  g_root = DefaultRootWindow(g_dpy);
  g_backend = "x11";
  if (want == "float") { g_mode = Mode::Floating; g_why = "RENSGA_EMBED=float: floating window over the panel"; }
  else { g_mode = Mode::Docked; g_why.clear(); }
  return g_mode;
}

uint64_t findEmulatorWindow() {
  if (!g_dpy || !g_nativeX11) return 0;
  Guard guard;
  return search(g_root, descendants(), 2);
}

bool attach(uint64_t emu) {
  if (!emu) return false;
  g_emu = static_cast<Window>(emu);
  g_prev = 0;
  g_mapped = true;
  g_focusEmu = false;
  g_last = Rect();
  if (!g_dpy || !g_nativeX11) return true;  // Wayland fallback: nothing to move
  Guard guard;
  if (g_mode == Mode::Docked) {
    // No decorations when a window manager draws them (Motif hint), then move the window into the editor window.
    struct { unsigned long flags, functions, decorations; long input; unsigned long status; } mwm = {2, 0, 0, 0, 0};
    Atom hints = XInternAtom(g_dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(g_dpy, g_emu, hints, hints, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&mwm), 5);
    XReparentWindow(g_dpy, g_emu, g_editor, 0, 0);
    XMapRaised(g_dpy, g_emu);
  }
  XSync(g_dpy, False);
  if (g_errors) { g_emu = 0; return false; }
  return true;
}

bool attached() { return g_emu != 0; }
uint64_t window() { return g_emu; }

void detach() {
  if (!g_emu) return;
  if (g_dpy && g_nativeX11 && g_mode == Mode::Docked) {
    Guard guard;
    XUnmapWindow(g_dpy, g_emu);
    XReparentWindow(g_dpy, g_emu, g_root, 40, 40);
    XMapRaised(g_dpy, g_emu);
    XSync(g_dpy, False);
  }
  g_prev = g_emu;
  g_emu = 0;
  g_mapped = g_focusEmu = false;
}

void place(const Rect& area, bool visible) {
  if (!g_emu || !g_dpy || !g_nativeX11) return;
  if (++g_tick % 20 == 0 && !alive()) { g_emu = 0; return; }  // the emulator quit
  Guard guard;
  const bool show = visible && area.w > 8 && area.h > 8;
  if (!show) {
    if (g_mapped) { XUnmapWindow(g_dpy, g_emu); g_mapped = false; }
    return;
  }
  int ox = 0, oy = 0;
  if (g_mode == Mode::Floating) rootOrigin(g_editor, ox, oy);
  const Rect want{area.x + ox, area.y + oy, area.w, area.h};
  const bool moved = want.x != g_last.x || want.y != g_last.y || want.w != g_last.w || want.h != g_last.h;
  if (moved) {
    XMoveResizeWindow(g_dpy, g_emu, want.x, want.y, static_cast<unsigned>(want.w), static_cast<unsigned>(want.h));
    g_last = want;
  }
  if (!g_mapped) { XMapRaised(g_dpy, g_emu); g_mapped = true; }
  else if (moved || (g_mode == Mode::Floating && g_tick % 30 == 0)) XRaiseWindow(g_dpy, g_emu);  // floating: kept above the editor
  XFlush(g_dpy);
}

bool followFocus(const Rect& area, bool panelVisible) {
  if (!g_emu || !g_dpy || !g_nativeX11) return false;
  Guard guard;
  Window rootRet, childRet;
  int rx, ry, wx, wy;
  unsigned mask;
  if (!XQueryPointer(g_dpy, g_root, &rootRet, &childRet, &rx, &ry, &wx, &wy, &mask)) return false;
  int ox, oy;
  rootOrigin(g_editor, ox, oy);
  const bool inside = panelVisible && rx >= ox + area.x && rx < ox + area.x + area.w && ry >= oy + area.y && ry < oy + area.y + area.h;
  if (inside && !g_focusEmu) {
    XSetInputFocus(g_dpy, g_emu, RevertToParent, CurrentTime);
    g_focusEmu = true;
  } else if (!inside && g_focusEmu) {
    XSetInputFocus(g_dpy, g_editor, RevertToParent, CurrentTime);
    g_focusEmu = false;
  }
  XFlush(g_dpy);
  return inside;
}

bool dump(std::string& out) {
  out.clear();
  const Window w = g_emu ? g_emu : g_prev;
  if (!g_dpy || !g_nativeX11 || !w) { out = "emu=0 (not attached)"; return false; }
  Guard guard;
  Window root, parent, *kids = nullptr;
  unsigned n = 0;
  Window par = 0;
  if (XQueryTree(g_dpy, w, &root, &par, &kids, &n) && kids) XFree(kids);
  // Is the editor window an ancestor?
  bool desc = false;
  for (Window w = par; w && w != g_root; ) {
    if (w == g_editor) { desc = true; break; }
    Window r2, p2, *k2 = nullptr;
    unsigned n2 = 0;
    if (!XQueryTree(g_dpy, w, &r2, &p2, &k2, &n2)) break;
    if (k2) XFree(k2);
    w = p2;
  }
  (void)parent;
  XWindowAttributes a;
  XGetWindowAttributes(g_dpy, w, &a);
  Window input = 0;
  int revert = 0;
  XGetInputFocus(g_dpy, &input, &revert);
  char buf[256];
  const char* state = !g_emu ? "released" : g_mode == Mode::Docked ? "docked" : "floating";
  std::snprintf(buf, sizeof buf, "emu=0x%lx parent=0x%lx editor=0x%lx descendant=%d rect=%d,%d,%d,%d mapped=%d focus=%s state=%s", w, par, g_editor,
                desc ? 1 : 0, a.x, a.y, a.width, a.height, a.map_state != IsUnmapped ? 1 : 0, input == w ? "emulator" : input == g_editor ? "editor" : "other", state);
  out = buf;
  if (!g_emu) return par == g_root;  // released: back to a top-level window
  return g_mode == Mode::Docked ? desc : par == g_root;
}

bool warpPointer(const Rect& area, bool inside) {
  if (!g_dpy || !g_nativeX11) return false;
  Guard guard;
  if (inside) XWarpPointer(g_dpy, None, g_editor, 0, 0, 0, 0, area.x + area.w / 2, area.y + area.h / 2);
  else XWarpPointer(g_dpy, None, g_editor, 0, 0, 0, 0, 4, 4);
  XFlush(g_dpy);
  return true;
}

bool screenRect(Rect& r) {
  if (!g_emu || !g_dpy || !g_nativeX11) return false;
  Guard guard;
  XWindowAttributes a;
  if (!XGetWindowAttributes(g_dpy, g_emu, &a)) return false;
  int x, y;
  rootOrigin(g_emu, x, y);
  r = Rect{x, y, a.width, a.height};
  return true;
}

#else  // no libX11 headers at build time

Mode init(SDL_Window*) { g_why = "built without X11"; return g_mode = Mode::Off; }
uint64_t findEmulatorWindow() { return 0; }
bool attach(uint64_t) { return false; }
bool attached() { return false; }
uint64_t window() { return 0; }
void detach() {}
void place(const Rect&, bool) {}
bool followFocus(const Rect&, bool) { return false; }
bool dump(std::string& out) { out = "emu=0 (no X11)"; return false; }
bool warpPointer(const Rect&, bool) { return false; }
bool screenRect(Rect&) { return false; }

#endif

}  // namespace embed
}  // namespace editor
}  // namespace rensga
#endif  // !_WIN32
