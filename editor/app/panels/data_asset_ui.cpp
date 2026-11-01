// F6.07 (Editor 13): data assets in the editor. Create > Data Asset > <Type> in the Project window writes <Type>'s defaults; the
// Inspector shows the fields through the same drawers as components (enum dropdowns, arrays, asset pickers), every edit is undoable
// and is written to the file at once, so Play and the cooker always see what the Inspector shows.
//
// --ui-event verbs (names with spaces: use '_'):
//   datacreate:<Type>[|name]     create in the current Project folder and select it ("data: created <rel> type=<Type>")
//   dataset:<field>=<json>       edit a field of the selected data asset ("data: set <field> ok undo=<n>")
//   dataundo | dataredo          undo / redo in the selected data asset
//   datadump                     print "data: <rel> type=<Type> fields=<json> undo=<n> redo=<n> unknown=<n>"
//   datatype:<Type>              give an untyped data asset its type
#include <cfloat>
#include <cstdio>
#include <fstream>

#include <imgui.h>

#include "data_asset_ui.h"
#include "drawers/drawers.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "rensga/cooker/data_cook.h"
#include "rensga/editor/data_asset_doc.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/inspector_model.h"
#include "theme.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct DataUi {
  DataAssetDoc doc;
  std::string guid;
  fs::file_time_type stamp{};
  bool gesture = false;
  std::string error;
};
DataUi& ui() {
  static DataUi u;
  return u;
}

fs::file_time_type mtime(const fs::path& p) {
  std::error_code ec;
  const auto t = fs::last_write_time(p, ec);
  return ec ? fs::file_time_type{} : t;
}

// The document of the selected asset; reloaded when another asset is picked or the file changed on disk (and no edit is in flight).
DataUi& open(const AssetEntry& e) {
  DataUi& u = ui();
  if (u.guid != e.guid || (!u.gesture && mtime(e.path) != u.stamp)) {
    const bool same = u.guid == e.guid;
    const size_t undo = u.doc.undoDepth();
    std::string err;
    if (!u.doc.load(e.path, err)) u.error = err;
    else u.error.clear();
    (void)same;
    (void)undo;
    u.guid = e.guid;
    u.stamp = mtime(e.path);
  }
  return u;
}

void endGestureIfIdle() {
  DataUi& u = ui();
  if (u.gesture && !ImGui::IsAnyItemActive()) {
    u.doc.endGesture();
    u.gesture = false;
  }
}

}  // namespace

bool drawDataAssetInspector(App& a, const AssetEntry& e) {
  DataUi& u = open(e);
  registerBuiltinDrawers();
  endGestureIfIdle();
  if (!u.error.empty()) {
    ImGui::TextColored(brandToken("danger"), "[x] %s", u.error.c_str());
    return true;
  }
  DataAssetDoc& doc = u.doc;
  const TypeInfo* ti = doc.type();
  ImGui::Spacing();
  if (doc.typeName().empty()) {
    ImGui::TextColored(brandToken("warn"), "[!] This data asset has no type yet.");
    const std::vector<std::string> types = dataAssetTypeNames();
    if (types.empty()) ImGui::TextWrapped("No data asset types: write a struct that derives from DataAsset (REFLECT, SERIALIZE_FIELD) in Assets/Scripts.");
    for (const std::string& t : types)
      if (ImGui::Button(t.c_str())) {
        std::string err;
        std::ifstream in(e.path);
        json j = json::parse(in, nullptr, false);
        in.close();
        if (j.is_object()) {
          j["type"] = t;
          j["fields"] = json::object();
          std::ofstream(e.path) << j.dump(1) << "\n";
          u.guid.clear();
        }
      }
    return true;
  }
  ImGui::TextDisabled("Data asset of type");
  ImGui::SameLine();
  ImGui::PushFont(a.fonts.uiBold, 0.0f);
  ImGui::TextUnformatted(doc.typeName().c_str());
  ImGui::PopFont();
  if (!ti) {
    ImGui::TextColored(brandToken("warn"), "[!] Missing script (%s)", doc.typeName().c_str());
    ImGui::TextWrapped("The script that defines this type is not loaded (still compiling, renamed or deleted). The file is kept as it is.");
    return true;
  }
  ImGui::BeginDisabled(doc.undoDepth() == 0);
  if (ImGui::SmallButton("Undo")) doc.undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(doc.redoDepth() == 0);
  if (ImGui::SmallButton("Redo")) doc.redo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("edits are saved at once");
  const std::vector<std::string> unknown = doc.unknownFields();
  if (!unknown.empty()) {
    ImGui::TextColored(brandToken("warn"), "[!] The type has no field '%s' any more; the cook refuses it. Edit any value to drop it.", unknown[0].c_str());
  }
  ImGui::Spacing();
  if (ImGui::BeginTable("##dataasset", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    const json values = doc.values();
    for (u32 i = 0; i < ti->fieldCount; ++i) {
      const FieldDesc& f = ti->fields[i];
      if (f.hideInInspector) continue;
      if (f.header) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetColumnIndex(1);
        ImGui::SeparatorText(f.header);
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(prettyFieldName(f.name).c_str());
      if (f.tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.tooltip);
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::PushID(f.name);
      DrawCtx ctx{a, f, e.guid + "/" + f.name, std::string(), false};
      json nv;
      FieldDrawer draw = drawerFor(ti->name, f);
      if (draw && values.contains(f.name) && draw(ctx, values[f.name], nv)) {
        if (!u.gesture) { doc.beginGesture(); u.gesture = true; }
        std::string err;
        if (!doc.set(f.name, nv, err)) EditorLog::instance().warn("data asset: " + err);
        u.stamp = mtime(e.path);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  return true;
}

bool createDataAssetFromMenu(App& a, const std::string& typeName, const std::string& name) {
  AssetUi& u = assetUi();
  fs::path made;
  std::string err;
  if (!createDataAsset(u.sync.db(), a.project.root / u.folder, typeName, name, made, err)) {
    EditorLog::instance().warn("create data asset failed: " + err);
    u.errorText = err;
    return false;
  }
  a.assetsStamp = -100;
  if (const AssetEntry* e = u.sync.db().findPath(made)) {
    u.selGuid = e->guid;
    std::printf("data: created %s type=%s\n", e->rel.c_str(), typeName.c_str());
    EditorLog::instance().info("asset: created " + e->rel);
  }
  return true;
}

void drawCreateDataAssetMenu(App& a) {
  const std::vector<std::string> types = dataAssetTypeNames();
  if (!ImGui::BeginMenu("Data Asset")) return;
  if (types.empty()) {
    ImGui::BeginDisabled();
    ImGui::MenuItem("No data asset types yet");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Write a struct that derives from DataAsset (REFLECT + SERIALIZE_FIELD) in Assets/Scripts");
  }
  for (const std::string& t : types)
    if (ImGui::MenuItem(t.c_str())) createDataAssetFromMenu(a, t, std::string());
  ImGui::EndMenu();
}

namespace {

const AssetEntry* selectedData(App& a) {
  AssetUi& u = assetUi();
  const AssetEntry* e = u.selGuid.empty() ? nullptr : u.sync.db().findGuid(u.selGuid);
  if (!e || e->kind != AssetKind::DataAsset) return nullptr;
  (void)a;
  return e;
}

bool dataHook(App& a, const std::string& act, const std::string& arg) {
  if (act.rfind("data", 0) != 0) return false;
  if (act == "datacreate") {
    const size_t bar = arg.find('|');
    createDataAssetFromMenu(a, bar == std::string::npos ? arg : arg.substr(0, bar), bar == std::string::npos ? std::string() : arg.substr(bar + 1));
    return true;
  }
  const AssetEntry* e = selectedData(a);
  if (!e) {
    if (act == "dataset" || act == "dataundo" || act == "dataredo" || act == "datadump" || act == "datatype") std::printf("data: no data asset selected\n");
    return act == "dataset" || act == "dataundo" || act == "dataredo" || act == "datadump" || act == "datatype";
  }
  DataUi& u = open(*e);
  std::string err;
  if (act == "dataset") {
    const size_t eq = arg.find('=');
    json v = eq == std::string::npos ? json() : json::parse(arg.substr(eq + 1), nullptr, false);
    if (eq == std::string::npos || v.is_discarded()) {
      std::printf("data: bad dataset argument (field=<json>)\n");
      return true;
    }
    const bool ok = u.doc.set(arg.substr(0, eq), v, err);
    u.stamp = mtime(e->path);
    std::printf("data: set %s %s undo=%zu%s%s\n", arg.substr(0, eq).c_str(), ok ? "ok" : "refused", u.doc.undoDepth(), ok ? "" : " why=", ok ? "" : err.c_str());
  } else if (act == "dataundo" || act == "dataredo") {
    const bool ok = act == "dataundo" ? u.doc.undo() : u.doc.redo();
    u.stamp = mtime(e->path);
    std::printf("data: %s %s undo=%zu redo=%zu\n", act.c_str() + 4, ok ? "ok" : "nothing", u.doc.undoDepth(), u.doc.redoDepth());
  } else if (act == "datatype") {
    std::ifstream in(e->path);
    json j = json::parse(in, nullptr, false);
    in.close();
    if (j.is_object()) {
      j["type"] = arg;
      j["fields"] = json::object();
      std::ofstream(e->path) << j.dump(1) << "\n";
      u.guid.clear();
    }
    std::printf("data: type %s\n", arg.c_str());
  } else if (act == "datadump") {
    std::printf("data: %s type=%s fields=%s undo=%zu redo=%zu unknown=%zu\n", e->rel.c_str(), u.doc.typeName().c_str(), u.doc.values().dump().c_str(),
                u.doc.undoDepth(), u.doc.redoDepth(), u.doc.unknownFields().size());
  } else {
    return false;
  }
  return true;
}

}  // namespace

RENSGA_EDITOR_PANEL(data_assets, "Data Assets", nullptr, dataHook, nullptr, 0, 460, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
