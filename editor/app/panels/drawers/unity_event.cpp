// UnityEvent drawer (API 4): a list of listeners. Each one is a target object, a component + method picked from the
// method registry (METHOD(...) declarations), and an argument for one-argument methods.
// JSON shape: [{"target": guid, "type": "Light", "method": "SetIntensity", "arg": 2.5}, ...] (see unity_event.h).
#include <cstring>

#include "drawers.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/unity_event.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {

const char* argKindName(MethodArgKind k) {
  switch (k) {
    case MethodArgKind::Scalar: return "float";
    case MethodArgKind::I32: return "int";
    case MethodArgKind::Bool: return "bool";
    case MethodArgKind::Object: return "object";
    default: return "";
  }
}

json defaultArg(MethodArgKind k) {
  switch (k) {
    case MethodArgKind::Scalar: return 0.0;
    case MethodArgKind::I32: return 0;
    case MethodArgKind::Bool: return false;
    case MethodArgKind::Object: return "00000000-0000-0000-0000-000000000000";
    default: return json();
  }
}

// First component on the object that has a registered method: the listener a "+" click starts from.
json firstListener(App& a, const std::string& owner) {
  json l = {{"target", owner}, {"type", ""}, {"method", ""}};
  if (const SceneObject* o = a.doc.find(owner))
    for (const SceneComponent& c : o->components) {
      const TypeInfo* ti = TypeRegistry::instance().find(c.type.c_str());
      if (!ti) continue;
      auto ms = methodsOf(*ti);
      if (ms.empty()) continue;
      l["type"] = c.type;
      l["method"] = ms[0]->name;
      if (ms[0]->arg != MethodArgKind::None) l["arg"] = defaultArg(ms[0]->arg);
      break;
    }
  return l;
}

}  // namespace

bool drawUnityEventField(DrawCtx& c, const json& value, json& out) {
  json ev = value.is_array() ? value : json::array();
  bool edited = false;
  int removeAt = -1;
  const float w = ImGui::GetContentRegionAvail().x;
  ImGui::BeginGroup();
  if (ev.empty()) ImGui::TextDisabled("No listeners");
  for (int i = 0; i < static_cast<int>(ev.size()); ++i) {
    json& l = ev[i];
    ImGui::PushID(i);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::BeginChild("l", ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    const std::string target = l.value("target", std::string());
    // target object
    ImGui::TextDisabled("Listener %d", i + 1);
    ImGui::SameLine(w - 62);
    if (ImGui::SmallButton("Remove")) removeAt = i;
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool known = true;
    std::string tl = guidLabel(c.a, target, &known);
    if (ImGui::Button((tl + "##t").c_str(), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("pickt");
    std::string nv;
    if (guidDropTarget(c.a, true, false, nv) | guidPicker(c.a, "pickt", target, "", true, false, nv)) {
      l["target"] = nv;
      // a new target keeps the method only if the new object has it
      l["type"] = "";
      l["method"] = "";
      l.erase("arg");
      edited = true;
    }
    // function: Type.Method of the target's components
    const std::string curType = l.value("type", std::string()), curMethod = l.value("method", std::string());
    const std::string fnLabel = curMethod.empty() ? std::string("No function") : curType + "." + curMethod;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##fn", fnLabel.c_str())) {
      bool any = false;
      if (const SceneObject* o = c.a.doc.find(target))
        for (const SceneComponent& comp : o->components) {
          const TypeInfo* ti = TypeRegistry::instance().find(comp.type.c_str());
          if (!ti) continue;
          for (const MethodInfo* m : methodsOf(*ti)) {
            any = true;
            std::string item = comp.type + "." + m->name;
            if (m->arg != MethodArgKind::None) item += std::string(" (") + argKindName(m->arg) + ")";
            if (ImGui::Selectable((item + "##" + comp.type + m->name).c_str(), curType == comp.type && curMethod == m->name)) {
              l["type"] = comp.type;
              l["method"] = m->name;
              if (m->arg == MethodArgKind::None) l.erase("arg");
              else l["arg"] = defaultArg(m->arg);
              edited = true;
            }
          }
        }
      if (!any) ImGui::TextDisabled("The target has no METHOD(...) functions");
      ImGui::EndCombo();
    }
    // argument
    const TypeInfo* ti = TypeRegistry::instance().find(curType.c_str());
    const MethodInfo* mi = ti ? findMethod(*ti, curMethod.c_str()) : nullptr;
    if (!curMethod.empty() && !mi) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
      ImGui::TextWrapped("Missing function %s.%s: the cook will reject this listener.", curType.c_str(), curMethod.c_str());
      ImGui::PopStyleColor();
    }
    if (mi && mi->arg != MethodArgKind::None) {
      ImGui::SetNextItemWidth(-FLT_MIN);
      const json cur = l.contains("arg") ? l["arg"] : defaultArg(mi->arg);
      if (mi->arg == MethodArgKind::Scalar) {
        float v = cur.is_number() ? cur.get<float>() : 0;
        if (ImGui::DragFloat("##arg", &v, 0.05f)) { l["arg"] = q7(v); edited = true; }
      } else if (mi->arg == MethodArgKind::I32) {
        int v = cur.is_number() ? cur.get<int>() : 0;
        if (ImGui::DragInt("##arg", &v, 0.2f)) { l["arg"] = v; edited = true; }
      } else if (mi->arg == MethodArgKind::Bool) {
        bool v = cur.is_boolean() && cur.get<bool>();
        if (ImGui::Checkbox("##arg", &v)) { l["arg"] = v; edited = true; }
      } else {
        std::string g = cur.is_string() ? cur.get<std::string>() : std::string();
        std::string an;
        if (ImGui::Button((guidLabel(c.a, g) + "##arg").c_str(), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("picka");
        if (guidDropTarget(c.a, true, false, an) | guidPicker(c.a, "picka", g, "", true, false, an)) { l["arg"] = an; edited = true; }
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  if (removeAt >= 0) { ev.erase(static_cast<size_t>(removeAt)); edited = true; }
  ImGui::BeginDisabled(ev.size() >= kMaxUnityEventListeners);
  if (ImGui::Button("+ Add Listener")) { ev.push_back(firstListener(c.a, c.ownerGuid)); edited = true; }
  ImGui::EndDisabled();
  if (ev.size() >= kMaxUnityEventListeners && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("An event holds up to %u listeners.", static_cast<unsigned>(kMaxUnityEventListeners));
  ImGui::EndGroup();
  if (edited) out = ev;
  return edited;
}

}  // namespace editor
}  // namespace rensga
