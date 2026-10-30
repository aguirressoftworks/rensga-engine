#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

void drawConsole(App& a) {
  if (!a.showConsole) return;
  if (!ImGui::Begin("Console", nullptr)) { ImGui::End(); return; }
  if (ImGui::Button("Clear")) {}  // log is append-only in the MVP
  ImGui::SameLine(); ImGui::Checkbox("Info", &a.showInfo);
  ImGui::SameLine(); ImGui::Checkbox("Warn", &a.showWarn);
  ImGui::SameLine(); ImGui::Checkbox("Error", &a.showError);
  ImGui::SameLine(); ImGui::Checkbox("Editor", &a.showEditor);
  ImGui::SameLine(); ImGui::Checkbox("Game", &a.showGame);
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
      ImGui::Text("%s %s", l.source == LogSource::Game ? "[game]" : "[editor]", l.text.c_str());
    }
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  if (a.scrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
