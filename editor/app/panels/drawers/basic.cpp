// Drawers for Scalar, I32, Bool, Vector3, Quaternion (Euler degrees) and Color32.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "drawers.h"
#include <imgui_internal.h>

namespace rensga {
namespace editor {

using nlohmann::json;

bool drawGuidField(DrawCtx&, const json&, json&);  // reference.cpp

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

struct EulerCache { std::string key; float e[3] = {0, 0, 0}; } g_euler;

float numOr(const json& v, float d) { return v.is_number() ? v.get<float>() : d; }

}  // namespace

bool& inspectorGesture() {
  static bool g = false;
  return g;
}

double q7(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.7g", static_cast<double>(v));
  return std::strtod(buf, nullptr);
}

// An em dash marks a mixed value; ASCII fallback if the font lacks the glyph.
const char* mixedText() { return "\xE2\x80\x94"; }

bool eulerWidget(const std::string& key, float q[4], const char* label, bool mixed) {
  if (g_euler.key != key || !inspectorGesture()) { quatToEuler(q, g_euler.e); g_euler.key = key; }
  bool ed = ImGui::DragFloat3(label, g_euler.e, 0.5f, 0, 0, mixed ? mixedText() : "%.1f");
  if (ed) eulerToQuat(g_euler.e, q);
  return ed;
}

bool drawScalarField(DrawCtx& c, const json& value, json& out) {
  float v = numOr(value, 0);
  const char* fmt = c.mixed ? mixedText() : "%.2f";
  bool ed = c.f.hasRange ? ImGui::SliderFloat("##v", &v, c.f.rangeMin, c.f.rangeMax, fmt) : ImGui::DragFloat("##v", &v, 0.05f, 0, 0, c.mixed ? mixedText() : "%.3g");
  if (ed) out = q7(v);
  return ed;
}

bool drawI32Field(DrawCtx& c, const json& value, json& out) {
  int v = value.is_number() ? value.get<int>() : 0;
  const char* fmt = c.mixed ? mixedText() : "%d";
  bool ed = c.f.hasRange ? ImGui::SliderInt("##v", &v, static_cast<int>(c.f.rangeMin), static_cast<int>(c.f.rangeMax), fmt)
                         : ImGui::DragInt("##v", &v, 0.2f, 0, 0, fmt);
  if (ed) out = v;
  return ed;
}

bool drawBoolField(DrawCtx& c, const json& value, json& out) {
  bool v = value.is_boolean() && value.get<bool>();
  if (c.mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
  bool ed = ImGui::Checkbox("##v", &v);
  if (c.mixed) ImGui::PopItemFlag();
  if (ed) out = v;
  return ed;
}

bool drawVector3Field(DrawCtx& c, const json& value, json& out) {
  float v[3] = {0, 0, 0};
  if (value.is_array() && value.size() == 3) for (int i = 0; i < 3; ++i) v[i] = numOr(value[i], 0);
  bool ed = ImGui::DragFloat3("##v", v, 0.05f, 0, 0, c.mixed ? mixedText() : "%.3g");
  if (ed) out = json::array({q7(v[0]), q7(v[1]), q7(v[2])});
  return ed;
}

bool drawQuaternionField(DrawCtx& c, const json& value, json& out) {
  float q[4] = {0, 0, 0, 1};
  if (value.is_array() && value.size() == 4) for (int i = 0; i < 4; ++i) q[i] = numOr(value[i], q[i]);
  bool ed = eulerWidget(c.key, q, "##rot", c.mixed);
  if (ed) out = json::array({q7(q[0]), q7(q[1]), q7(q[2]), q7(q[3])});
  return ed;
}

bool drawColor32Field(DrawCtx& c, const json& value, json& out) {
  float col[4] = {1, 1, 1, 1};
  if (value.is_array() && value.size() == 4) {
    bool fl = false;
    for (int i = 0; i < 4; ++i) fl |= value[i].is_number_float();
    for (int i = 0; i < 4; ++i) col[i] = fl ? numOr(value[i], 1) : numOr(value[i], 255) / 255.0f;
  }
  if (c.mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
  bool ed = ImGui::ColorEdit4("##v", col, ImGuiColorEditFlags_AlphaPreview);
  if (c.mixed) ImGui::PopItemFlag();
  if (ed) {
    json arr = json::array();
    for (int i = 0; i < 4; ++i) arr.push_back(static_cast<int>(col[i] * 255.0f + 0.5f));
    out = arr;
  }
  return ed;
}

// ---- F6 field types: String, Enum, Vector2, Array ---------------------------------------------------------------------------

bool drawStringField(DrawCtx& c, const json& value, json& out) {
  char buf[String32::kCapacity] = {};
  if (value.is_string()) std::snprintf(buf, sizeof buf, "%s", value.get<std::string>().c_str());
  ImGuiInputTextFlags fl = c.mixed ? ImGuiInputTextFlags_ReadOnly : 0;
  bool ed = ImGui::InputText("##v", buf, sizeof buf, fl);  // 31 characters + NUL, the reflected capacity
  if (ed) out = std::string(buf);
  return ed;
}

bool drawEnumField(DrawCtx& c, const json& value, json& out) {
  const u32 n = enumLabelCount(c.f);
  int cur = value.is_number_integer() ? value.get<int>() : 0;
  if (n == 0) {  // no labels (stripped metadata): a plain number
    bool ed = ImGui::DragInt("##v", &cur, 0.2f);
    if (ed) out = cur;
    return ed;
  }
  char label[64];
  if (!(cur >= 0 && static_cast<u32>(cur) < n && enumLabel(c.f, static_cast<u32>(cur), label, sizeof label)))
    std::snprintf(label, sizeof label, "%d (out of range)", cur);
  bool ed = false;
  if (ImGui::BeginCombo("##v", c.mixed ? mixedText() : label)) {
    for (u32 i = 0; i < n; ++i) {
      char item[64];
      enumLabel(c.f, i, item, sizeof item);
      if (ImGui::Selectable(item, !c.mixed && static_cast<int>(i) == cur)) {
        out = static_cast<int>(i);
        ed = true;
      }
    }
    ImGui::EndCombo();
  }
  return ed;
}

bool drawVector2Field(DrawCtx& c, const json& value, json& out) {
  float v[2] = {0, 0};
  if (value.is_array() && value.size() == 2) for (int i = 0; i < 2; ++i) v[i] = numOr(value[i], 0);
  bool ed = ImGui::DragFloat2("##v", v, 0.05f, 0, 0, c.mixed ? mixedText() : "%.3g");
  if (ed) out = json::array({q7(v[0]), q7(v[1])});
  return ed;
}

// A fixed-capacity list: a count header, one row per element drawn by the drawer of the element type, Add and Remove buttons.
// The edit is the whole new array, so it is one undo step like every other field.
bool drawArrayField(DrawCtx& c, const json& value, json& out) {
  json arr = value.is_array() ? value : json::array();
  const int cap = static_cast<int>(c.f.arrayCap);
  bool edited = false;
  char head[64];
  std::snprintf(head, sizeof head, "%d / %d elements", static_cast<int>(arr.size()), cap);
  ImGui::TextUnformatted(c.mixed ? mixedText() : head);
  FieldDesc ef = c.f;  // the element drawer sees a field of the element type
  ef.type = c.f.elemType;
  ef.arrayCap = 0;
  ef.hasRange = false;
  FieldDrawer elem = nullptr;
  switch (c.f.elemType) {
    case FieldType::I32: elem = drawI32Field; break;
    case FieldType::Guid: elem = drawGuidField; break;
    case FieldType::Vector3: elem = drawVector3Field; break;
    default: elem = drawScalarField; break;
  }
  int remove = -1;
  for (int i = 0; i < static_cast<int>(arr.size()); ++i) {
    ImGui::PushID(i);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d", i);
    ImGui::SameLine();
    float rm = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - rm - ImGui::GetStyle().ItemSpacing.x);
    DrawCtx ec{c.a, ef, c.key + "[" + std::to_string(i) + "]", c.ownerGuid, c.mixed};
    json eo;
    if (elem(ec, arr[static_cast<size_t>(i)], eo)) {
      arr[static_cast<size_t>(i)] = eo;
      edited = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("-", ImVec2(rm, 0))) remove = i;
    ImGui::PopID();
  }
  if (remove >= 0) {
    arr.erase(arr.begin() + remove);
    edited = true;
  }
  if (static_cast<int>(arr.size()) < cap && ImGui::Button("+ Add")) {
    switch (c.f.elemType) {
      case FieldType::I32: arr.push_back(0); break;
      case FieldType::Guid: arr.push_back("00000000-0000-0000-0000-000000000000"); break;
      case FieldType::Vector3: arr.push_back(json::array({0.0, 0.0, 0.0})); break;
      default: arr.push_back(0.0); break;
    }
    edited = true;
  }
  if (edited) out = arr;
  return edited;
}

}  // namespace editor
}  // namespace rensga
