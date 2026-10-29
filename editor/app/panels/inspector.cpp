#include <cmath>
#include <cstring>

#include "app.h"
#include "rensga/core/reflect.h"

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

bool g_gesture = false;  // a transform drag in progress: one undo step for the whole drag

void drawFieldJson(const char* name, const nlohmann::json& v) {
  std::string t = v.dump();
  ImGui::BeginDisabled();
  ImGui::LabelText(name, "%s", t.c_str());
  ImGui::EndDisabled();
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

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    float p[3] = {o->position[0], o->position[1], o->position[2]};
    float q[4] = {o->rotation[0], o->rotation[1], o->rotation[2], o->rotation[3]};
    float s[3] = {o->scale[0], o->scale[1], o->scale[2]};
    float e[3];
    quatToEuler(q, e);
    bool edited = false;
    edited |= ImGui::DragFloat3("Position", p, 0.05f);
    bool rotEdited = ImGui::DragFloat3("Rotation", e, 0.5f, 0, 0, "%.1f");
    edited |= rotEdited;
    edited |= ImGui::DragFloat3("Scale", s, 0.02f);
    if (rotEdited) eulerToQuat(e, q);
    if (edited) {
      if (!g_gesture) { a.doc.beginGesture(); g_gesture = true; }
      a.doc.setTransform(guid, p, q, s);
    }
  }
  // Which components may be added comes from the target (data-driven, RULES section 1).
  const TargetModule* tm = a.targetModule();
  for (int ci = 0; ci < static_cast<int>(o->components.size()); ++ci) {
    const SceneComponent& c = o->components[ci];
    ImGui::PushID(ci);
    bool open = ImGui::CollapsingHeader(c.type.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
      const TypeInfo* ti = TypeRegistry::instance().find(c.type.c_str());
      if (ti) {
        for (u32 i = 0; i < ti->fieldCount; ++i) {
          const FieldDesc& f = ti->fields[i];
          if (f.header) ImGui::SeparatorText(f.header);
          std::string label = std::string(f.name) + " (" + fieldTypeName(f.type) + ")";
          if (f.hasRange) label += " [" + std::to_string(f.rangeMin) + ".." + std::to_string(f.rangeMax) + "]";
          if (c.fields.contains(f.name)) drawFieldJson(label.c_str(), c.fields[f.name]);
          else drawFieldJson(label.c_str(), "(default)");
        }
      } else {  // type unknown to the editor (e.g. a project script): show the raw scene data
        for (auto it = c.fields.begin(); it != c.fields.end(); ++it) drawFieldJson(it.key().c_str(), it.value());
      }
    }
    ImGui::PopID();
  }
  (void)tm;
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
