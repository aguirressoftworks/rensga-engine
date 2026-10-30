// Guid reference field: a button that shows the target, a picker popup (scene objects and project assets, searchable)
// and drop targets for RENSGA_OBJECT (Hierarchy) and RENSGA_ASSET_GUID (Project window).
#include <algorithm>
#include <cstring>

#include "drawers.h"
#include "panels/asset_ui.h"
#include "rensga/editor/asset_database.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {
const char* kNullGuid = "00000000-0000-0000-0000-000000000000";

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string meshName(const std::filesystem::path& p) {
  std::string n = p.filename().string();
  if (n.size() > 10) n.resize(n.size() - 10);  // .prim.json
  return n;
}
}  // namespace

std::string guidLabel(App& a, const std::string& cur, bool* known) {
  if (known) *known = true;
  if (cur.empty() || cur == kNullGuid) return "None";
  if (const SceneObject* so = a.doc.find(cur)) return so->name + "  (object)";
  auto mf = a.meshes.files().find(cur);
  if (mf != a.meshes.files().end()) return meshName(mf->second) + "  (mesh)";
  if (const AssetEntry* e = assetUi().sync.db().findGuid(cur)) return e->rel + "  (asset)";
  if (known) *known = false;
  return "Missing  " + cur;
}

bool guidPicker(App& a, const char* id, const std::string& cur, const char* fieldName, bool wantObjects, bool wantAssets, std::string& out) {
  bool picked = false;
  ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(520, 380));
  if (!ImGui::BeginPopup(id)) return false;
  static char search[96] = {};
  if (ImGui::IsWindowAppearing()) { search[0] = 0; ImGui::SetKeyboardFocusHere(); }
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##s", "Search", search, sizeof search);
  const std::string q = lower(search);
  auto match = [&](const std::string& s) { return q.empty() || lower(s).find(q) != std::string::npos; };
  if (ImGui::Selectable("None", cur.empty() || cur == kNullGuid)) { out = kNullGuid; picked = true; }
  const bool meshField = std::strstr(fieldName, "esh") != nullptr;
  if (wantObjects && !meshField) {
    ImGui::SeparatorText("Scene objects");
    for (const SceneObject& so : a.doc.scene().objects)
      if (match(so.name) && ImGui::Selectable((so.name + "##o" + so.guid).c_str(), so.guid == cur)) { out = so.guid; picked = true; }
  }
  if (meshField && !a.meshes.files().empty()) {
    ImGui::SeparatorText("Mesh assets");
    for (const auto& kv : a.meshes.files()) {
      std::string n = meshName(kv.second);
      if (match(n) && ImGui::Selectable((n + "##m" + kv.first).c_str(), kv.first == cur)) { out = kv.first; picked = true; }
    }
  }
  if (wantAssets) {
    ImGui::SeparatorText("Project assets");
    int shown = 0;
    for (const AssetEntry& e : assetUi().sync.db().entries()) {
      if (e.kind == AssetKind::Folder || !match(e.rel)) continue;
      if (++shown > 300) { ImGui::TextDisabled("... type to narrow the list"); break; }
      if (ImGui::Selectable((e.rel + "##a" + e.guid).c_str(), e.guid == cur)) { out = e.guid; picked = true; }
    }
    if (shown == 0) ImGui::TextDisabled("No matching assets");
  }
  if (picked) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return picked;
}

bool guidDropTarget(App& a, bool wantObjects, bool wantAssets, std::string& out) {
  bool ed = false;
  if (!ImGui::BeginDragDropTarget()) return false;
  if (wantObjects)
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_OBJECT")) {
      out = static_cast<const char*>(p->Data);
      ed = a.doc.find(out) != nullptr;
    }
  if (wantAssets)
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload)) {
      std::string g = static_cast<const char*>(p->Data);
      if (assetUi().sync.db().findGuid(g)) { out = g; ed = true; }
    }
  ImGui::EndDragDropTarget();
  return ed;
}

bool drawGuidField(DrawCtx& c, const json& value, json& out) {
  const std::string cur = value.is_string() ? value.get<std::string>() : std::string();
  bool known = true;
  std::string label = c.mixed ? std::string(mixedText()) : guidLabel(c.a, cur, &known);
  const bool bad = !c.mixed && !known;
  if (bad) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));  // paired with the word "Missing"
  bool open = ImGui::Button((label + "##ref").c_str(), ImVec2(-FLT_MIN, 0));
  if (bad) ImGui::PopStyleColor();
  std::string nv;
  bool ed = guidDropTarget(c.a, true, true, nv);
  if (open) ImGui::OpenPopup("pick");
  if (guidPicker(c.a, "pick", cur, c.f.name, true, true, nv)) ed = true;
  if (ed) out = nv;
  return ed;
}

}  // namespace editor
}  // namespace rensga
