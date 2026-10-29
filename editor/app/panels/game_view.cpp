// Game window. Stage 1 placeholder: a frame at the target's native preview resolution, letterboxed.
#include "app.h"

namespace rensga {
namespace editor {

void drawGameView(App& a) {
  if (!a.showGame2) return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = ImGui::Begin("Game", nullptr);
  ImGui::PopStyleVar();
  if (open) {
    ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
    const TargetModule* tm = a.targetModule();
    PreviewRules pr = tm ? tm->previewRules() : PreviewRules();
    if (sz.x > 4 && sz.y > 4) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("surface-2")));
      float aspect = pr.width > 0 && pr.height > 0 ? float(pr.width) / float(pr.height) : 16.0f / 9.0f;
      float fw = sz.x - 32, fh = fw / aspect;
      if (fh > sz.y - 32) { fh = sz.y - 32; fw = fh * aspect; }
      ImVec2 f0(p0.x + (sz.x - fw) * 0.5f, p0.y + (sz.y - fh) * 0.5f), f1(f0.x + fw, f0.y + fh);
      dl->AddRectFilled(f0, f1, ImGui::GetColorU32(brandToken("bg")));
      dl->AddRect(f0, f1, ImGui::GetColorU32(brandToken("border-strong")));
      char msg[128];
      if (pr.width > 0) std::snprintf(msg, sizeof msg, "%d x %d  %s", pr.width, pr.height, tm ? std::string(tm->displayName()).c_str() : "");
      else std::snprintf(msg, sizeof msg, "No target");
      ImVec2 ts = ImGui::CalcTextSize(msg);
      dl->AddText(ImVec2(f0.x + (fw - ts.x) * 0.5f, f0.y + (fh - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), msg);
      ImGui::Dummy(sz);
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
