// Project hub model (F1.03): user settings, project discovery, templates, project creation.
// No ImGui and no console names: APIs and build targets come from templates/* and editor/data/hub.json.
#pragma once
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

struct HubSettings {
  std::filesystem::path home;           // RENSGA_HOME (or the user's home): settings and the default projects path live under it
  std::filesystem::path projectsPath;   // where the picker looks
  std::map<std::string, long long> lastOpened;  // project dir -> unix time
  std::map<std::string, std::string> modulePaths;  // manual build support modules: module id -> folder the user located

  std::filesystem::path file() const { return home / ".config" / "rensga" / "settings.json"; }
  static std::filesystem::path resolveHome(const std::string& override_);  // override, else $RENSGA_HOME, else $HOME
  void load(const std::filesystem::path& home);   // defaults when the file is missing; creates projectsPath
  bool save() const;
};

struct ProjectEntry {
  std::string name, profile, buildTarget;   // profile = ConsoleTarget "profile"
  std::filesystem::path dir;
  long long lastOpened = 0;
};
std::vector<ProjectEntry> listProjects(const HubSettings& s);  // most recently opened first, then by name

struct HubApi { std::string id; bool available = false; bool hasTemplate = false; };
struct HubBuildTarget { std::string id, label; bool available = false; };
struct HubData {
  std::vector<HubApi> apis;
  std::vector<HubBuildTarget> buildTargets;
  std::filesystem::path templatesDir;
  bool load(const std::filesystem::path& dataFile, const std::filesystem::path& templatesDir);
  bool apiSelectable(size_t i) const { return apis[i].available && apis[i].hasTemplate; }
};

// "" when the name is usable as a new project folder under projectsPath, else the reason.
std::string validateProjectName(const std::string& name, const std::filesystem::path& projectsPath);
// Copies templates/<api> (without Library/), sets Project.json name/target/buildTarget, writes .gitignore.
bool createProject(const HubData& data, const HubSettings& s, const std::string& name, const std::string& api,
                   const std::string& buildTarget, std::filesystem::path& outDir, std::string& err);

}  // namespace editor
}  // namespace rensga
