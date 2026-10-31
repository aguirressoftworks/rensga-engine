// Unity-style default layout: Hierarchy left, Inspector right, Project + Console bottom, Scene + Game center.
#include <imgui_internal.h>

#include <fstream>

#include "app.h"
#include "panel_registry.h"

namespace rensga {
namespace editor {

ImGuiID dockSpaceId() { return ImHashStr("RensgaDockSpace"); }

static void buildDefaultLayout(ImGuiID root, ImVec2 size) {
  ImGui::DockBuilderRemoveNode(root);
  ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(root, size);
  ImGuiID center = root, left = 0, right = 0, bottom = 0;
  left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
  right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f / (1.0f - 0.18f), nullptr, &center);
  bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
  ImGui::DockBuilderDockWindow("Hierarchy", left);
  ImGui::DockBuilderDockWindow("Inspector", right);
  ImGui::DockBuilderDockWindow("Project", bottom);
  ImGui::DockBuilderDockWindow("Console", bottom);
  ImGui::DockBuilderDockWindow("Profiler", bottom);  // F5.10
  ImGui::DockBuilderDockWindow("Scene", center);
  ImGui::DockBuilderDockWindow("Game", center);
  ImGui::DockBuilderDockWindow("Emulator", center);  // F5.23
  ImGui::DockBuilderFinish(root);
}

void drawDockSpaceAndLayout(App& a) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGuiID id = dockSpaceId();
  bool needBuild = a.resetLayout || ImGui::DockBuilderGetNode(id) == nullptr;
  ImGui::DockSpaceOverViewport(id, vp);
  if (needBuild) {
    buildDefaultLayout(id, vp->WorkSize);
    a.resetLayout = false;
    a.focusScene = true;
    a.showHierarchy = a.showInspector = a.showConsole = a.showProject = a.showScene = a.showGame2 = true;
  }
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(dock_layout, "Dock space", drawDockSpaceAndLayout, nullptr, nullptr, 40, 0, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
