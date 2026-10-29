// Unity-style default layout: Hierarchy left, Inspector right, Project + Console bottom, Scene + Game center.
#include <imgui_internal.h>

#include <fstream>

#include "app.h"

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
  ImGui::DockBuilderDockWindow("Scene", center);
  ImGui::DockBuilderDockWindow("Game", center);
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

}  // namespace editor
}  // namespace rensga
