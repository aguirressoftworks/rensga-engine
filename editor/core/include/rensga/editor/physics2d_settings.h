// F6.04 Editor 10: ProjectSettings/Physics2D.json (2D layer names and the layers that do not collide, by name) as an editable
// document. Format: {"format":1,"layers":["Default","Player",...],"ignore":[["Enemy","Pickup"],...]}. The cooker reads the same file
// (PH2D section). A missing file means one layer "Default" and everything collides. No ImGui.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

struct Physics2DSettings {
  std::vector<std::string> layers = {"Default"};
  std::vector<std::pair<std::string, std::string>> ignore;  // normalised: first <= second by layer order, unique
  nlohmann::json extra = nlohmann::json::object();

  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  int layerIndex(const std::string& name) const;
  bool collides(int a, int b) const;
  void setCollides(int a, int b, bool on);
  std::vector<std::string> validate() const;
};

bool loadPhysics2D(const std::filesystem::path& projectRoot, Physics2DSettings& out, std::string& err);
bool savePhysics2D(const std::filesystem::path& projectRoot, const Physics2DSettings& s, std::string& err);

}  // namespace editor
}  // namespace rensga
