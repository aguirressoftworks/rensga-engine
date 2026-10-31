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
// One project template (F4.09): templates/<dir>. `available` comes from hub.json, `present` from the folder on disk.
// A card is selectable only when both are true; otherwise the Create Project dialog greys it out with "Coming in <comingIn>".
struct HubTemplate {
  std::string api;                 // target API id, from hub.json
  std::string kind;                // "2d" or "3d"
  std::string dir;                 // folder under templates/
  std::string title;               // "2D" / "3D"
  std::string description;         // one line for the card
  std::string note;                // extra line when the template has a limit (shown under the cards)
  std::string comingIn;            // roadmap phase that delivers it, e.g. "PSX Template 5"
  bool available = false, present = false;
  bool selectable() const { return available && present; }
};
struct HubData {
  std::vector<HubApi> apis;
  std::vector<HubBuildTarget> buildTargets;
  std::vector<HubTemplate> templates;   // hub.json order
  std::filesystem::path templatesDir;
  bool load(const std::filesystem::path& dataFile, const std::filesystem::path& templatesDir);
  bool apiSelectable(size_t i) const { return apis[i].available && apis[i].hasTemplate; }
  // Index into `templates` for an API and kind ("2d"|"3d"), or -1.
  int findTemplate(const std::string& api, const std::string& kind) const;
  // Preferred template for an API: "3d" when selectable, else the first selectable one, else -1.
  int defaultTemplate(const std::string& api) const;
  // One line per template (`hub-dump`): "template: <dir> api=<id> kind=2d state=available|disabled reason=...".
  std::vector<std::string> dumpTemplates() const;
};

// "" when the name is usable as a new project folder under projectsPath, else the reason.
std::string validateProjectName(const std::string& name, const std::filesystem::path& projectsPath);
// Copies templates/<tmpl.dir> (without Library/ and thumbnail.png), sets Project.json name/target/buildTarget, writes .gitignore.
// Refuses a template that is not selectable ("Coming in <phase>") and leaves nothing on disk.
bool createProject(const HubData& data, const HubSettings& s, const std::string& name, const HubTemplate& tmpl,
                   const std::string& buildTarget, std::filesystem::path& outDir, std::string& err);
// Same, for the preferred template of an API (3D when it exists).
bool createProject(const HubData& data, const HubSettings& s, const std::string& name, const std::string& api,
                   const std::string& buildTarget, std::filesystem::path& outDir, std::string& err);

}  // namespace editor
}  // namespace rensga
