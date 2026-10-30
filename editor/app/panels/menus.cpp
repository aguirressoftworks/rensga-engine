#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include "app.h"
#include "panels/asset_ui.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

bool App::saveScene() {
  if (!doc.loaded()) return false;
  std::string err;
  if (!doc.save(err)) { EditorLog::instance().error("save failed: " + err); return false; }
  EditorLog::instance().info("scene saved: " + doc.path().filename().string());
  return true;
}

static void handleShortcuts(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return;
  auto chord = [](ImGuiKeyChord c) { return ImGui::IsKeyChordPressed(c, ImGuiInputFlags_RouteGlobal); };
  if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) a.saveScene();
  if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || chord(ImGuiMod_Ctrl | ImGuiKey_Y)) a.doc.redo();
  else if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) a.doc.undo();
  if (chord(ImGuiMod_Ctrl | ImGuiKey_D)) duplicateSelected(a);
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) deleteSelected(a);
  if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !a.doc.selected().empty()) beginRename(a, a.doc.selected());
}

void drawMenuBar(App& a) {
  handleShortcuts(a);
  if (!ImGui::BeginMainMenuBar()) return;
  const bool hasSel = !a.doc.selected().empty();
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Project...")) a.hub.openCreate = true;
    if (ImGui::MenuItem("Open Project...")) a.hub.goPicker = true;
    if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, a.doc.loaded())) a.saveScene();
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) a.quitRequested = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, a.doc.canUndo())) a.doc.undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, a.doc.canRedo())) a.doc.redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel)) duplicateSelected(a);
    if (ImGui::MenuItem("Delete", "Del", false, hasSel)) deleteSelected(a);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("GameObject")) {
    drawCreateMenuItems(a, "");
    if (ImGui::MenuItem("Create Empty Child", nullptr, false, hasSel)) createEmptyObject(a, a.doc.selected());
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2", false, hasSel)) beginRename(a, a.doc.selected());
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel)) duplicateSelected(a);
    if (ImGui::MenuItem("Delete", "Del", false, hasSel)) deleteSelected(a);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    if (ImGui::BeginMenu("Layouts")) {
      if (ImGui::MenuItem("Default")) a.resetLayout = true;
      std::error_code ec;
      std::vector<std::string> names;
      for (fs::directory_iterator it(a.project.root / "Library" / "Editor" / "layouts", ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".ini") names.push_back(it->path().stem().string());
      std::sort(names.begin(), names.end());
      for (const std::string& n : names)
        if (ImGui::MenuItem(n.c_str())) a.pendingLayout = (a.project.root / "Library" / "Editor" / "layouts" / (n + ".ini")).string();
      ImGui::Separator();
      if (ImGui::MenuItem("Save Layout As...")) a.openSaveLayout = true;
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset Layout")) a.resetLayout = true;
    ImGui::Separator();
    ImGui::MenuItem("VRAM Map", nullptr, &assetUi().showVram, a.loaded);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    if (ImGui::MenuItem("About Rensga Engine")) a.openAbout = true;
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void drawDialogs(App& a) {
  drawScriptDialogs(a);
  if (a.openAbout) { ImGui::OpenPopup("About Rensga Engine"); a.openAbout = false; }
  ImVec2 c = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("About Rensga Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    drawPequi(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 64);
    ImGui::Dummy(ImVec2(64, 64));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(a.fonts.uiBold, 20);
    ImGui::TextUnformatted("Rensga Engine");
    ImGui::PopFont();
    ImGui::TextDisabled("Open-source engine and editor for retro consoles.");
    ImGui::TextDisabled("Its icon is a pequi fruit from the cerrado of Goias.");
    ImGui::EndGroup();
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  drawSaveLayoutDialog(a);
}

}  // namespace editor
}  // namespace rensga
