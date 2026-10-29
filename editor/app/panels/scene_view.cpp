// Scene window. Stage 1 placeholder: framed viewport with a grid; the real renderer and gizmos come next.
#include <cmath>

#include "app.h"

namespace rensga {
namespace editor {

void drawSceneView(App& a) {
  if (!a.showScene) return;
  if (a.focusScene) { ImGui::SetNextWindowFocus(); a.focusScene = false; }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = ImGui::Begin("Scene", nullptr);
  ImGui::PopStyleVar();
  if (open) {
    ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
    if (sz.x > 4 && sz.y > 4) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
      dl->AddRectFilled(p0, p1, ImGui::GetColorU32(brandToken("surface-2")));
      dl->PushClipRect(p0, p1, true);
      const float step = 40;
      ImU32 grid = ImGui::GetColorU32(brandToken("border"));
      for (float x = std::fmod(sz.x * 0.5f, step); x < sz.x; x += step) dl->AddLine(ImVec2(p0.x + x, p0.y), ImVec2(p0.x + x, p1.y), grid);
      for (float y = std::fmod(sz.y * 0.5f, step); y < sz.y; y += step) dl->AddLine(ImVec2(p0.x, p0.y + y), ImVec2(p1.x, p0.y + y), grid);
      ImU32 axis = ImGui::GetColorU32(brandToken("border-strong"));
      dl->AddLine(ImVec2(p0.x + sz.x * 0.5f, p0.y), ImVec2(p0.x + sz.x * 0.5f, p1.y), axis, 2);
      dl->AddLine(ImVec2(p0.x, p0.y + sz.y * 0.5f), ImVec2(p1.x, p0.y + sz.y * 0.5f), axis, 2);
      dl->PopClipRect();
      char msg[96];
      std::snprintf(msg, sizeof msg, "%d object(s) in %s", static_cast<int>(a.doc.scene().objects.size()),
                    a.doc.loaded() ? a.doc.path().filename().string().c_str() : "(no scene)");
      ImVec2 ts = ImGui::CalcTextSize(msg);
      dl->AddText(ImVec2(p0.x + (sz.x - ts.x) * 0.5f, p0.y + sz.y * 0.5f - ts.y - 8), ImGui::GetColorU32(brandToken("text-2")), msg);
      const char* hint = "Scene editing arrives in the next step";
      ImVec2 hs = ImGui::CalcTextSize(hint);
      dl->AddText(ImVec2(p0.x + (sz.x - hs.x) * 0.5f, p0.y + sz.y * 0.5f + 8), ImGui::GetColorU32(brandToken("muted")), hint);
      ImGui::Dummy(sz);
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
