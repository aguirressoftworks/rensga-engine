#include <cmath>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <fstream>
#include <map>
#include <new>
#include <set>

#include <imgui_internal.h>

#include "app.h"
#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"

namespace rensga {
namespace editor {

namespace {

constexpr float kRad2Deg = 57.29577951308232f, kDeg2Rad = 0.017453292519943295f;

// Unity order: rotate about Z, then X, then Y (q = qY * qX * qZ).
void quatToEuler(const float q[4], float e[3]) {
  float x = q[0], y = q[1], z = q[2], w = q[3];
  float sx = 2 * (w * x - y * z);
  sx = sx > 1 ? 1 : sx < -1 ? -1 : sx;
  e[0] = std::asin(sx) * kRad2Deg;
  e[1] = std::atan2(2 * (w * y + x * z), 1 - 2 * (x * x + y * y)) * kRad2Deg;
  e[2] = std::atan2(2 * (w * z + x * y), 1 - 2 * (x * x + z * z)) * kRad2Deg;
}

void eulerToQuat(const float e[3], float q[4]) {
  float hx = e[0] * kDeg2Rad * 0.5f, hy = e[1] * kDeg2Rad * 0.5f, hz = e[2] * kDeg2Rad * 0.5f;
  float sx = std::sin(hx), cx = std::cos(hx), sy = std::sin(hy), cy = std::cos(hy), sz = std::sin(hz), cz = std::cos(hz);
  q[0] = cy * sx * cz + sy * cx * sz;
  q[1] = sy * cx * cz - cy * sx * sz;
  q[2] = cy * cx * sz - sy * sx * cz;
  q[3] = cy * cx * cz + sy * sx * sz;
}

using nlohmann::json;

bool g_gesture = false;  // a widget drag in progress: one undo step for the whole drag
struct EulerCache { std::string key; float e[3] = {0, 0, 0}; } g_euler;  // keeps Euler stable while dragging

void touch(App& a) {
  if (!g_gesture) { a.doc.beginGesture(); g_gesture = true; }
}

double q7(float v) {  // shortest text a float round-trips through (0.1 not 0.10000000149)
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.7g", static_cast<double>(v));
  return std::strtod(buf, nullptr);
}

std::string pretty(const char* n) {  // jumpSpeed -> "Jump Speed"
  std::string out;
  for (const char* p = n; *p; ++p) {
    if (p == n) out += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    else {
      if (std::isupper(static_cast<unsigned char>(*p)) && !std::isupper(static_cast<unsigned char>(p[-1]))) out += ' ';
      out += *p;
    }
  }
  return out;
}

json defaultsFor(const TypeInfo& ti) { return defaultFieldsFor(ti); }

// Component types the target accepts: its inspector rules plus the project's own ConsoleTarget list (scripts).
std::set<std::string> allowedComponents(App& a) {
  static std::string cachedFor;
  static std::set<std::string> cached;
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(a.project.root / "ProjectSettings" / "ConsoleTarget.json", ec);
  const std::string key = a.project.root.string() + "|" + a.target + "|" + std::to_string(ec ? 0 : mt.time_since_epoch().count());
  if (key == cachedFor) return cached;
  cachedFor = key;
  cached.clear();
  if (const TargetModule* tm = a.targetModule())
    for (const std::string& n : tm->inspectorRules(consoleTargetFor(a.project.root)).allowedComponents) cached.insert(n);
  try {
    std::ifstream in(a.project.root / "ProjectSettings" / "ConsoleTarget.json");
    json j = json::parse(in);
    for (const json& n : j.value("allowedComponents", json::array())) cached.insert(n.get<std::string>());
  } catch (...) {}
  return cached;
}

bool beginFields(const char* id) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp)) return false;
  ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.40f);
  ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch, 0.60f);
  return true;
}

void row(const char* label) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

// Euler widget for a quaternion (x y z w). Returns true when edited; q is updated.
bool eulerWidget(const std::string& key, float q[4]) {
  if (g_euler.key != key || !g_gesture) { quatToEuler(q, g_euler.e); g_euler.key = key; }
  bool ed = ImGui::DragFloat3("##rot", g_euler.e, 0.5f, 0, 0, "%.1f");
  if (ed) eulerToQuat(g_euler.e, q);
  return ed;
}

float numOr(const json& v, float d) { return v.is_number() ? v.get<float>() : d; }

bool guidCombo(App& a, const char* fieldName, const std::string& cur, std::string& out) {
  const bool wantsMesh = std::string(fieldName).find("esh") != std::string::npos;
  const std::string kNull = "00000000-0000-0000-0000-000000000000";
  std::string label = "None";
  bool known = cur.empty() || cur == kNull;
  if (const SceneObject* so = a.doc.find(cur)) { label = so->name + "  (object)"; known = true; }
  auto mf = a.meshes.files().find(cur);
  if (mf != a.meshes.files().end()) {
    std::string n = mf->second.filename().string();
    if (n.size() > 10) n.resize(n.size() - 10);  // .prim.json
    label = n + "  (mesh)";
    known = true;
  }
  if (!known) label = "Missing  " + cur;
  bool changed = false;
  if (ImGui::BeginCombo("##g", label.c_str())) {
    if (ImGui::Selectable("None", cur.empty() || cur == kNull)) { out = kNull; changed = true; }
    if (wantsMesh) {
      ImGui::SeparatorText("Mesh assets");
      for (const auto& kv : a.meshes.files()) {
        std::string n = kv.second.filename().string();
        if (n.size() > 10) n.resize(n.size() - 10);
        if (ImGui::Selectable((n + "##" + kv.first).c_str(), kv.first == cur)) { out = kv.first; changed = true; }
      }
    } else {
      ImGui::SeparatorText("Scene objects");
      for (const SceneObject& so : a.doc.scene().objects)
        if (ImGui::Selectable((so.name + "##" + so.guid).c_str(), so.guid == cur)) { out = so.guid; changed = true; }
    }
    ImGui::EndCombo();
  }
  return changed;
}

// One reflected field. `value` is what the scene holds (or the default); returns true and fills `out` on edit.
bool fieldWidget(App& a, const FieldDesc& f, const std::string& rowKey, const json& value, json& out) {
  switch (f.type) {
    case FieldType::Scalar: {
      float v = numOr(value, 0);
      bool ed = f.hasRange ? ImGui::SliderFloat("##v", &v, f.rangeMin, f.rangeMax, "%.2f")
                           : ImGui::DragFloat("##v", &v, 0.05f);
      if (ed) out = q7(v);
      return ed;
    }
    case FieldType::I32: {
      int v = value.is_number() ? value.get<int>() : 0;
      bool ed = f.hasRange ? ImGui::SliderInt("##v", &v, static_cast<int>(f.rangeMin), static_cast<int>(f.rangeMax))
                           : ImGui::DragInt("##v", &v, 0.2f);
      if (ed) out = v;
      return ed;
    }
    case FieldType::Bool: {
      bool v = value.is_boolean() && value.get<bool>();
      bool ed = ImGui::Checkbox("##v", &v);
      if (ed) out = v;
      return ed;
    }
    case FieldType::Vector3: {
      float v[3] = {0, 0, 0};
      if (value.is_array() && value.size() == 3) for (int i = 0; i < 3; ++i) v[i] = numOr(value[i], 0);
      bool ed = ImGui::DragFloat3("##v", v, 0.05f);
      if (ed) out = json::array({q7(v[0]), q7(v[1]), q7(v[2])});
      return ed;
    }
    case FieldType::Quaternion: {
      float q[4] = {0, 0, 0, 1};
      if (value.is_array() && value.size() == 4) for (int i = 0; i < 4; ++i) q[i] = numOr(value[i], q[i]);
      bool ed = eulerWidget(rowKey, q);
      if (ed) out = json::array({q7(q[0]), q7(q[1]), q7(q[2]), q7(q[3])});
      return ed;
    }
    case FieldType::Color32: {
      float c[4] = {1, 1, 1, 1};
      if (value.is_array() && value.size() == 4) {
        bool fl = false;
        for (int i = 0; i < 4; ++i) fl |= value[i].is_number_float();
        for (int i = 0; i < 4; ++i) c[i] = fl ? numOr(value[i], 1) : numOr(value[i], 255) / 255.0f;
      }
      bool ed = ImGui::ColorEdit4("##v", c, ImGuiColorEditFlags_AlphaPreview);
      if (ed) {
        json arr = json::array();
        for (int i = 0; i < 4; ++i) arr.push_back(static_cast<int>(c[i] * 255.0f + 0.5f));
        out = arr;
      }
      return ed;
    }
    case FieldType::Guid: {
      std::string cur = value.is_string() ? value.get<std::string>() : std::string();
      std::string nv;
      bool ed = guidCombo(a, f.name, cur, nv);
      if (ed) out = nv;
      return ed;
    }
  }
  return false;
}

}  // namespace

void drawInspector(App& a) {
  if (g_gesture && !ImGui::IsAnyItemActive()) { a.doc.endGesture(); g_gesture = false; }
  if (!a.showInspector) return;
  if (!ImGui::Begin("Inspector", nullptr)) { ImGui::End(); return; }
  const SceneObject* o = a.doc.find(a.doc.selected());
  if (!o) {
    ImGui::TextDisabled("Select an object");
    ImGui::End();
    return;
  }
  const std::string guid = o->guid;
  bool active = o->active;
  if (ImGui::Checkbox("##active", &active)) a.doc.setActive(guid, active);
  ImGui::SameLine();
  char name[128];
  std::snprintf(name, sizeof name, "%s", o->name.c_str());
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputText("##name", name, sizeof name, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    a.doc.rename(guid, name);
  if (ImGui::IsItemDeactivatedAfterEdit()) a.doc.rename(guid, name);
  ImGui::TextDisabled("%s", guid.c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen) && beginFields("##xf")) {
    float p[3] = {o->position[0], o->position[1], o->position[2]};
    float q[4] = {o->rotation[0], o->rotation[1], o->rotation[2], o->rotation[3]};
    float s[3] = {o->scale[0], o->scale[1], o->scale[2]};
    bool edited = false, rotEdited = false;
    row("Position");
    edited |= ImGui::DragFloat3("##p", p, 0.05f);
    row("Rotation");
    rotEdited = eulerWidget(guid + "/T", q);
    edited |= rotEdited;
    row("Scale");
    edited |= ImGui::DragFloat3("##s", s, 0.02f);
    ImGui::EndTable();
    if (edited) {
      touch(a);
      a.doc.setTransform(guid, p, q, s);
    }
  }

  const std::set<std::string> allowed = allowedComponents(a);
  const TargetModule* tm = a.targetModule();
  const std::string targetName = tm ? std::string(tm->displayName()) : std::string("target");
  int removeIdx = -1;
  for (int ci = 0; ci < static_cast<int>(o->components.size()); ++ci) {
    const SceneComponent& c = o->components[ci];
    ImGui::PushID(ci);
    bool open = ImGui::CollapsingHeader(c.type.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (ImGui::BeginPopupContextItem("cmenu")) {
      if (ImGui::MenuItem("Remove Component")) removeIdx = ci;
      ImGui::EndPopup();
    }
    if (open) {
      if (!allowed.empty() && !allowed.count(c.type)) {
        ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
        ImGui::TextWrapped("Not supported by %s: the cook will reject this component.", targetName.c_str());
        ImGui::PopStyleColor();
      }
      const TypeInfo* ti = TypeRegistry::instance().find(c.type.c_str());
      if (ti) {
        const json dflt = defaultsFor(*ti);
        bool inTable = false;
        for (u32 i = 0; i < ti->fieldCount; ++i) {
          const FieldDesc& f = ti->fields[i];
          if (f.header) {
            if (inTable) { ImGui::EndTable(); inTable = false; }
            ImGui::SeparatorText(f.header);
          }
          if (!inTable) inTable = beginFields("##f");
          if (!inTable) break;
          row(pretty(f.name).c_str());
          ImGui::PushID(f.name);
          const json& cur = c.fields.contains(f.name) ? c.fields[f.name] : (dflt.contains(f.name) ? dflt[f.name] : json());
          json nv;
          if (fieldWidget(a, f, guid + "/" + std::to_string(ci) + "/" + f.name, cur, nv)) {
            touch(a);
            a.doc.setField(guid, ci, f.name, nv);
          }
          ImGui::PopID();
        }
        if (inTable) ImGui::EndTable();
      } else {  // type unknown to the editor (e.g. a project script): show the raw scene data
        if (beginFields("##raw")) {
          for (auto it = c.fields.begin(); it != c.fields.end(); ++it) {
            row(it.key().c_str());
            ImGui::BeginDisabled();
            ImGui::TextUnformatted(it.value().dump().c_str());
            ImGui::EndDisabled();
          }
          ImGui::EndTable();
        }
      }
    }
    ImGui::PopID();
  }
  if (removeIdx >= 0) a.doc.removeComponent(guid, removeIdx);

  ImGui::Spacing();
  if (ImGui::Button("Add Component", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("addcomp");
  ImGui::SetNextWindowSizeConstraints(ImVec2(240, 0), ImVec2(FLT_MAX, 360));
  if (ImGui::BeginPopup("addcomp")) {
    std::set<std::string> projectScripts;
    for (const ScriptInfo& sc : a.scripts.list) projectScripts.insert(sc.name);
    if (ImGui::BeginMenu("Scripts")) {
      if (a.scripts.list.empty()) ImGui::TextDisabled("No scripts in Assets/");
      for (const ScriptInfo& sc : a.scripts.list) {
        const std::string why = scriptStatus(a, sc.name);
        ImGui::BeginDisabled(!why.empty());
        if (ImGui::MenuItem(sc.name.c_str(), why.empty() ? nullptr : why.c_str())) {
          addScriptToObject(a, guid, sc.name);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
      }
      ImGui::EndMenu();
    }
    ImGui::Separator();
    std::vector<const TypeInfo*> types;
    const TypeRegistry& reg = TypeRegistry::instance();
    for (u32 i = 0; i < reg.count(); ++i)
      if (const TypeInfo* ti = reg.byId(i)) if (ti->asComponent && !projectScripts.count(ti->name)) types.push_back(ti);
    std::sort(types.begin(), types.end(), [](const TypeInfo* x, const TypeInfo* y) { return std::strcmp(x->name, y->name) < 0; });
    for (const TypeInfo* ti : types) {
      const bool ok = allowed.empty() || allowed.count(ti->name);
      ImGui::BeginDisabled(!ok);
      if (ImGui::MenuItem(ti->name)) a.doc.addComponent(guid, ti->name, defaultsFor(*ti));
      ImGui::EndDisabled();
      if (!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Not in the component list of %s (ConsoleTarget allowedComponents).", targetName.c_str());
    }
    ImGui::EndPopup();
  }
  // a script dragged from the Project window onto the Inspector goes on the selected object
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  if (ImGui::BeginDragDropTargetCustom(win->Rect(), win->ID)) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_SCRIPT")) addScriptToObject(a, guid, static_cast<const char*>(p->Data));
    ImGui::EndDragDropTarget();
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
