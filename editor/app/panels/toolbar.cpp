// Fixed strip under the menu bar: pequi + project name (left), Play/Stop (center), target + status (right).
#include <imgui_internal.h>

#include "app.h"
#include "panel_registry.h"
#include "modules.h"
#include "emulator_manager.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

const TargetModule* App::targetModule() const { return TargetRegistry::find(target); }

// The Play button and the Edit > Play item: follows the Play dropdown (Play in editor by default).
void doPlay(App& a) {
  if (a.playInEditor) { startPlayInEditor(a); return; }
  doPlayConsole(a);
}

// Dev build + emulator launch of the project's target; --play calls it directly.
void doPlayConsole(App& a) {
  if (!a.loaded) { EditorLog::instance().error("play: no project loaded"); return; }
  if (a.target.empty()) { EditorLog::instance().error("play: the project's target (" + a.project.target + ") has no editor module; console run is not available"); return; }
  // The target's build support module must be installed: otherwise ask (Install now?) and continue after the install.
  if (!gateTargetModule(a, a.target, AfterModule::PlayConsole)) return;
  // The emulator must be there too: otherwise offer to install it (F5.04) and continue after.
  if (!gateTargetEmulator(a, a.target, AfterEmulator::PlayConsole)) return;
  if (a.scripts.enabled) {  // changed scripts must be compiled in first; the restart that follows resumes the Play
    ScriptSync sync = scriptsSync(a);
    if (sync == ScriptSync::Building) {
      a.scripts.playAfter = true;
      EditorLog::instance().info("play: waiting for the script rebuild");
      return;
    }
    if (sync == ScriptSync::Failed) { EditorLog::instance().error("play: fix script compile errors first"); return; }
  }
  if (a.doc.dirty()) {  // the cook reads scene files from disk, so unsaved edits must land first
    EditorLog::instance().info("play: saving modified scene before the build");
    if (!a.saveScene()) return;
  }
  if (!a.play.play(a.project.root, a.target)) EditorLog::instance().warn("play: already running");
}

// The status word next to the target combo while a console run exists: status + whether it is paused, hung or crashed.
std::string consoleStatusText(App& a) {
  std::string s = playStatusName(a.play.status());
  if (a.play.status() == PlayStatus::Running) {
    if (a.play.exitState() == pipeline::ExitState::Hung) s = "Running (hung)";
    else if (a.play.exitState() == pipeline::ExitState::Crashed) s = "Running (crashed)";
    else if (a.play.paused()) s = "Running (paused)";
  } else if (a.play.status() == PlayStatus::Stopped && a.play.finished() && !a.play.lastOk()) {
    s = "Stopped (failed)";
  }
  return s;
}

static bool primaryButton(const char* label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, brandRamp("pequi.600"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
  ImVec2 p = ImGui::GetCursorScreenPos();
  bool pressed = ImGui::Button(label, size);
  // play triangle, drawn so the label font needs no special glyph
  float cy = p.y + size.y * 0.5f, x = p.x + 14;
  ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(x, cy - 6), ImVec2(x, cy + 6), ImVec2(x + 10, cy),
                                                ImGui::GetColorU32(ImGuiCol_Text));
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  return pressed;
}

void drawToolbar(App& a) {
  const float h = 44;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
  bool open = ImGui::BeginViewportSideBar("##Toolbar", vp, ImGuiDir_Up, h, fl);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if (open) {
    const float W = ImGui::GetWindowWidth(), bh = 30, top = (h - bh) * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // left: pequi icon at 24 px (brand: editor toolbar size) + wordmark + project name
    ImVec2 wp = ImGui::GetWindowPos();
    drawPequi(dl, ImVec2(wp.x + 12, wp.y + (h - 28) * 0.5f), 28);
    ImGui::SetCursorPos(ImVec2(12 + 28 + 8, 0));
    ImGui::PushFont(a.fonts.uiBold, a.fonts.wordmarkSize);
    float th = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY((h - th) * 0.5f);
    ImGui::TextUnformatted("Rensga Engine");
    ImGui::PopFont();
    if (a.loaded) {
      ImGui::SameLine(0, 10);
      ImGui::SetCursorPosY((h - ImGui::GetTextLineHeight()) * 0.5f);
      ImGui::TextDisabled("/  %s", a.project.name.c_str());
    }

    // center: Play, Pause, Step, Stop, play-mode menu
    const float pw = 84, sw = 64, qw = 34, aw = 30, gap = 6, total = pw + qw + qw + sw + aw + gap * 4;
    ImGui::SetCursorPos(ImVec2((W - total) * 0.5f, top));
    const bool busy = a.play.busy();
    const bool editing = a.session.active();
    const bool blocked = scriptsBlockPlay(a);
    const bool noModule = a.target.empty() && !a.playInEditor;  // F3.02: console run needs the project's TargetModule
    ImGui::BeginDisabled(editing || (!a.playInEditor && busy) || blocked || noModule || (a.playInEditor && a.play.busy()));
    if (primaryButton("      Play", ImVec2(pw, bh))) doPlay(a);
    ImGui::EndDisabled();
    if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Fix script compile errors first");
    else if (noModule && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("The '%s' target has no editor module yet, so the editor cannot build or run it on a console. Play in editor works.", a.project.target.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
    ImGui::SameLine(0, gap);
    // Pause / Step drive Play in editor, or the game running in the emulator (through the dev control channel, F5.20).
    const bool consoleRun = !editing && a.play.status() == PlayStatus::Running;
    ImGui::BeginDisabled(!editing && !consoleRun);
    const bool paused = editing ? a.session.state() == PlayState::Paused : (consoleRun && a.play.paused());
    if (paused) ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary")), ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
    if (ImGui::Button("||##pause", ImVec2(qw, bh))) {
      if (editing) a.session.togglePause();
      else if (paused) a.play.resume();
      else a.play.pause();
    }
    if (paused) ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(consoleRun ? "Pause the game in the emulator" : "Pause (Ctrl+Shift+P)");
    ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("|>##step", ImVec2(qw, bh))) {
      if (editing) a.session.requestStep();
      else a.play.step(1);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(consoleRun ? "Run exactly one frame in the emulator while paused" : "Step one frame while paused (Ctrl+Alt+P)");
    ImGui::EndDisabled();
    ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!busy && !editing);
    if (ImGui::Button("Stop", ImVec2(sw, bh))) { if (editing) stopPlayInEditor(a); else a.play.stop(); }
    ImGui::EndDisabled();
    ImGui::SameLine(0, gap);
    if (ImGui::ArrowButton("##playmenu", ImGuiDir_Down)) ImGui::OpenPopup("playmode");
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("playmode")) {
      if (ImGui::MenuItem("Play in editor", nullptr, a.playInEditor, !editing)) a.playInEditor = true;
      const bool consoleOk = !a.target.empty();
      if (ImGui::MenuItem("Play on console", nullptr, !a.playInEditor, consoleOk && !editing)) a.playInEditor = false;
      if (!consoleOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The '%s' target has no editor module, so there is no emulator run.", a.project.target.c_str());
      ImGui::EndPopup();
    }

    // right: status then target combo
    char st[96];
    const bool compiling = a.scripts.builder.busy();
    if (compiling) snprintf(st, sizeof st, "Compiling scripts %d%%", a.scripts.builder.percent());
    else if (editing) snprintf(st, sizeof st, "%s   frame %llu", paused ? "Paused" : "Playing", static_cast<unsigned long long>(a.session.frames()));
    else snprintf(st, sizeof st, "%s%s", consoleStatusText(a).c_str(), a.doc.dirty() ? "   * unsaved" : "");
    const float cw = 190, stw = ImGui::CalcTextSize(st).x;
    ImGui::SetCursorPos(ImVec2(W - 12 - stw - 14 - cw, top));
    ImGui::SetNextItemWidth(cw);
    const TargetModule* tm = a.targetModule();
    if (ImGui::BeginCombo("##target", tm ? std::string(tm->displayName()).c_str() : (a.project.target + " (no editor module)").c_str())) {
      for (TargetModule* m : TargetRegistry::all()) {
        std::string id(m->id());
        if (ImGui::Selectable((std::string(m->displayName()) + "##" + id).c_str(), id == a.target)) a.target = id;
      }
      ImGui::EndCombo();
    }
    // drawn directly: the combo leaves a frame-padding baseline offset that would push a Text item down
    ImVec2 sp = ImGui::GetWindowPos();
    dl->AddText(ImVec2(sp.x + W - 12 - stw, sp.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(compiling ? brandToken("primary") : brandToken("text-2")), st);
    if (compiling) {  // thin progress bar under the status text
      const float x0 = sp.x + W - 12 - stw, y0 = sp.y + (h + ImGui::GetTextLineHeight()) * 0.5f + 3;
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + stw, y0 + 3), ImGui::GetColorU32(brandToken("border")), 2);
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + stw * a.scripts.builder.percent() / 100.0f, y0 + 3),
                        ImGui::GetColorU32(brandToken("primary")), 2);
    }
  }
  ImGui::End();
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(toolbar, "Toolbar", drawToolbar, nullptr, nullptr, 20, 0, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
