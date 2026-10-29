#include "rensga/editor/project.h"

#include <algorithm>
#include <fstream>

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

static bool readJson(const fs::path& p, json& out, std::string& err) {
  std::ifstream f(p);
  if (!f) { err = "cannot open " + p.string(); return false; }
  out = json::parse(f, nullptr, false);
  if (out.is_discarded()) { err = "invalid JSON in " + p.string(); return false; }
  return true;
}

template <size_t N>
static void readArr(const json& j, const char* key, float (&dst)[N]) {
  if (!j.contains(key) || !j[key].is_array()) return;
  for (size_t i = 0; i < N && i < j[key].size(); ++i) dst[i] = j[key][i].get<float>();
}

Scene parseScene(const json& sj) {
  Scene s;
  s.guid = sj.value("guid", "");
  for (const json& oj : sj.value("objects", json::array())) {
    SceneObject o;
    o.guid = oj.value("guid", "");
    o.name = oj.value("name", "");
    if (oj.contains("parent") && oj["parent"].is_string()) o.parentGuid = oj["parent"].get<std::string>();
    o.active = oj.value("active", true);
    if (oj.contains("transform")) {
      readArr(oj["transform"], "position", o.position);
      readArr(oj["transform"], "rotation", o.rotation);
      readArr(oj["transform"], "scale", o.scale);
    }
    for (const json& cj : oj.value("components", json::array())) {
      SceneComponent c;
      c.type = cj.value("type", "");
      if (cj.contains("fields")) c.fields = cj["fields"];
      o.components.push_back(std::move(c));
    }
    s.objects.push_back(std::move(o));
  }
  return s;
}

bool Project::load(const fs::path& dir, std::string& err) {
  root = dir;
  json pj;
  if (!readJson(dir / "ProjectSettings" / "Project.json", pj, err)) return false;
  name = pj.value("name", "");
  target = pj.value("target", "");
  scenes.clear();
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir / "Assets", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".scene") files.push_back(it->path());
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) {
    json sj;
    if (!readJson(f, sj, err)) return false;
    Scene s = parseScene(sj);
    s.path = f.string();
    scenes.push_back(std::move(s));
  }
  return true;
}

}  // namespace editor
}  // namespace rensga
