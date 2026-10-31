// ProjectSettings schemas (F6 Editor 6, contract E0). Console-agnostic, no ImGui. Spec: docs/rensga-engine/project-settings.md.
//
//   ProjectSettings/TagManager.json   tags + 32 named layers
//   ProjectSettings/Physics.json      3D gravity + 32x32 collision matrix (Physics2D.json keeps its own file and format)
//   ProjectSettings/Time.json         fixed timestep, maximum delta, time scale
//   ProjectSettings/Project.json      name, target id, development flag, scenes in build (+ enabled flags)
//
// Every settings type has defaults(), toJson(), fromJson() (lenient: missing keys keep the default, wrong types are errors),
// validate() (human readable problems, empty = ok) and load/save helpers. A missing file loads as the defaults.
// Files are diff-friendly JSON (2-space indent, trailing newline). Unknown keys survive a load/save round trip (`extra`).
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

constexpr int kLayerCount = 32;

// ---- TagManager.json: {"format":1,"tags":["Enemy",...],"layers":["Default","TransparentFX",...32 entries...]} ----
struct TagManagerSettings {
  std::vector<std::string> tags;                 // user tags; "Untagged" is implicit (empty tag string on an object)
  std::array<std::string, kLayerCount> layers;   // index = layer number; "" = unnamed; layer 0 is always "Default"
  nlohmann::json extra = nlohmann::json::object();

  static TagManagerSettings defaults();
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  std::vector<std::string> validate() const;
  bool hasTag(const std::string& t) const;
  int layerIndex(const std::string& name) const;  // -1 when no layer has that name
};

// ---- Physics.json: {"format":1,"gravity":[0,-9.81,0],"ignore":[[2,3],...],"queriesHitTriggers":true} ----
// "ignore" lists layer-index pairs that do NOT collide; everything else collides (an empty list = all collide).
// The cooked form is PHY3 (pack-format.md): gravity + one 32-bit mask per layer from collisionMasks().
struct PhysicsSettings {
  float gravity[3] = {0.0f, -9.81f, 0.0f};
  std::vector<std::array<int, 2>> ignore;  // normalised: a <= b, sorted, unique
  bool queriesHitTriggers = true;
  nlohmann::json extra = nlohmann::json::object();

  static PhysicsSettings defaults();
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  std::vector<std::string> validate() const;
  bool collides(int a, int b) const;
  void setCollides(int a, int b, bool on);
  std::array<uint32_t, kLayerCount> collisionMasks() const;  // bit j of [i] set = layers i and j collide (symmetric)
};

// ---- Time.json: {"format":1,"fixedTimestep":0.016666668,"maximumDelta":0.1,"timeScale":1.0} ----
struct TimeSettings {
  double fixedTimestep = 1.0 / 60.0;  // seconds of one FixedUpdate / physics step; the engine is a fixed-step player
  double maximumDelta = 0.1;          // longest frame time fed to the accumulator (stops the spiral of death)
  double timeScale = 1.0;
  nlohmann::json extra = nlohmann::json::object();

  static TimeSettings defaults();
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  std::vector<std::string> validate() const;
};

// ---- Project.json build settings ----
// {"format":1,"name":"..","target":"<id>","development":false,
//  "scenes":["<guid>",...],                               <- ENABLED scenes in build order; what the cooker and the pipeline read
//  "buildScenes":[{"guid":"<guid>","enabled":true},...]}  <- the editor's full list (disabled entries kept); optional on load
// save() always rewrites "scenes" from "buildScenes", so a file written by an older editor (only "scenes") and a file
// written by this one are both read by the cooker unchanged.
struct BuildScene {
  std::string guid;
  bool enabled = true;
};
struct ProjectBuildSettings {
  std::string name;
  std::string target;       // ConsoleTarget id; the editor core only stores it
  bool development = false; // development build: profiler hooks, logging, no release packaging checks
  std::vector<BuildScene> scenes;
  nlohmann::json extra = nlohmann::json::object();  // every other key of Project.json, kept verbatim

  static ProjectBuildSettings defaults();
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  std::vector<std::string> validate() const;
  std::vector<std::string> enabledScenes() const;
  bool addScene(const std::string& guid, bool enabled = true);  // false when already listed
  bool removeScene(const std::string& guid);
  bool setSceneEnabled(const std::string& guid, bool enabled);
  bool moveScene(int from, int to);
};

// File helpers: <projectRoot>/ProjectSettings/<file>. load() returns true for a missing file (defaults) and false with
// `err` for unreadable or invalid JSON; save() creates ProjectSettings/ and writes atomically (temp file + rename).
bool loadTagManager(const std::filesystem::path& projectRoot, TagManagerSettings& out, std::string& err);
bool saveTagManager(const std::filesystem::path& projectRoot, const TagManagerSettings& s, std::string& err);
bool loadPhysics(const std::filesystem::path& projectRoot, PhysicsSettings& out, std::string& err);
bool savePhysics(const std::filesystem::path& projectRoot, const PhysicsSettings& s, std::string& err);
bool loadTime(const std::filesystem::path& projectRoot, TimeSettings& out, std::string& err);
bool saveTime(const std::filesystem::path& projectRoot, const TimeSettings& s, std::string& err);
bool loadProjectBuild(const std::filesystem::path& projectRoot, ProjectBuildSettings& out, std::string& err);
bool saveProjectBuild(const std::filesystem::path& projectRoot, const ProjectBuildSettings& s, std::string& err);

}  // namespace editor
}  // namespace rensga
