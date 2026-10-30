#include <cfloat>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "panels/asset_ui.h"

namespace rensga {
namespace editor {

static void drawObject(App& a, const std::string& guid) {
  const SceneObject* o = a.doc.find(guid);
  if (!o) return;
  std::vector<std::string> kids = a.doc.childrenOf(guid);
  ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
  if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
  if (a.doc.selected() == guid) fl |= ImGuiTreeNodeFlags_Selected;
  ImGui::PushID(guid.c_str());
  const bool renaming = a.renameGuid == guid;
  if (!o->active) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  bool open = ImGui::TreeNodeEx(renaming ? "##node" : (o->name.empty() ? "(unnamed)" : o->name.c_str()), fl);
  if (!o->active) ImGui::PopStyleColor();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) a.doc.select(guid);
  if (!renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) beginRename(a, guid);

  if (renaming) {
    static char buf[128];
    ImGui::SameLine();
    if (a.renameFocus) {
      std::snprintf(buf, sizeof buf, "%s", o->name.c_str());
      ImGui::SetKeyboardFocusHere();
      a.renameFocus = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool enter = ImGui::InputText("##rename", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (enter || (ImGui::IsItemDeactivated() && !escape)) { if (buf[0]) a.doc.rename(guid, buf); a.renameGuid.clear(); }
    else if (escape) a.renameGuid.clear();
  }

  if (!renaming && ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("RENSGA_OBJECT", guid.c_str(), guid.size() + 1);
    ImGui::TextUnformatted(o->name.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_OBJECT"))
      a.doc.reparent(static_cast<const char*>(p->Data), guid);
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_SCRIPT"))
      addScriptToObject(a, guid, static_cast<const char*>(p->Data));
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload))
      acceptAssetDrop(a, static_cast<const char*>(p->Data), guid);
    ImGui::EndDragDropTarget();
  }
  std::string toDelete, toDup;
  if (ImGui::BeginPopupContextItem("ctx")) {
    drawCreateMenuItems(a, guid);
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) beginRename(a, guid);
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) toDup = guid;
    if (ImGui::MenuItem("Delete", "Del")) toDelete = guid;
    ImGui::EndPopup();
  }
  if (open) {
    for (const std::string& k : kids) drawObject(a, k);
    ImGui::TreePop();
  }
  ImGui::PopID();
  if (!toDup.empty()) { a.doc.select(toDup); duplicateSelected(a); }
  if (!toDelete.empty()) a.doc.remove(toDelete);
}

void drawHierarchy(App& a) {
  if (!a.showHierarchy) return;
  if (!ImGui::Begin("Hierarchy", nullptr)) { ImGui::End(); return; }
  if (!a.doc.loaded()) {
    ImGui::TextDisabled("No scene loaded");
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("%s%s", a.doc.path().filename().string().c_str(), a.doc.dirty() ? " *" : "");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 4);
  if (ImGui::SmallButton("+")) ImGui::OpenPopup("hier_add");
  if (ImGui::BeginPopup("hier_add")) {
    drawCreateMenuItems(a, "");
    ImGui::EndPopup();
  }
  ImGui::Separator();
  for (const std::string& r : a.doc.childrenOf("")) drawObject(a, r);
  // drop on the empty area: unparent
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.y > 8) {
    ImGui::InvisibleButton("##hier_empty", ImVec2(avail.x, avail.y));
    if (ImGui::IsItemClicked()) a.doc.select("");
    if (ImGui::BeginPopupContextItem("##hier_empty_ctx")) {
      drawCreateMenuItems(a, "");
      ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_OBJECT"))
        a.doc.reparent(static_cast<const char*>(p->Data), "");
      ImGui::EndDragDropTarget();
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
