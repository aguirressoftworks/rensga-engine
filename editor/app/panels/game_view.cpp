// Game window (MVP-50): the scene seen through its Camera component at the active target's preview resolution.
// Resolution comes from TargetModule::previewRules(); nothing here names a console.
#include <algorithm>

#include "app.h"

namespace rensga {
namespace editor {

void drawGameView(App& a) {
  if (!a.showGame2) return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }
  const ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
  if (sz.x < 8 || sz.y < 8) { ImGui::Dummy(sz); ImGui::End(); return; }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("bg")));

  const TargetModule* tm = a.targetModule();
  PreviewRules pr = tm ? tm->previewRules() : PreviewRules();
  const int rw = pr.width > 0 ? pr.width : 640, rh = pr.height > 0 ? pr.height : 360;
  SceneDraw sd = gatherScene(a.doc, a.meshes);
  const float labelH = ImGui::GetTextLineHeight() + 16;
  const float aspect = static_cast<float>(rw) / static_cast<float>(rh);
  float fw = sz.x - 24, fh = fw / aspect;
  if (fh > sz.y - 24 - labelH) { fh = sz.y - 24 - labelH; fw = fh * aspect; }
  fw = std::max(fw, 8.0f); fh = std::max(fh, 8.0f);
  const ImVec2 f0(p0.x + (sz.x - fw) * 0.5f, p0.y + 12 + (sz.y - 24 - labelH - fh) * 0.5f), f1(f0.x + fw, f0.y + fh);

  if (sd.camera.found && a.renderer.ready()) {
    a.gameRT.ensure(rw, rh, 0, true);
    RenderOptions ro;
    const CameraInfo& c = sd.camera;
    ro.view = viewLH(c.eye, c.right, c.up, c.fwd);
    ro.proj = perspectiveLH(c.fovDeg, aspect, c.nearClip, c.farClip);
    ro.farClip = c.farClip;
    ImVec4 bg = brandToken("surface-2");
    ro.clear[0] = bg.x; ro.clear[1] = bg.y; ro.clear[2] = bg.z; ro.clear[3] = 1;
    a.renderer.render(a.gameRT, sd, ro);
    dl->AddImage(static_cast<ImTextureID>(a.gameRT.tex), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
  } else {
    dl->AddRectFilled(f0, f1, ImGui::GetColorU32(brandToken("surface-2")));
    const char* msg = "No Camera in the scene";
    ImVec2 ts = ImGui::CalcTextSize(msg);
    dl->AddText(ImVec2(f0.x + (fw - ts.x) * 0.5f, f0.y + (fh - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), msg);
  }
  dl->AddRect(f0, f1, ImGui::GetColorU32(brandToken("border-strong")));

  char info[160];
  std::snprintf(info, sizeof info, "%d x %d  %s", rw, rh, tm ? std::string(tm->displayName()).c_str() : "(no target)");
  dl->AddText(ImVec2(f0.x, f1.y + 6), ImGui::GetColorU32(brandToken("text-2")), info);
  const char* note = "Camera preview \xE2\x80\x94 Play in editor arrives in Editor 5";
  ImVec2 ns = ImGui::CalcTextSize(note);
  dl->AddText(ImVec2(f1.x - ns.x, f1.y + 6), ImGui::GetColorU32(brandToken("muted")), note);
  ImGui::Dummy(sz);
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
