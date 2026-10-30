// Scene editing actions (MVP-52). Every one is a single SceneDocument call (or a gesture), so one action = one undo step.
#include <cctype>
#include <cmath>

#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

// World position of a new object: where the caller says, else 5 units in front of the Scene camera.
static void placeAt(App& a, const std::string& guid, const float* at) {
  float eye[3], fwd[3];
  a.sv.cam.eye(eye);
  a.sv.cam.forward(fwd);
  float p[3] = {at ? at[0] : eye[0] + fwd[0] * 5.0f, at ? at[1] : eye[1] + fwd[1] * 5.0f, at ? at[2] : eye[2] + fwd[2] * 5.0f};
  if (a.sv.mode2d && !at) { p[0] = std::round(a.sv.cam2.center[0]); p[1] = std::round(a.sv.cam2.center[1]); p[2] = 0; }  // 2D: view centre
  double w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, p[0], p[1], p[2], 1};
  a.doc.setWorldMatrix(guid, w);
}

std::string createEmptyObject(App& a, const std::string& parent, const float* at) {
  if (!a.doc.loaded()) return "";
  a.doc.beginGesture();
  std::string g = a.doc.createObject("GameObject", parent);
  if (!g.empty()) {
    if (at) placeAt(a, g, at);
    a.doc.select(g);
  }
  a.doc.endGesture();
  return g;
}

static std::string createComponentObject(App& a, const char* name, const char* type, const std::string& parent, const float* at) {
  if (!a.doc.loaded()) return "";
  const TypeInfo* ti = TypeRegistry::instance().find(type);
  if (!ti) {
    EditorLog::instance().warn(std::string("component ") + type + " is not registered in this build");
    return "";
  }
  SceneComponent c;
  c.type = type;
  c.fields = defaultFieldsFor(*ti);
  a.doc.beginGesture();
  std::string g = a.doc.createObject(name, parent, {c});
  if (!g.empty()) {
    placeAt(a, g, at);
    a.doc.select(g);
  }
  a.doc.endGesture();
  return g;
}

std::string createCameraObject(App& a, const std::string& parent, const float* at) {
  return createComponentObject(a, "Camera", "Camera", parent, at);
}

std::string createLightObject(App& a, const std::string& parent, const float* at) {
  return createComponentObject(a, "Light", "DirectionalLight", parent, at);
}

std::string createPrimitiveObject(App& a, const std::string& shape, const std::string& parent, const float* at) {
  if (!a.doc.loaded()) return "";
  std::string mesh = a.meshes.guidForShape(shape);
  if (mesh.empty()) {
    EditorLog::instance().warn("no '" + shape + "' primitive asset in this project (Assets/**/*.prim.json)");
    return "";
  }
  using nlohmann::json;
  std::vector<SceneComponent> comps;
  SceneComponent mr;
  mr.type = "MeshRenderer";
  mr.fields = json::object({{"mesh", mesh}, {"color", json::array({200, 200, 200, 255})}});
  comps.push_back(mr);
  SceneComponent col;
  if (shape == "sphere" || shape == "capsule") {
    col.type = "SphereCollider";
    col.fields = json::object({{"radius", 0.5}, {"isTrigger", false}});
  } else {
    col.type = "BoxCollider";
    if (shape == "plane") col.fields = json::object({{"center", json::array({0, -0.5, 0})}, {"size", json::array({10, 1, 10})}, {"isTrigger", false}});
    else if (shape == "ramp") col.fields = json::object({{"center", json::array({0, 0, 0})}, {"size", json::array({1, 1, 2})}, {"isTrigger", false}});
    else col.fields = json::object({{"center", json::array({0, 0, 0})}, {"size", json::array({1, 1, 1})}, {"isTrigger", false}});
  }
  comps.push_back(col);
  std::string name = shape;
  name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));

  a.doc.beginGesture();  // create + place = one undo step
  std::string g = a.doc.createObject(name, parent, comps);
  if (!g.empty()) {
    placeAt(a, g, at);
    a.doc.select(g);
  }
  a.doc.endGesture();
  return g;
}

// Duplicate, delete, copy and paste act on the whole selection and are one undo step each.
void duplicateSelected(App& a) {
  if (!a.doc.selection().empty()) a.doc.duplicateMany(a.doc.selection());
}

void deleteSelected(App& a) {
  if (!a.doc.selection().empty()) a.doc.removeMany(a.doc.selection());
}

static std::string g_objectClipboard;  // also kept in the system clipboard when there is one

void copySelected(App& a) {
  if (a.doc.selection().empty()) return;
  g_objectClipboard = a.doc.copyObjects(a.doc.selection());
  ImGui::SetClipboardText(g_objectClipboard.c_str());
}

void pasteSelected(App& a) {
  std::string text = g_objectClipboard;
  if (const char* sys = ImGui::GetClipboardText()) {
    if (std::string(sys).find("rensga_clipboard") != std::string::npos) text = sys;
  }
  if (!text.empty()) a.doc.pasteObjects(text, "");
}

void selectAllObjects(App& a) {
  std::vector<std::string> all;
  for (const SceneObject& o : a.doc.scene().objects) all.push_back(o.guid);
  a.doc.setSelection(all);
}

void beginRename(App& a, const std::string& guid) {
  if (a.doc.find(guid)) { a.renameGuid = guid; a.renameFocus = true; }
}

void drawCreateMenuItems(App& a, const std::string& parent, const float* at) {
  const bool ok = a.doc.loaded();
  if (ImGui::MenuItem(parent.empty() ? "Create Empty" : "Create Empty Child", nullptr, false, ok)) createEmptyObject(a, parent, at);
  if (ImGui::BeginMenu("3D Object", ok)) {
    static const char* const kShapes[][2] = {{"Cube", "cube"}, {"Sphere", "sphere"}, {"Capsule", "capsule"}, {"Plane", "plane"}, {"Ramp", "ramp"}};
    for (const auto& s : kShapes)
      if (ImGui::MenuItem(s[0], nullptr, false, !a.meshes.guidForShape(s[1]).empty())) createPrimitiveObject(a, s[1], parent, at);
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Camera", nullptr, false, ok)) createCameraObject(a, parent, at);
  if (ImGui::MenuItem("Light", nullptr, false, ok)) createLightObject(a, parent, at);
}

}  // namespace editor
}  // namespace rensga
