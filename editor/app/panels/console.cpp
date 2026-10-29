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
  ImGui::Separator();
  ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus);
  ImGui::PushFont(a.fonts.mono, a.fonts.monoSize);
  for (const LogLine& l : EditorLog::instance().snapshot()) {
    if (l.level == LogLevel::Info && !a.showInfo) continue;
    if (l.level == LogLevel::Warn && !a.showWarn) continue;
    if (l.level == LogLevel::Error && !a.showError) continue;
    if (l.source == LogSource::Editor && !a.showEditor) continue;
    if (l.source == LogSource::Game && !a.showGame) continue;
    ImVec4 col = l.level == LogLevel::Error ? brandToken("danger")
               : l.level == LogLevel::Warn ? brandToken("warn") : brandToken("text-2");
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::Text("%s %s", l.source == LogSource::Game ? "[game]" : "[editor]", l.text.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  if (a.scrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
