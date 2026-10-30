// Bottom status bar: script build progress with Cancel, the last build result, error and warning counts.
// Status colors always come with a word (brand rule): "Compiling scripts...", "Errors", "Warnings", "OK".
#include <imgui_internal.h>

#include <cstdio>

#include "app.h"

namespace rensga {
namespace editor {

void drawStatusBar(App& a) {
  if (!a.loaded) return;
  ScriptState& s = a.scripts;
  const float h = 26;
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 3));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  const ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
  const bool open = ImGui::BeginViewportSideBar("##StatusBar", vp, ImGuiDir_Down, h, fl);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if (open) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float W = ImGui::GetWindowWidth();
    const float textY = (h - ImGui::GetTextLineHeight()) * 0.5f;
    if (s.builder.busy()) {
      ImGui::SetCursorPos(ImVec2(12, textY));
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("primary"));
      ImGui::TextUnformatted("Compiling scripts...");
      ImGui::PopStyleColor();
      const float x0 = wp.x + 12 + ImGui::CalcTextSize("Compiling scripts...").x + 14, bw = 200, y0 = wp.y + h * 0.5f - 3;
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw, y0 + 6), ImGui::GetColorU32(brandToken("border")), 3);
      dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + bw * s.builder.progress(), y0 + 6), ImGui::GetColorU32(brandToken("primary")), 3);
      char pct[16];
      std::snprintf(pct, sizeof pct, "%d%%", s.builder.percent());
      dl->AddText(ImVec2(x0 + bw + 10, wp.y + textY), ImGui::GetColorU32(brandToken("text-2")), pct);
      ImGui::SetCursorPos(ImVec2(x0 - wp.x + bw + 10 + ImGui::CalcTextSize("100%").x + 14, 2));
      if (ImGui::SmallButton("Cancel")) scriptsCancel(a);
    } else {
      const char* word = s.statusKind == 1 ? "OK" : s.statusKind == 2 ? "Error" : "";
      ImGui::SetCursorPos(ImVec2(12, textY));
      if (!s.statusText.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, brandToken(s.statusKind == 1 ? "ok" : s.statusKind == 2 ? "danger" : "text-2"));
        if (*word) ImGui::Text("%s:", word);
        if (*word) ImGui::SameLine();
        ImGui::TextUnformatted(s.statusText.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::TextDisabled("Ready");
      }
    }
    // right side: the last build's counts
    if (s.lastErrors > 0 || s.lastWarnings > 0) {
      char buf[96];
      std::snprintf(buf, sizeof buf, "Errors %d   Warnings %d", s.lastErrors, s.lastWarnings);
      const float tw = ImGui::CalcTextSize(buf).x;
      dl->AddText(ImVec2(wp.x + W - 12 - tw, wp.y + textY),
                  ImGui::GetColorU32(brandToken(s.lastErrors > 0 ? "danger" : "warn")), buf);
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
