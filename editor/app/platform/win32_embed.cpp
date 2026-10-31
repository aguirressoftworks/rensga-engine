// F5.23: Win32 backend of the emulator embed (platform/embed.h): SetParent of the emulator's top-level window into the editor
// window, placed over the Emulator panel. Compiled for the mingw build (cannot be run on the Linux dev machine). The emulator
// window is found by process id: any process that descends from the editor.
#if defined(_WIN32)
#include "platform/embed.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

namespace rensga {
namespace editor {
namespace embed {

namespace {
Mode g_mode = Mode::Off;
std::string g_why = "not initialised";
HWND g_editor = nullptr, g_emu = nullptr, g_prev = nullptr;  // g_prev: the window given back by detach()
bool g_mapped = false, g_focusEmu = false;
LONG_PTR g_oldStyle = 0;
Rect g_last;
int g_tick = 0;

std::set<DWORD> descendants() {
  std::map<DWORD, DWORD> parentOf;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) parentOf[pe.th32ProcessID] = pe.th32ParentProcessID;
    CloseHandle(snap);
  }
  std::set<DWORD> all;
  bool grew = true;
  while (grew) {
    grew = false;
    for (const auto& kv : parentOf)
      if (!all.count(kv.first) && (kv.second == GetCurrentProcessId() || all.count(kv.second))) { all.insert(kv.first); grew = true; }
  }
  return all;
}

struct Find {
  std::set<DWORD> pids;
  HWND found = nullptr;
};
BOOL CALLBACK enumTop(HWND w, LPARAM lp) {
  Find* f = reinterpret_cast<Find*>(lp);
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  RECT r;
  if (pid && f->pids.count(pid) && IsWindowVisible(w) && GetWindowRect(w, &r) && r.right - r.left > 16 && r.bottom - r.top > 16 &&
      GetWindow(w, GW_OWNER) == nullptr) {
    f->found = w;
    return FALSE;
  }
  return TRUE;
}
}  // namespace

Mode init(SDL_Window* win) {
  const char* env = std::getenv("RENSGA_EMBED");
  const std::string want = env && *env ? env : "auto";
  g_mode = Mode::Off;
  if (want == "off") { g_why = "RENSGA_EMBED=off"; return g_mode; }
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  if (!win || !SDL_GetWindowWMInfo(win, &info) || info.subsystem != SDL_SYSWM_WINDOWS) { g_why = "no HWND for the editor"; return g_mode; }
  g_editor = info.info.win.window;
  if (want == "float") { g_mode = Mode::Floating; g_why = "RENSGA_EMBED=float: floating window over the panel"; }
  else { g_mode = Mode::Docked; g_why.clear(); }
  return g_mode;
}

Mode mode() { return g_mode; }
const char* backendName() { return "win32"; }
std::string describe() {
  const char* m = g_mode == Mode::Docked ? "docked" : g_mode == Mode::Floating ? "floating" : "off";
  return std::string("embed: backend=win32 mode=") + m + (g_why.empty() ? "" : " (" + g_why + ")");
}

uint64_t findEmulatorWindow() {
  Find f;
  f.pids = descendants();
  EnumWindows(enumTop, reinterpret_cast<LPARAM>(&f));
  return reinterpret_cast<uint64_t>(f.found);
}

bool attach(uint64_t emu) {
  g_emu = reinterpret_cast<HWND>(emu);
  g_prev = nullptr;
  if (!g_emu || !IsWindow(g_emu)) { g_emu = nullptr; return false; }
  g_mapped = true;
  g_focusEmu = false;
  g_last = Rect();
  if (g_mode == Mode::Docked) {
    g_oldStyle = GetWindowLongPtr(g_emu, GWL_STYLE);
    SetWindowLongPtr(g_emu, GWL_STYLE, (g_oldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_POPUP | WS_SYSMENU)) | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS);
    SetParent(g_emu, g_editor);
    SetWindowPos(g_emu, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  }
  return true;
}

bool attached() { return g_emu != nullptr; }
uint64_t window() { return reinterpret_cast<uint64_t>(g_emu); }

void detach() {
  if (!g_emu) return;
  if (g_mode == Mode::Docked && IsWindow(g_emu)) {
    ShowWindow(g_emu, SW_HIDE);
    SetParent(g_emu, nullptr);
    SetWindowLongPtr(g_emu, GWL_STYLE, g_oldStyle);
    SetWindowPos(g_emu, HWND_TOP, 40, 40, 0, 0, SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
  }
  g_prev = g_emu;
  g_emu = nullptr;
  g_mapped = g_focusEmu = false;
}

void place(const Rect& area, bool visible) {
  if (!g_emu) return;
  if (++g_tick % 20 == 0 && !IsWindow(g_emu)) { g_emu = nullptr; return; }
  const bool show = visible && area.w > 8 && area.h > 8;
  if (!show) {
    if (g_mapped) { ShowWindow(g_emu, SW_HIDE); g_mapped = false; }
    return;
  }
  Rect want = area;
  if (g_mode == Mode::Floating) {
    POINT p = {0, 0};
    ClientToScreen(g_editor, &p);
    want.x += p.x;
    want.y += p.y;
  }
  const bool moved = want.x != g_last.x || want.y != g_last.y || want.w != g_last.w || want.h != g_last.h;
  if (moved) {
    SetWindowPos(g_emu, HWND_TOP, want.x, want.y, want.w, want.h, SWP_NOACTIVATE);
    g_last = want;
  }
  if (!g_mapped) { ShowWindow(g_emu, SW_SHOWNOACTIVATE); g_mapped = true; }
}

bool followFocus(const Rect& area, bool panelVisible) {
  if (!g_emu) return false;
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(g_editor, &p);
  const bool inside = panelVisible && p.x >= area.x && p.x < area.x + area.w && p.y >= area.y && p.y < area.y + area.h;
  if (inside && !g_focusEmu) {
    // Input queues are per thread: attach ours to the emulator's thread for the SetFocus.
    DWORD emuThread = GetWindowThreadProcessId(g_emu, nullptr);
    AttachThreadInput(GetCurrentThreadId(), emuThread, TRUE);
    SetFocus(g_emu);
    AttachThreadInput(GetCurrentThreadId(), emuThread, FALSE);
    g_focusEmu = true;
  } else if (!inside && g_focusEmu) {
    SetFocus(g_editor);
    g_focusEmu = false;
  }
  return inside;
}

bool dump(std::string& out) {
  const HWND w = g_emu ? g_emu : g_prev;
  if (!w || !IsWindow(w)) { out = "emu=0 (not attached)"; return false; }
  RECT r;
  GetWindowRect(w, &r);
  const bool child = GetParent(w) == g_editor;
  char buf[240];
  std::snprintf(buf, sizeof buf, "emu=%p parent=%p editor=%p descendant=%d rect=%ld,%ld,%ld,%ld mapped=%d focus=? state=%s", static_cast<void*>(w),
                static_cast<void*>(GetParent(w)), static_cast<void*>(g_editor), child ? 1 : 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                IsWindowVisible(w) ? 1 : 0, !g_emu ? "released" : g_mode == Mode::Docked ? "docked" : "floating");
  out = buf;
  if (!g_emu) return !child;
  return g_mode == Mode::Docked ? child : true;
}

bool warpPointer(const Rect& area, bool inside) {
  POINT p = {inside ? area.x + area.w / 2 : 4, inside ? area.y + area.h / 2 : 4};
  ClientToScreen(g_editor, &p);
  return SetCursorPos(p.x, p.y) != 0;
}

bool screenRect(Rect& r) {
  RECT w;
  if (!g_emu || !GetWindowRect(g_emu, &w)) return false;
  r = Rect{static_cast<int>(w.left), static_cast<int>(w.top), static_cast<int>(w.right - w.left), static_cast<int>(w.bottom - w.top)};
  return true;
}

}  // namespace embed
}  // namespace editor
}  // namespace rensga
#endif  // _WIN32
