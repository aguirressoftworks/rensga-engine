#include "rensga/editor/hub.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

static bool readJson(const fs::path& p, json& out) {
  std::ifstream f(p);
  if (!f) return false;
  out = json::parse(f, nullptr, false);
  return !out.is_discarded();
}

fs::path HubSettings::resolveHome(const std::string& o) {
  if (!o.empty()) return fs::path(o);
  if (const char* e = std::getenv("RENSGA_HOME"); e && *e) return fs::path(e);
  if (const char* h = std::getenv("HOME"); h && *h) return fs::path(h);
  return fs::path(".");
}

void HubSettings::load(const fs::path& h) {
  home = h;
  projectsPath = home / "RensgaProjects";
  lastOpened.clear();
  json j;
  if (readJson(file(), j) && j.is_object()) {
    std::string p = j.value("projectsPath", "");
    if (!p.empty()) projectsPath = p;
    if (j.contains("lastOpened") && j["lastOpened"].is_object())
      for (auto it = j["lastOpened"].begin(); it != j["lastOpened"].end(); ++it)
        if (it.value().is_number_integer()) lastOpened[it.key()] = it.value().get<long long>();
  }
  std::error_code ec;
  fs::create_directories(projectsPath, ec);
}

bool HubSettings::save() const {
  std::error_code ec;
  fs::create_directories(file().parent_path(), ec);
  json j;
  j["format"] = 1;
  j["projectsPath"] = projectsPath.string();
  j["lastOpened"] = json::object();
  for (const auto& kv : lastOpened) j["lastOpened"][kv.first] = kv.second;
  std::ofstream f(file());
  if (!f) return false;
  f << j.dump(2) << "\n";
  return static_cast<bool>(f);
}

std::vector<ProjectEntry> listProjects(const HubSettings& s) {
  std::vector<ProjectEntry> out;
  std::error_code ec;
  for (fs::directory_iterator it(s.projectsPath, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    json pj;
    if (!readJson(it->path() / "ProjectSettings" / "Project.json", pj)) continue;
    ProjectEntry e;
    e.dir = it->path();
    e.name = pj.value("name", it->path().filename().string());
    e.buildTarget = pj.value("buildTarget", "console");
    e.profile = pj.value("target", "");
    json ct;
    if (readJson(it->path() / "ProjectSettings" / "ConsoleTarget.json", ct)) e.profile = ct.value("profile", e.profile);
    auto lo = s.lastOpened.find(e.dir.string());
    if (lo != s.lastOpened.end()) e.lastOpened = lo->second;
    out.push_back(std::move(e));
  }
  std::sort(out.begin(), out.end(), [](const ProjectEntry& a, const ProjectEntry& b) {
    return a.lastOpened != b.lastOpened ? a.lastOpened > b.lastOpened : a.name < b.name;
  });
  return out;
}

bool HubData::load(const fs::path& dataFile, const fs::path& tdir) {
  templatesDir = tdir;
  apis.clear();
  buildTargets.clear();
  json j;
  if (!readJson(dataFile, j)) return false;
  for (const json& a : j.value("apis", json::array())) {
    HubApi x;
    x.id = a.value("id", "");
    x.available = a.value("available", false);
    std::error_code ec;
    x.hasTemplate = fs::exists(tdir / x.id / "ProjectSettings" / "Project.json", ec);
    if (!x.id.empty()) apis.push_back(std::move(x));
  }
  for (const json& b : j.value("buildTargets", json::array())) {
    HubBuildTarget x;
    x.id = b.value("id", "");
    x.label = b.value("label", x.id);
    x.available = b.value("available", false);
    if (!x.id.empty()) buildTargets.push_back(std::move(x));
  }
  return !apis.empty() && !buildTargets.empty();
}

std::string validateProjectName(const std::string& name, const fs::path& projectsPath) {
  if (name.empty()) return "Enter a project name.";
  if (name.size() > 64) return "The name is too long (64 characters at most).";
  if (name.front() == '.' || name.front() == ' ' || name.back() == ' ' || name.back() == '.')
    return "The name cannot start or end with a dot or a space.";
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ' ';
    if (!ok) return "Use letters, digits, space, '-', '_' and '.' only.";
  }
  std::error_code ec;
  if (fs::exists(projectsPath / name, ec)) return "A folder with this name already exists.";
  return "";
}

bool createProject(const HubData& data, const HubSettings& s, const std::string& name, const std::string& api,
                   const std::string& buildTarget, fs::path& outDir, std::string& err) {
  err = validateProjectName(name, s.projectsPath);
  if (!err.empty()) return false;
  const fs::path src = data.templatesDir / api, dst = s.projectsPath / name;
  std::error_code ec;
  if (!fs::exists(src / "ProjectSettings" / "Project.json", ec)) { err = "no template for " + api; return false; }
  fs::create_directories(dst, ec);
  for (fs::recursive_directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) {
    fs::path rel = fs::relative(it->path(), src);
    if (*rel.begin() == "Library") { if (it->is_directory()) it.disable_recursion_pending(); continue; }
    std::error_code e2;
    if (it->is_directory()) fs::create_directories(dst / rel, e2);
    else fs::copy_file(it->path(), dst / rel, fs::copy_options::overwrite_existing, e2);
    if (e2) { err = "copy failed: " + e2.message(); return false; }
  }
  if (ec) { err = "copy failed: " + ec.message(); return false; }
  json pj;
  if (!readJson(dst / "ProjectSettings" / "Project.json", pj)) { err = "template Project.json is invalid"; return false; }
  pj["name"] = name;
  pj["target"] = api;
  pj["buildTarget"] = buildTarget;
  { std::ofstream f(dst / "ProjectSettings" / "Project.json"); f << pj.dump(2) << "\n"; if (!f) { err = "cannot write Project.json"; return false; } }
  { std::ofstream f(dst / ".gitignore"); f << "Library/\n"; }
  fs::create_directories(dst / "Library", ec);
  outDir = dst;
  return true;
}

}  // namespace editor
}  // namespace rensga
