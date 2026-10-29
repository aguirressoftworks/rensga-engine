// Scene editing actions (MVP-52). Every one is a single SceneDocument call (or a gesture), so one action = one undo step.
#include <cctype>

#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

std::string createEmptyObject(App& a, const std::string& parent) {
  if (!a.doc.loaded()) return "";
  std::string g = a.doc.createObject("GameObject", parent);
  if (!g.empty()) a.doc.select(g);
  return g;
}

std::string createPrimitiveObject(App& a, const std::string& shape, const std::string& parent) {
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

  float eye[3], fwd[3];
  a.sv.cam.eye(eye);
  a.sv.cam.forward(fwd);
  a.doc.beginGesture();  // create + place = one undo step
  std::string g = a.doc.createObject(name, parent, comps);
  if (!g.empty()) {
    double w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, eye[0] + fwd[0] * 5.0, eye[1] + fwd[1] * 5.0, eye[2] + fwd[2] * 5.0, 1};
    a.doc.setWorldMatrix(g, w);
    a.doc.select(g);
  }
  a.doc.endGesture();
  return g;
}

void duplicateSelected(App& a) {
  if (a.doc.selected().empty()) return;
  std::string g = a.doc.duplicate(a.doc.selected());
  if (!g.empty()) a.doc.select(g);
}

void deleteSelected(App& a) {
  if (!a.doc.selected().empty()) a.doc.remove(a.doc.selected());
}

void beginRename(App& a, const std::string& guid) {
  if (a.doc.find(guid)) { a.renameGuid = guid; a.renameFocus = true; }
}

void drawCreateMenuItems(App& a, const std::string& parent) {
  const bool ok = a.doc.loaded();
  if (ImGui::MenuItem(parent.empty() ? "Create Empty" : "Create Empty Child", nullptr, false, ok)) createEmptyObject(a, parent);
  if (ImGui::BeginMenu("3D Object", ok)) {
    static const char* const kShapes[][2] = {{"Cube", "cube"}, {"Sphere", "sphere"}, {"Capsule", "capsule"}, {"Plane", "plane"}, {"Ramp", "ramp"}};
    for (const auto& s : kShapes)
      if (ImGui::MenuItem(s[0], nullptr, false, !a.meshes.guidForShape(s[1]).empty())) createPrimitiveObject(a, s[1], parent);
    ImGui::EndMenu();
  }
}

}  // namespace editor
}  // namespace rensga
