// Project and scene model for the editor (read-only in the MVP). No ImGui, no console names.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

struct SceneComponent {
  std::string type;
  nlohmann::json fields = nlohmann::json::object();
};

struct SceneObject {
  std::string guid, name, parentGuid;  // parentGuid empty = root
  bool active = true;
  float position[3] = {0, 0, 0};
  float rotation[4] = {0, 0, 0, 1};
  float scale[3] = {1, 1, 1};
  std::vector<SceneComponent> components;
};

struct Scene {
  std::string guid, path;
  std::vector<SceneObject> objects;
};

struct Project {
  std::filesystem::path root;
  std::string name, target;
  std::vector<Scene> scenes;

  // Loads ProjectSettings/Project.json and every Assets/Scenes/*.scene. Returns false and fills err.
  bool load(const std::filesystem::path& dir, std::string& err);
};

}  // namespace editor
}  // namespace rensga
