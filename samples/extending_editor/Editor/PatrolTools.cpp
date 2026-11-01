// F6.07 sample: editor extensions for the Patroller component. This file is compiled into the editor only (never into a player or a
// console build); the editor rebuilds it in the background when you save and reloads it without a restart.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>
#include <rensga/editor/extension.h>

using namespace rensga::editor::ext;

// ---- custom inspector: drawn under the generated fields of every Patroller ----------------------------------------------------------
static void patrollerInspector(ComponentView& v) {
  float a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
  v.GetVector3("pointA", a);
  v.GetVector3("pointB", b);
  const float length = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));

  ImGui::SeparatorText("Patrol tools");
  // The route, drawn to scale in a small box (top view: x across, z down).
  const ImVec2 size(ImGui::GetContentRegionAvail().x, 56);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##route", size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(32, 36, 44, 255), 4);
  const float span = std::fmax(length, 1.0f);
  const float cx = (a[0] + b[0]) * 0.5f, cz = (a[2] + b[2]) * 0.5f;
  const float scale = (size.x - 40) / (span * 1.2f);
  auto toScreen = [&](const float p[3]) { return ImVec2(p0.x + size.x * 0.5f + (p[0] - cx) * scale, p0.y + size.y * 0.5f + (p[2] - cz) * scale); };
  dl->AddLine(toScreen(a), toScreen(b), IM_COL32(110, 230, 130, 255), 2.0f);
  dl->AddCircleFilled(toScreen(a), 5, IM_COL32(255, 205, 40, 255));
  dl->AddCircleFilled(toScreen(b), 5, IM_COL32(255, 120, 60, 255));
  ImGui::Text("Route length %.1f m", length);

  // Edits go through the same undo machinery as the generated fields: Ctrl+Z reverts a swap in one step.
  if (ImGui::Button("Swap A and B")) {
    v.SetVector3("pointA", b);
    v.SetVector3("pointB", a);
  }
  ImGui::SameLine();
  if (ImGui::Button("Flatten to ground")) {
    a[1] = b[1] = 0.5f;
    v.SetVector3("pointA", a);
    v.SetVector3("pointB", b);
  }

  // The data asset behind the `stats` field, looked up through the asset database API.
  const std::string guid = v.GetGuid("stats");
  if (guid.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "[!] No stats asset: drag an EnemyStats asset onto the Stats field");
  } else {
    const nlohmann::json data = Assets::ReadDataAsset(guid);
    if (data.is_null()) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1), "[x] The Stats field points to something that is not a data asset");
    } else {
      const nlohmann::json& f = data["fields"];
      const float speed = f.value("speed", 0.0f);
      ImGui::Text("%s (%s): speed %.2f m/s, one trip takes %.1f s", f.value("label", std::string("?")).c_str(), data.value("type", std::string("?")).c_str(), speed,
                  speed > 0 ? length / speed : 0.0f);
      ImGui::TextDisabled("%s", Assets::PathOf(guid).c_str());
    }
  }
}
RENSGA_CUSTOM_INSPECTOR(Patroller, patrollerInspector)

// ---- editor window: every Patroller of the open scene in one table -------------------------------------------------------------------
static void overviewWindow() {
  const std::vector<ObjectInfo> list = SceneEdit::Find("", "Patroller");
  ImGui::Text("%d patroller(s) in the open scene", static_cast<int>(list.size()));
  if (ImGui::BeginTable("##patrollers", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Object");
    ImGui::TableSetupColumn("Label");
    ImGui::TableSetupColumn("Speed");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (const ObjectInfo& o : list) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(o.name.c_str());
      const std::string stats = SceneEdit::GetField(o.guid, "Patroller", "stats").get<std::string>();
      const nlohmann::json data = stats.empty() ? nlohmann::json() : Assets::ReadDataAsset(stats);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(data.is_null() ? "-" : data["fields"].value("label", std::string("?")).c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%.2f", data.is_null() ? 0.0f : data["fields"].value("speed", 0.0f));
      ImGui::TableNextColumn();
      ImGui::PushID(o.guid.c_str());
      if (ImGui::SmallButton("Select")) Selection::Select(o.guid);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ImGui::Button("Select all")) {
    std::vector<std::string> guids;
    for (const ObjectInfo& o : list) guids.push_back(o.guid);
    Selection::SelectMany(guids);
  }
}
RENSGA_EDITOR_WINDOW("Patrol Overview", overviewWindow)

// ---- menu items ---------------------------------------------------------------------------------------------------------------------------
// Tools/Patrol/Line Up Selection: spreads the selected objects along x, 3 m apart. One undo step for the whole row.
static void lineUp() {
  const std::vector<std::string> sel = Selection::Guids();
  if (sel.size() < 2) {
    Log::Warning("Line Up Selection needs at least two selected objects");
    Log::Status("Select two or more objects first");
    return;
  }
  SceneEdit::BeginGroup();
  float x = -3.0f * static_cast<float>(sel.size() - 1) * 0.5f;
  for (const std::string& g : sel) {
    float p[3];
    if (SceneEdit::GetPosition(g, p)) SceneEdit::SetPosition(g, x, p[1], p[2]);
    x += 3.0f;
  }
  SceneEdit::EndGroup();
  Log::Info("lined up " + std::to_string(sel.size()) + " objects");
}
RENSGA_MENU_ITEM_KEY("Tools/Patrol/Line Up Selection", "Ctrl+Shift+L", lineUp)

static void selectAll() {
  std::vector<std::string> guids;
  for (const ObjectInfo& o : SceneEdit::Find("", "Patroller")) guids.push_back(o.guid);
  Selection::SelectMany(guids);
  Log::Status("Selected " + std::to_string(guids.size()) + " patroller(s)");
}
RENSGA_MENU_ITEM("Tools/Patrol/Select All Patrollers", selectAll)
