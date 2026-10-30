// Hierarchy window (MVP-52, F3.01): multi-select (ctrl toggles, shift selects a range), drag and drop of the whole
// selection to reparent (world transforms kept) or reorder siblings (file order is sibling order), rename, duplicate,
// delete, copy/paste, search. Every action is one SceneDocument call, so one undo step.
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "panels/asset_ui.h"

namespace rensga {
namespace editor {

namespace {
std::string g_anchor;      // shift-click range start
std::string g_collapseOn;  // plain press on an already selected row: becomes the only selection on release without a drag
char g_search[96] = {};

bool matches(const std::string& name) {
  if (!g_search[0]) return true;
  std::string n = name, q = g_search;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
  std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return std::tolower(c); });
  return n.find(q) != std::string::npos;
}

// What a drag of `dragged` moves: the whole selection when the dragged row is part of it, else just that row.
std::vector<std::string> dragSet(App& a, const std::string& dragged) {
  if (a.doc.isSelected(dragged)) return a.doc.selection();
  return {dragged};
}

enum class Zone { Before, Into, After };

// Applies a drop of `moving` on `target` in the given zone.
void drop(App& a, const std::vector<std::string>& moving, const std::string& target, Zone z) {
  const SceneObject* t = a.doc.find(target);
  if (!t) return;
  std::string parent = t->parentGuid;
  if (!parent.empty() && !a.doc.find(parent)) parent.clear();
  if (z == Zone::Into) { a.doc.reparentMany(moving, target, ""); return; }
  if (z == Zone::Before) { a.doc.reparentMany(moving, parent, target); return; }
  // After: before the next sibling that is not being moved ("" = last)
  std::vector<std::string> sibs = a.doc.childrenOf(parent);
  std::string next;
  bool seen = false;
  for (const std::string& s : sibs) {
    if (s == target) { seen = true; continue; }
    if (seen && std::find(moving.begin(), moving.end(), s) == moving.end()) { next = s; break; }
  }
  a.doc.reparentMany(moving, parent, next);
}

void select(App& a, const std::string& guid) {
  ImGuiIO& io = ImGui::GetIO();
  g_collapseOn.clear();
  if (io.KeyShift && !g_anchor.empty() && a.doc.find(g_anchor)) {
    a.doc.setSelection(a.doc.displayRange(g_anchor, guid));
    a.doc.addToSelection(guid);
  } else if (io.KeyCtrl) {
    a.doc.toggleSelect(guid);
    g_anchor = guid;
  } else if (a.doc.isSelected(guid) && a.doc.selection().size() > 1) {
    g_collapseOn = guid;  // keep the group so it can be dragged
  } else {
    a.doc.select(guid);
    g_anchor = guid;
  }
}

void drawObject(App& a, const std::string& guid, bool flat) {
  const SceneObject* o = a.doc.find(guid);
  if (!o) return;
  std::vector<std::string> kids = flat ? std::vector<std::string>() : a.doc.childrenOf(guid);
  ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen;
  if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
  if (a.doc.isSelected(guid)) fl |= ImGuiTreeNodeFlags_Selected;
  ImGui::PushID(guid.c_str());
  const bool renaming = a.renameGuid == guid;
  if (!o->active) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  bool open = ImGui::TreeNodeEx(renaming ? "##node" : (o->name.empty() ? "(unnamed)" : o->name.c_str()), fl);
  if (!o->active) ImGui::PopStyleColor();
  const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) select(a, guid);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !a.doc.isSelected(guid)) { a.doc.select(guid); g_anchor = guid; }
  if (!g_collapseOn.empty() && g_collapseOn == guid && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
      !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)) {
    a.doc.select(guid);
    g_anchor = guid;
    g_collapseOn.clear();
  }
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
    const size_t n = dragSet(a, guid).size();
    if (n > 1) ImGui::Text("%zu objects", n); else ImGui::TextUnformatted(o->name.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    // top quarter of the row: before it, bottom quarter: after it (sibling order), middle: make it the parent
    const float h = rmax.y - rmin.y, my = ImGui::GetIO().MousePos.y;
    const Zone z = my < rmin.y + h * 0.25f ? Zone::Before : my > rmax.y - h * 0.25f ? Zone::After : Zone::Into;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_OBJECT", ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
      const std::string dragged = static_cast<const char*>(p->Data);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImU32 col = ImGui::GetColorU32(brandToken("primary"));
      if (z == Zone::Into) dl->AddRect(rmin, rmax, col, 2.0f, ImDrawFlags_None, 2.0f);
      else { const float y = z == Zone::Before ? rmin.y : rmax.y; dl->AddLine(ImVec2(rmin.x, y), ImVec2(rmax.x, y), col, 2); }
      if (p->IsDelivery()) drop(a, dragSet(a, dragged), guid, z);
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_SCRIPT"))
      addScriptToObject(a, guid, static_cast<const char*>(p->Data));
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload))
      acceptAssetDrop(a, static_cast<const char*>(p->Data), guid);
    ImGui::EndDragDropTarget();
  }
  bool dup = false, del = false, copy = false, paste = false;
  if (ImGui::BeginPopupContextItem("ctx")) {
    drawCreateMenuItems(a, guid);
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) beginRename(a, guid);
    if (ImGui::MenuItem("Copy", "Ctrl+C")) copy = true;
    if (ImGui::MenuItem("Paste", "Ctrl+V")) paste = true;
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) dup = true;
    if (ImGui::MenuItem("Delete", "Del")) del = true;
    ImGui::EndPopup();
  }
  if (open) {
    for (const std::string& k : kids) drawObject(a, k, false);
    ImGui::TreePop();
  }
  ImGui::PopID();
  if (copy) copySelected(a);
  if (paste) pasteSelected(a);
  if (dup) duplicateSelected(a);
  if (del) deleteSelected(a);
}
}  // namespace

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
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##hier_search", "Search", g_search, sizeof g_search);
  ImGui::Separator();

  // shortcuts while this window has the focus (Ctrl+D, Del and F2 are global, see menus.cpp)
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && a.renameGuid.empty()) {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) copySelected(a);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteSelected(a);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) selectAllObjects(a);
  }

  if (g_search[0]) {  // search: a flat list of matches
    for (const SceneObject& o : a.doc.scene().objects)
      if (matches(o.name)) drawObject(a, o.guid, true);
  } else {
    for (const std::string& r : a.doc.childrenOf("")) drawObject(a, r, false);
  }
  // drop on the empty area: unparent, last among the roots
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.y > 8) {
    ImGui::InvisibleButton("##hier_empty", ImVec2(avail.x, avail.y));
    if (ImGui::IsItemClicked() && !io.KeyCtrl && !io.KeyShift) a.doc.select("");
    if (ImGui::BeginPopupContextItem("##hier_empty_ctx")) {
      drawCreateMenuItems(a, "");
      ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_OBJECT"))
        a.doc.reparentMany(dragSet(a, static_cast<const char*>(p->Data)), "", "");
      ImGui::EndDragDropTarget();
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
