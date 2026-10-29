#include <imgui_internal.h>

#include <cstring>

#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

bool App::saveScene() {
  if (!doc.loaded()) return false;
  std::string err;
  if (!doc.save(err)) { EditorLog::instance().error("save failed: " + err); return false; }
  EditorLog::instance().info("scene saved: " + doc.path().filename().string());
  return true;
}

static void doDuplicate(App& a) { if (!a.doc.selected().empty()) a.doc.duplicate(a.doc.selected()); }
static void doDelete(App& a) { if (!a.doc.selected().empty()) a.doc.remove(a.doc.selected()); }

static void doCreateEmpty(App& a, bool child) {
  if (!a.doc.loaded()) return;
  std::string parent = child ? a.doc.selected() : std::string();
  std::string g = a.doc.createObject("GameObject", parent);
  if (!g.empty()) a.doc.select(g);
}

static void handleShortcuts(App& a) {
  ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return;
  auto chord = [](ImGuiKeyChord c) { return ImGui::IsKeyChordPressed(c, ImGuiInputFlags_RouteGlobal); };
  if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) a.saveScene();
  if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || chord(ImGuiMod_Ctrl | ImGuiKey_Y)) a.doc.redo();
  else if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) a.doc.undo();
  if (chord(ImGuiMod_Ctrl | ImGuiKey_D)) doDuplicate(a);
  if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) doDelete(a);
}

void drawMenuBar(App& a) {
  handleShortcuts(a);
  if (!ImGui::BeginMainMenuBar()) return;
  const bool hasSel = !a.doc.selected().empty();
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Open Project...")) { a.openProjectDialog = true; }
    if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, a.doc.loaded())) a.saveScene();
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) a.quitRequested = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, a.doc.canUndo())) a.doc.undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z", false, a.doc.canRedo())) a.doc.redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSel)) doDuplicate(a);
    if (ImGui::MenuItem("Delete", "Del", false, hasSel)) doDelete(a);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("GameObject")) {
    if (ImGui::MenuItem("Create Empty", nullptr, false, a.doc.loaded())) doCreateEmpty(a, false);
    if (ImGui::MenuItem("Create Empty Child", nullptr, false, hasSel)) doCreateEmpty(a, true);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Window")) {
    if (ImGui::MenuItem("Reset Layout")) a.resetLayout = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Help")) {
    if (ImGui::MenuItem("About Rensga Engine")) a.openAbout = true;
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void drawDialogs(App& a) {
  if (a.openAbout) { ImGui::OpenPopup("About Rensga Engine"); a.openAbout = false; }
  if (a.openProjectDialog) { ImGui::OpenPopup("Open Project"); a.openProjectDialog = false; }
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
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Project folder (contains ProjectSettings/ and Assets/)");
    ImGui::SetNextItemWidth(520);
    bool enter = ImGui::InputText("##path", a.openPath, sizeof a.openPath, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    if (enter || ImGui::Button("Open", ImVec2(96, 0))) {
      if (a.openPath[0]) a.pendingProject = a.openPath;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

}  // namespace editor
}  // namespace rensga
