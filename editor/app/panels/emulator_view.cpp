// F5.23: the docked Emulator panel. While Play on console runs, the emulator's own window is moved into the editor window and
// kept over this panel's content area (platform/embed.h). The panel is an ordinary dockable tab: it follows dock changes
// (resize, tab switches, collapse) every frame. Focus follows the mouse: the keyboard goes to the emulator while the pointer is
// over the panel. Where the platform cannot reparent (native Wayland) or RENSGA_EMBED=float is set, the emulator keeps its own
// window and the panel only says so.
#include <cstdio>
#include "panel_registry.h"
#include <cstdlib>
#include <cstring>

#include "dock_debug.h"
#include "platform/embed.h"

namespace rensga {
namespace editor {

DockDebugState& dockDebug() {
  static DockDebugState s;
  return s;
}

namespace {

struct EmuView {
  bool userDocked = true;     // false after Undock: the emulator keeps its own window
  bool focusTab = false;      // bring the tab to the front when the window first appears
  int scan = 0;
  embed::Rect area;           // last content area (editor-window pixels)
  bool visible = false;
  bool hovered = false;
  bool wasRunning = false;
};
EmuView g_v;

const char* modeName() {
  switch (embed::mode()) {
    case embed::Mode::Docked: return "docked";
    case embed::Mode::Floating: return "floating";
    default: return "off";
  }
}

// Keep the attachment in step with the Play session: attach when the window appears, detach when the run ends.
void syncAttachment(App& a) {
  const bool running = a.play.status() == PlayStatus::Running;
  if (embed::mode() == embed::Mode::Off) return;
  if (!running) {
    if (embed::attached()) { embed::detach(); EditorLog::instance().info("embed: emulator run ended, window released"); }
    g_v.wasRunning = false;
    return;
  }
  if (!g_v.wasRunning) { g_v.wasRunning = true; g_v.scan = 0; }
  if (embed::attached() || !g_v.userDocked) return;
  if (g_v.scan++ % 6 != 0) return;  // look for the window about 10 times a second
  const uint64_t w = embed::findEmulatorWindow();
  if (!w) return;
  if (embed::attach(w)) {
    char b[160];
    std::snprintf(b, sizeof b, "embed: attached emulator window 0x%llx (%s)", static_cast<unsigned long long>(w), modeName());
    EditorLog::instance().info(b);
    g_v.focusTab = true;
    dockDebug().showEmulator = true;
  }
}

}  // namespace

void initEmbed(void* sdlWindow) {
  embed::init(static_cast<SDL_Window*>(sdlWindow));
  EditorLog::instance().info(embed::describe());
}

void shutdownEmbed() { embed::detach(); }

void drawEmulatorView(App& a) {
  if (!a.loaded) return;
  syncAttachment(a);
  DockDebugState& st = dockDebug();
  if (!st.showEmulator) { embed::place(g_v.area, false); return; }
  if (g_v.focusTab) { ImGui::SetNextWindowFocus(); g_v.focusTab = false; }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const bool open = ImGui::Begin("Emulator", &st.showEmulator, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) {
    g_v.visible = false;
    ImGui::End();
    embed::place(g_v.area, false);
    return;
  }
  const bool running = a.play.status() == PlayStatus::Running;
  const bool have = embed::attached();
  ImGui::SetCursorPos(ImVec2(8, ImGui::GetCursorPosY() + 6));  // below the tab bar (docked windows start their cursor under it)
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(brandToken("text-2"), "%s", running ? (have ? (embed::mode() == embed::Mode::Docked ? "Emulator docked" : "Emulator floating over this panel")
                                                                 : "Emulator starting...")
                                                        : "Emulator not running");
  if (have || (running && !g_v.userDocked)) {
    ImGui::SameLine();
    if (g_v.userDocked) {
      if (ImGui::SmallButton("Undock")) { embed::detach(); g_v.userDocked = false; EditorLog::instance().info("embed: undocked by the user"); }
    }
  }
  if (!g_v.userDocked && ImGui::SmallButton("Dock")) g_v.userDocked = true;
  ImGui::SetCursorPos(ImVec2(0, ImGui::GetCursorPosY() + 2));

  // Content area = everything below the header row.
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImVec2 sz = ImGui::GetContentRegionAvail();
  if (sz.x < 1) sz.x = 1;
  if (sz.y < 1) sz.y = 1;
  const ImVec2 origin = ImGui::GetMainViewport()->Pos;
  g_v.area = embed::Rect{static_cast<int>(p0.x - origin.x), static_cast<int>(p0.y - origin.y), static_cast<int>(sz.x), static_cast<int>(sz.y)};
  ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("surface-2")));
  ImGui::InvisibleButton("##emuarea", sz);
  if (!have) {
    const char* msg = running ? "Waiting for the emulator window..." : "Press Play (Play on console) to run the game in the emulator here.";
    const ImVec2 ts = ImGui::CalcTextSize(msg);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + (sz.x - ts.x) * 0.5f, p0.y + (sz.y - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), msg);
    if (running && embed::describe().find("Wayland") != std::string::npos) {
      const char* m3 = "Native Wayland cannot dock a foreign window: the emulator stays a separate window next to the editor.";
      const ImVec2 t3 = ImGui::CalcTextSize(m3);
      ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + (sz.x - t3.x) * 0.5f, p0.y + sz.y * 0.5f + 20), ImGui::GetColorU32(brandToken("warn")), m3);
    } else if (!g_v.userDocked && running) {
      const char* m2 = "Undocked: the emulator is its own window.";
      const ImVec2 t2 = ImGui::CalcTextSize(m2);
      ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + (sz.x - t2.x) * 0.5f, p0.y + sz.y * 0.5f + 20), ImGui::GetColorU32(brandToken("text-2")), m2);
    }
  }
  g_v.visible = true;
  ImGui::End();

  if (have) {
    embed::place(g_v.area, g_v.visible);
    g_v.hovered = embed::followFocus(g_v.area, g_v.visible);
  }
}

bool dockUiHook(App& a, const std::string& act, const std::string& arg) {
  if (act == "dockemu") {
    g_v.userDocked = true;
    if (!embed::attached()) {
      const uint64_t w = embed::findEmulatorWindow();
      if (w) embed::attach(w);
    }
    std::printf("dock: dockemu mode=%s attached=%d\n", modeName(), embed::attached() ? 1 : 0);
    return true;
  }
  if (act == "undockemu") {
    embed::detach();
    g_v.userDocked = false;
    std::printf("dock: undockemu attached=%d\n", embed::attached() ? 1 : 0);
    return true;
  }
  if (act == "emulator") { dockDebug().showEmulator = true; g_v.focusTab = true; return true; }
  if (act == "embedpointer") {
    const bool in = arg != "out";
    std::printf("dock: pointer %s ok=%d\n", in ? "in" : "out", embed::warpPointer(g_v.area, in) ? 1 : 0);
    return true;
  }
  if (act == "dumpembed") {
    std::string line;
    const bool ok = embed::dump(line);
    std::printf("dock: %s\ndock: area=%d,%d,%d,%d visible=%d hovered=%d mode=%s backend=%s\ndock: check=%s\n", line.c_str(), g_v.area.x, g_v.area.y,
                g_v.area.w, g_v.area.h, g_v.visible ? 1 : 0, g_v.hovered ? 1 : 0, modeName(), embed::backendName(), ok ? "ok" : "FAIL");
    std::printf("%s\n", embed::describe().c_str());
    std::fflush(stdout);
    (void)a;
    return true;
  }
  return false;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(emulator_view, "Emulator", drawEmulatorView, dockUiHook, nullptr, 80, 40, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
