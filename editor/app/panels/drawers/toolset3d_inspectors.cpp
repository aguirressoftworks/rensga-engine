// F6.02 (Editor 8): component inspectors of the 3D toolset, drawn under the reflected fields:
//   Light       what the light does and how many lights the scene has against the target's per-object limit
//   Camera      a thumbnail of what this camera sees (the lit scene, at the target's aspect)
//   AudioSource what is missing / which mixer group scales it
//   BoxCollider, SphereCollider, CapsuleCollider   "Fit to mesh" from the MeshRenderer of the same object (one undo step)
//   MeshRenderer  "Bake lighting" state: what the cook will do with this renderer
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "app.h"
#include "panels/drawers/component_inspector.h"
#include "panels/material_editor.h"
#include "render/lit_view.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

namespace {
using nlohmann::json;

int countLights(App& a, int* directional, int* point) {
  *directional = *point = 0;
  for (const SceneObject& o : a.doc.scene().objects) {
    if (!o.active) continue;
    for (const SceneComponent& c : o.components) {
      if (c.type == "Light") (c.fields.value("type", 0) == 1 ? *point : *directional)++;
      else if (c.type == "DirectionalLight") (*directional)++;
    }
  }
  return *directional + *point;
}

// A dimmed paragraph that wraps at the window edge.
void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void note(const char* fmt, ...) {
  char t[400];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(t, sizeof t, fmt, ap);
  va_end(ap);
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", t);
  ImGui::PopStyleColor();
}

void lightInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  if (multi) return;
  const json type = fieldValue(a.doc, refs[0], "type");
  const bool point = type.is_number() && type.get<int>() == 1;
  ImGui::Spacing();
  if (point) {
    const json r = fieldValue(a.doc, refs[0], "range");
    note("Point light: reaches %.1f m (intensity x (1 - d / range)^2)", r.is_number() ? r.get<float>() : 10.0f);
  } else {
    note("Directional light: shines along this object's forward (+Z) axis");
  }
  int d = 0, p = 0;
  const int n = countLights(a, &d, &p);
  const PreviewRules pr = effectivePreviewRules(a.targetModule(), consoleTargetFor(a.project.root));
  char t[200];
  if (pr.maxLightsPerObject > 0)
    std::snprintf(t, sizeof t, "%d lights in this scene (%d directional, %d point); the target lights each object with at most %d", n, d, p, pr.maxLightsPerObject);
  else
    std::snprintf(t, sizeof t, "%d lights in this scene (%d directional, %d point)", n, d, p);
  note("%s", t);
  if (pr.maxLightsPerObject > 0 && n > pr.maxLightsPerObject) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
    ImGui::TextWrapped("[!] More lights than one object can use: bake them (Lighting window, or MeshRenderer > Bake lighting)");
    ImGui::PopStyleColor();
  }
}

void cameraInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  if (multi || !a.loaded) return;
  const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
  const float aspect = ct.width > 0 && ct.height > 0 ? static_cast<float>(ct.width) / static_cast<float>(ct.height) : 16.0f / 9.0f;
  const float w = std::max(80.0f, ImGui::GetContentRegionAvail().x - 8.0f), h = w / aspect;
  ImGui::Spacing();
  ImGui::TextDisabled("Camera preview");
  const unsigned tex = litCameraTexture(a, refs[0].guid, std::min(static_cast<int>(w), 320), std::max(8, static_cast<int>(std::min(w, 320.0f) / aspect)));
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  if (tex) {
    ImGui::Image(static_cast<ImTextureID>(tex), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
  } else {
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(brandToken("surface-2")));
    const char* msg = "No preview: the scene is not loaded as a World yet";
    const ImVec2 ts = ImGui::CalcTextSize(msg);
    dl->AddText(ImVec2(p0.x + (w - ts.x) * 0.5f, p0.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), msg);
  }
  ImGui::GetWindowDrawList()->AddRect(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(brandToken("border-strong")));
}

void audioInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  if (multi) return;
  const json clip = fieldValue(a.doc, refs[0], "clip");
  const std::string g = clip.is_string() ? clip.get<std::string>() : std::string();
  ImGui::Spacing();
  if (g.empty() || g == "00000000-0000-0000-0000-000000000000")
    ImGui::TextColored(brandToken("warn"), "[!] No clip: this source is silent");
  const json grp = fieldValue(a.doc, refs[0], "outputGroup");
  static const char* const kGroups[] = {"Master", "Music", "SFX"};
  const int gi = grp.is_number() ? std::max(0, std::min(2, grp.get<int>())) : 0;
  note("Volume is scaled by the %s mixer group. The mixer has no 3D attenuation: the object's position does not change the sound.", kGroups[gi]);
}

// Local bounds of the object's MeshRenderer / SkinnedMeshRenderer mesh; false when it has none.
bool meshBounds(App& a, const std::string& guid, float mn[3], float mx[3]) {
  const SceneObject* o = a.doc.find(guid);
  if (!o) return false;
  for (const SceneComponent& c : o->components) {
    if (c.type != "MeshRenderer" && c.type != "SkinnedMeshRenderer") continue;
    if (!c.fields.contains("mesh") || !c.fields["mesh"].is_string()) continue;
    if (auto m = a.meshes.get(c.fields["mesh"].get<std::string>())) {
      for (int i = 0; i < 3; ++i) { mn[i] = m->bmin[i]; mx[i] = m->bmax[i]; }
      return true;
    }
  }
  return false;
}

void colliderInspector(App& a, const std::vector<ComponentRef>& refs, bool multi, const char* type) {
  float mn[3], mx[3];
  ImGui::Spacing();
  const bool have = !multi && meshBounds(a, refs[0].guid, mn, mx);
  ImGui::BeginDisabled(!have);
  if (ImGui::Button("Fit to mesh", ImVec2(-FLT_MIN, 0)) && have) {
    a.doc.beginGesture();
    const json center = json::array({(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f});
    const float sx = mx[0] - mn[0], sy = mx[1] - mn[1], sz = mx[2] - mn[2];
    editFields(a.doc, refs, "center", center);
    if (std::string(type) == "BoxCollider") {
      editFields(a.doc, refs, "size", json::array({sx, sy, sz}));
    } else if (std::string(type) == "SphereCollider") {
      editFields(a.doc, refs, "radius", std::max({sx, sy, sz}) * 0.5f);
    } else {  // capsule: the long axis is the field's direction
      const json dir = fieldValue(a.doc, refs[0], "direction");
      const int d = dir.is_number() ? dir.get<int>() : 1;
      const float len = d == 0 ? sx : (d == 2 ? sz : sy);
      const float cross = d == 0 ? std::max(sy, sz) : (d == 2 ? std::max(sx, sy) : std::max(sx, sz));
      editFields(a.doc, refs, "radius", cross * 0.5f);
      editFields(a.doc, refs, "height", std::max(len, cross));
    }
    a.doc.endGesture();
  }
  ImGui::EndDisabled();
  if (!have && !multi) note("Fit to mesh needs a MeshRenderer with a mesh on this object");
}
void boxInspector(App& a, const std::vector<ComponentRef>& r, bool m) { colliderInspector(a, r, m, "BoxCollider"); }
void sphereInspector(App& a, const std::vector<ComponentRef>& r, bool m) { colliderInspector(a, r, m, "SphereCollider"); }
void capsuleInspector(App& a, const std::vector<ComponentRef>& r, bool m) { colliderInspector(a, r, m, "CapsuleCollider"); }

void meshRendererInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  if (multi) return;
  const json bake = fieldValue(a.doc, refs[0], "bakeLighting");
  const json mat = fieldValue(a.doc, refs[0], "material");
  const bool on = bake.is_boolean() && bake.get<bool>();
  const bool hasMat = mat.is_string() && !mat.get<std::string>().empty() && mat.get<std::string>() != "00000000-0000-0000-0000-000000000000";
  ImGui::Spacing();
  if (hasMat && ImGui::Button("Edit Material", ImVec2(-FLT_MIN, 0))) openMaterialEditor(a, mat.get<std::string>());
  if (on)
    note("The cook bakes the scene's lights and ambient occlusion into this mesh's vertex colors%s.",
         hasMat ? " and draws it with the unlit variant of its material" : "");
  else if (hasMat)
    note("Lit at run time by the strongest lights (at most as many as the target allows per object).");
  else
    note("No material: this mesh keeps the cooker's single baked light (DirectionalLight).");
}

}  // namespace

RENSGA_COMPONENT_INSPECTOR(b3_light, "Light", lightInspector)
RENSGA_COMPONENT_INSPECTOR(b3_camera, "Camera", cameraInspector)
RENSGA_COMPONENT_INSPECTOR(b3_audio, "AudioSource", audioInspector)
RENSGA_COMPONENT_INSPECTOR(b3_box, "BoxCollider", boxInspector)
RENSGA_COMPONENT_INSPECTOR(b3_sphere, "SphereCollider", sphereInspector)
RENSGA_COMPONENT_INSPECTOR(b3_capsule, "CapsuleCollider", capsuleInspector)
RENSGA_COMPONENT_INSPECTOR(b3_meshrenderer, "MeshRenderer", meshRendererInspector)

}  // namespace editor
}  // namespace rensga
