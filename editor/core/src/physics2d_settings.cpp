#include "rensga/editor/physics2d_settings.h"

#include <algorithm>
#include <fstream>

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

int Physics2DSettings::layerIndex(const std::string& name) const {
  for (size_t i = 0; i < layers.size(); ++i) if (layers[i] == name) return static_cast<int>(i);
  return -1;
}

bool Physics2DSettings::collides(int a, int b) const {
  if (a < 0 || b < 0 || a >= static_cast<int>(layers.size()) || b >= static_cast<int>(layers.size())) return true;
  for (const auto& p : ignore)
    if ((p.first == layers[static_cast<size_t>(a)] && p.second == layers[static_cast<size_t>(b)]) ||
        (p.first == layers[static_cast<size_t>(b)] && p.second == layers[static_cast<size_t>(a)])) return false;
  return true;
}

void Physics2DSettings::setCollides(int a, int b, bool on) {
  if (a < 0 || b < 0 || a >= static_cast<int>(layers.size()) || b >= static_cast<int>(layers.size())) return;
  const std::string& na = layers[static_cast<size_t>(a)];
  const std::string& nb = layers[static_cast<size_t>(b)];
  for (size_t i = 0; i < ignore.size();)
    if ((ignore[i].first == na && ignore[i].second == nb) || (ignore[i].first == nb && ignore[i].second == na)) ignore.erase(ignore.begin() + static_cast<long>(i));
    else ++i;
  if (!on) ignore.push_back(a <= b ? std::make_pair(na, nb) : std::make_pair(nb, na));
}

json Physics2DSettings::toJson() const {
  json j = extra.is_object() ? extra : json::object();
  j["format"] = 1;
  j["layers"] = layers;
  json ig = json::array();
  for (const auto& p : ignore) ig.push_back(json::array({p.first, p.second}));
  j["ignore"] = ig;
  return j;
}

bool Physics2DSettings::fromJson(const json& j, std::string& err) {
  if (!j.is_object()) { err = "Physics2D.json must be an object"; return false; }
  Physics2DSettings s;
  s.layers.clear();
  if (j.contains("layers")) {
    if (!j["layers"].is_array()) { err = "'layers' must be an array of names"; return false; }
    for (const json& l : j["layers"]) { if (!l.is_string()) { err = "'layers' must hold strings"; return false; } s.layers.push_back(l.get<std::string>()); }
  }
  if (s.layers.empty()) s.layers.push_back("Default");
  if (j.contains("ignore")) {
    if (!j["ignore"].is_array()) { err = "'ignore' must be an array of [layer, layer] pairs"; return false; }
    for (const json& p : j["ignore"]) {
      if (!p.is_array() || p.size() != 2 || !p[0].is_string() || !p[1].is_string()) { err = "every 'ignore' entry must be two layer names"; return false; }
      s.ignore.emplace_back(p[0].get<std::string>(), p[1].get<std::string>());
    }
  }
  for (auto it = j.begin(); it != j.end(); ++it)
    if (it.key() != "format" && it.key() != "layers" && it.key() != "ignore") s.extra[it.key()] = it.value();
  *this = std::move(s);
  return true;
}

std::vector<std::string> Physics2DSettings::validate() const {
  std::vector<std::string> out;
  if (layers.size() > 32) out.push_back("at most 32 layers");
  for (size_t i = 0; i < layers.size(); ++i) {
    if (layers[i].empty()) out.push_back("layer " + std::to_string(i) + " has no name");
    for (size_t k = i + 1; k < layers.size(); ++k) if (layers[i] == layers[k]) out.push_back("layer name '" + layers[i] + "' is used twice");
  }
  for (const auto& p : ignore)
    if (layerIndex(p.first) < 0 || layerIndex(p.second) < 0) out.push_back("ignore pair [" + p.first + ", " + p.second + "] names an unknown layer");
  return out;
}

bool loadPhysics2D(const fs::path& root, Physics2DSettings& out, std::string& err) {
  const fs::path f = root / "ProjectSettings" / "Physics2D.json";
  std::error_code ec;
  if (!fs::exists(f, ec)) { out = Physics2DSettings(); return true; }
  std::ifstream in(f);
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded()) { err = f.string() + ": not valid JSON"; return false; }
  return out.fromJson(j, err);
}

bool savePhysics2D(const fs::path& root, const Physics2DSettings& s, std::string& err) {
  const fs::path dir = root / "ProjectSettings", f = dir / "Physics2D.json", tmp = dir / "Physics2D.json.tmp";
  std::error_code ec;
  fs::create_directories(dir, ec);
  {
    std::ofstream o(tmp, std::ios::binary);
    if (!o) { err = "cannot write " + tmp.string(); return false; }
    o << s.toJson().dump(2) << "\n";
  }
  fs::rename(tmp, f, ec);
  if (ec) { err = ec.message(); return false; }
  return true;
}

}  // namespace editor
}  // namespace rensga
