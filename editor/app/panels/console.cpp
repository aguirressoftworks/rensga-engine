#include <algorithm>
#include <cctype>
#include <cstring>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

static bool containsNoCase(const std::string& hay, const char* needle) {
  if (!needle || !*needle) return true;
  const std::string n(needle);
  return std::search(hay.begin(), hay.end(), n.begin(), n.end(), [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); }) != hay.end();
}

// F5.22: one line above the log when the console run crashed, hung or ended badly: the word, the reason, and what to do next.
static void drawSessionBanner(App& a) {
  const bool hung = a.play.status() == PlayStatus::Running && a.play.exitState() == pipeline::ExitState::Hung;
  const bool crashedLive = a.play.status() == PlayStatus::Running && a.play.exitState() == pipeline::ExitState::Crashed;
  const bool failed = a.play.status() == PlayStatus::Stopped && a.play.finished() && !a.play.lastOk();
  if (!hung && !crashedLive && !failed) return;
  std::string detail = a.play.exitDetail();
  const char* word = hung ? "HUNG" : "CRASHED";
  if (failed && detail.empty()) { word = "FAILED"; detail = "the Play pipeline stopped with an error (see the log)"; }
  for (const char* pre : {"crashed: ", "hung: "})  // the word is already in front
    if (detail.rfind(pre, 0) == 0) detail.erase(0, std::strlen(pre));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
  ImGui::Text("[!] %s: %s", word, detail.c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::SmallButton("Relaunch with debugger")) a.play.relaunchWithDebugger();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops the session and plays again with the emulator's debugger on (what that means is up to the target's emulator launcher)");
  if (!a.play.lastLogPath().empty()) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy log path")) ImGui::SetClipboardText(a.play.lastLogPath().string().c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.play.lastLogPath().string().c_str());
  }
}

void drawConsole(App& a) {
  if (!a.showConsole) return;
  if (!ImGui::Begin("Console", nullptr)) { ImGui::End(); return; }
  if (ImGui::Button("Clear")) EditorLog::instance().clear();
  ImGui::SameLine(); ImGui::Checkbox("Info", &a.showInfo);
  ImGui::SameLine(); ImGui::Checkbox("Warn", &a.showWarn);
  ImGui::SameLine(); ImGui::Checkbox("Error", &a.showError);
  ImGui::SameLine(); ImGui::Checkbox("Editor", &a.showEditor);
  ImGui::SameLine(); ImGui::Checkbox("Game", &a.showGame);
  ImGui::SameLine(); ImGui::Checkbox("Emulator", &a.showEmulator);
  ImGui::SameLine(); ImGui::SetNextItemWidth(150);
  ImGui::InputTextWithHint("##logsearch", "search", a.logSearch, sizeof a.logSearch);
  drawSessionBanner(a);
  {  // counts of the latest script build: word plus color, never color alone
    const int build = a.scripts.buildNumber;
    int errs = 0, warns = 0;
    if (build > 0)
      for (const LogLine& l : EditorLog::instance().snapshot())
        if (l.build == build && !l.file.empty()) { if (l.level == LogLevel::Error) ++errs; else if (l.level == LogLevel::Warn) ++warns; }
    if (errs || warns) {
      ImGui::SameLine(0, 24);
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken(errs ? "danger" : "warn"));
      ImGui::Text("Script build %d:  %d %s, %d %s", build, errs, errs == 1 ? "error" : "errors", warns, warns == 1 ? "warning" : "warnings");
      ImGui::PopStyleColor();
      ImGui::SameLine(); ImGui::TextDisabled("(double-click a line to open it)");
    }
  }
  ImGui::Separator();
  ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus);
  ImGui::PushFont(a.fonts.mono, a.fonts.monoSize);
  int idx = 0;
  for (const LogLine& l : EditorLog::instance().snapshot()) {
    ++idx;
    if (l.level == LogLevel::Info && !a.showInfo) continue;
    if (l.level == LogLevel::Warn && !a.showWarn) continue;
    if (l.level == LogLevel::Error && !a.showError) continue;
    if (l.source == LogSource::Editor && !a.showEditor) continue;
    if (l.source == LogSource::Game && !a.showGame) continue;
    if (l.source == LogSource::Emulator && !a.showEmulator) continue;
    if (a.logSearch[0] && !containsNoCase(l.text, a.logSearch)) continue;
    ImVec4 col = l.level == LogLevel::Error ? brandToken("danger")
               : l.level == LogLevel::Warn ? brandToken("warn") : brandToken("text-2");
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    if (!l.file.empty()) {  // a compiler diagnostic: double-click opens the file at the line
      ImGui::PushID(idx);
      const std::string label = std::string("[editor] ") + l.text;
      ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
      if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openLogLine(l);
        ImGui::SetTooltip("Double-click: open %s:%d in the code editor", l.file.c_str(), l.line);
      }
      ImGui::PopID();
    } else {
      ImGui::Text("%s %s", l.source == LogSource::Game ? "[game]" : l.source == LogSource::Emulator ? "[emulator]" : "[editor]", l.text.c_str());
    }
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  if (a.scrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(console, "Console", drawConsole, nullptr, nullptr, 120, 0, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
