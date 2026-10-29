// Fixed strip under the menu bar: pequi + project name (left), Play/Stop (center), target + status (right).
#include <imgui_internal.h>

#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

const TargetModule* App::targetModule() const { return TargetRegistry::find(target); }

// The single entry point for Play: the toolbar button and --play both call it.
void doPlay(App& a) {
  if (!a.loaded) { EditorLog::instance().error("play: no project loaded"); return; }
  if (a.target.empty()) { EditorLog::instance().error("play: no target registered"); return; }
  if (a.doc.dirty()) {  // the cook reads scene files from disk, so unsaved edits must land first
    EditorLog::instance().info("play: saving modified scene before the build");
    if (!a.saveScene()) return;
  }
  if (!a.play.play(a.project.root, a.target)) EditorLog::instance().warn("play: already running");
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

    // center: Play, Stop, play-mode menu
    const float pw = 84, sw = 64, aw = 30, gap = 6, total = pw + sw + aw + gap * 2;
    ImGui::SetCursorPos(ImVec2((W - total) * 0.5f, top));
    bool busy = a.play.busy();
    ImGui::BeginDisabled(busy);
    if (primaryButton("      Play", ImVec2(pw, bh))) doPlay(a);
    ImGui::EndDisabled();
    ImGui::SameLine(0, gap);
    ImGui::BeginDisabled(!busy);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
    if (ImGui::Button("Stop", ImVec2(sw, bh))) a.play.stop();
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ImGui::SameLine(0, gap);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8);
    if (ImGui::ArrowButton("##playmenu", ImGuiDir_Down)) ImGui::OpenPopup("playmode");
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("playmode")) {
      ImGui::MenuItem("Play on console", nullptr, true);
      ImGui::BeginDisabled();
      ImGui::MenuItem("Play in editor");
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("arrives in Editor 5");
      ImGui::EndPopup();
    }

    // right: status then target combo
    char st[64];
    snprintf(st, sizeof st, "%s%s", playStatusName(a.play.status()), a.doc.dirty() ? "   * unsaved" : "");
    const float cw = 190, stw = ImGui::CalcTextSize(st).x;
    ImGui::SetCursorPos(ImVec2(W - 12 - stw - 14 - cw, top));
    ImGui::SetNextItemWidth(cw);
    const TargetModule* tm = a.targetModule();
    if (ImGui::BeginCombo("##target", tm ? std::string(tm->displayName()).c_str() : "(no target)")) {
      for (TargetModule* m : TargetRegistry::all()) {
        std::string id(m->id());
        if (ImGui::Selectable((std::string(m->displayName()) + "##" + id).c_str(), id == a.target)) a.target = id;
      }
      ImGui::EndCombo();
    }
    // drawn directly: the combo leaves a frame-padding baseline offset that would push a Text item down
    ImVec2 sp = ImGui::GetWindowPos();
    dl->AddText(ImVec2(sp.x + W - 12 - stw, sp.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(brandToken("text-2")), st);
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
