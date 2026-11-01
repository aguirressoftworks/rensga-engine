// Scene editing actions (MVP-52). Every one is a single SceneDocument call (or a gesture), so one action = one undo step.
#include <cctype>
#include <cmath>

#include <algorithm>

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

// F6.02: GameObject > Light creates the F5 Light (Directional or Point). A project whose ConsoleTarget does not allow `Light` keeps the
// old DirectionalLight component, which every project can draw.
static bool typeAllowed(App& a, const char* type) {
  const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
  return ct.allowedComponents.empty() || std::find(ct.allowedComponents.begin(), ct.allowedComponents.end(), type) != ct.allowedComponents.end();
}

// The object's world rotation: `deg` about x (a directional light shines along +z, so +50 tilts it down); position as placed.
static void tiltAboutX(App& a, const std::string& guid, float deg) {
  double w[16];
  a.doc.worldMatrix(guid, w);
  const double r = deg * 3.14159265358979 / 180.0, c = std::cos(r), s = std::sin(r);
  w[0] = 1; w[1] = 0; w[2] = 0;
  w[4] = 0; w[5] = c; w[6] = s;
  w[8] = 0; w[9] = -s; w[10] = c;
  a.doc.setWorldMatrix(guid, w);
}

std::string createLightObject(App& a, const std::string& parent, const float* at) {
  if (!typeAllowed(a, "Light")) return createComponentObject(a, "Light", "DirectionalLight", parent, at);
  if (!a.doc.loaded()) return "";
  a.doc.beginGesture();
  std::string g = createComponentObject(a, "Directional Light", "Light", parent, at);
  if (!g.empty()) tiltAboutX(a, g, 50.0f);
  a.doc.endGesture();
  return g;
}

std::string createPointLightObject(App& a, const std::string& parent, const float* at) {
  if (!typeAllowed(a, "Light")) {
    EditorLog::instance().warn("Light is not an allowed component of this project's ConsoleTarget");
    return "";
  }
  a.doc.beginGesture();
  std::string g = createComponentObject(a, "Point Light", "Light", parent, at);
  if (!g.empty()) a.doc.setField(g, 0, "type", 1);
  a.doc.endGesture();
  return g;
}

std::string createAudioSourceObject(App& a, const std::string& parent, const float* at) {
  return createComponentObject(a, "Audio Source", "AudioSource", parent, at);
}

std::string createPrimitiveObject(App& a, const std::string& shape, const std::string& parent, const float* at) {
  if (!a.doc.loaded()) return "";
  // F6.02: the non-mesh 3D objects share this entry point (the --ui-event verb create:<shape> and the menus)
  if (shape == "camera") return createCameraObject(a, parent, at);
  if (shape == "light") return createLightObject(a, parent, at);
  if (shape == "pointlight") return createPointLightObject(a, parent, at);
  if (shape == "audio" || shape == "audiosource") return createAudioSourceObject(a, parent, at);
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
  if (shape == "capsule" && typeAllowed(a, "CapsuleCollider") && TypeRegistry::instance().find("CapsuleCollider")) {
    col.type = "CapsuleCollider";
    col.fields = json::object({{"center", json::array({0, 0, 0})}, {"radius", 0.5}, {"height", 2.0}, {"direction", 1}, {"isTrigger", false}});
  } else if (shape == "sphere" || shape == "capsule") {
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
    ImGui::Separator();
    if (ImGui::MenuItem("Camera", nullptr, false, ok)) createCameraObject(a, parent, at);
    if (ImGui::MenuItem("Audio Source", nullptr, false, ok)) createAudioSourceObject(a, parent, at);
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Camera", nullptr, false, ok)) createCameraObject(a, parent, at);
  if (ImGui::BeginMenu("Light", ok)) {
    if (ImGui::MenuItem("Directional Light", nullptr, false, ok)) createLightObject(a, parent, at);
    if (ImGui::MenuItem("Point Light", nullptr, false, ok && typeAllowed(a, "Light"))) createPointLightObject(a, parent, at);
    ImGui::EndMenu();
  }
}

}  // namespace editor
}  // namespace rensga
