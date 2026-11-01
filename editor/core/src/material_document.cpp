#include "rensga/editor/material_document.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::ordered_json;

namespace rensga {
namespace editor {

MaterialRules materialRulesFor(const ConsoleTargetData& ct) {
  MaterialRules r;
  r.maxTextureSize = ct.maxTextureSize;
  if (!ct.features.empty()) {
    r.lighting = ct.hasFeature("lighting");
    r.fog = ct.hasFeature("fog");
  }
  if (!r.lighting) r.noLightingReason = "This target has no lighting: every material is drawn unlit (ConsoleTarget features).";
  if (!r.fog) r.noFogReason = "This target does not draw fog (ConsoleTarget features).";
  return r;
}

bool MaterialDocument::load(const fs::path& file, std::string& err) {
  std::ifstream in(file, std::ios::binary);
  if (!in) { err = "cannot open " + file.string(); return false; }
  std::stringstream ss;
  ss << in.rdbuf();
  try {
    ordered_json j = ordered_json::parse(ss.str());
    if (!j.is_object()) { err = "the file is not a JSON object"; return false; }
    MaterialData d;
    if (j.contains("texture") && j["texture"].is_string()) d.texture = j["texture"].get<std::string>();
    if (j.contains("color")) {
      const auto& c = j["color"];
      if (!c.is_array() || c.size() != 4) { err = "\"color\" must be [r, g, b, a] (0..255)"; return false; }
      for (size_t i = 0; i < 4; ++i) d.color[i] = std::max(0, std::min(255, static_cast<int>(c[i].get<double>() + 0.5)));
    }
    d.unlit = j.value("unlit", false);
    d.alphaTest = j.value("alphaTest", false);
    raw_ = std::move(j);
    data_ = saved_ = d;
  } catch (const std::exception& e) {
    err = e.what();
    return false;
  }
  path_ = file;
  undo_.clear();
  redo_.clear();
  return true;
}

bool MaterialDocument::save(std::string& err) {
  raw_["format"] = 1;
  if (data_.texture.empty()) raw_["texture"] = nullptr; else raw_["texture"] = data_.texture;
  raw_["color"] = ordered_json::array({data_.color[0], data_.color[1], data_.color[2], data_.color[3]});
  raw_["unlit"] = data_.unlit;
  raw_["alphaTest"] = data_.alphaTest;
  const fs::path tmp = path_.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) { err = "cannot write " + tmp.string(); return false; }
    out << raw_.dump(1) << "\n";
  }
  std::error_code ec;
  fs::rename(tmp, path_, ec);
  if (ec) { err = ec.message(); return false; }
  saved_ = data_;
  return true;
}

void MaterialDocument::push() {
  undo_.push_back(data_);
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  redo_.clear();
}

bool MaterialDocument::setTexture(const std::string& guid) {
  if (guid == data_.texture) return true;
  push();
  data_.texture = guid;
  return true;
}
bool MaterialDocument::setColor(int r, int g, int b) {
  r = std::max(0, std::min(255, r)); g = std::max(0, std::min(255, g)); b = std::max(0, std::min(255, b));
  if (r == data_.color[0] && g == data_.color[1] && b == data_.color[2]) return true;
  push();
  data_.color[0] = r; data_.color[1] = g; data_.color[2] = b;
  return true;
}
bool MaterialDocument::setBlend(BlendMode m, const MaterialRules& rules) {
  if (m == data_.blend()) return true;
  if (m == BlendMode::Alpha && !rules.blend) return false;
  push();
  data_.color[3] = m == BlendMode::Opaque ? 255 : 160;
  return true;
}
bool MaterialDocument::setAlpha(int a, const MaterialRules& rules) {
  a = std::max(0, std::min(255, a));
  if (a == data_.color[3]) return true;
  if (a < 255 && !rules.blend) return false;
  push();
  data_.color[3] = a;
  return true;
}
bool MaterialDocument::setUnlit(bool on, const MaterialRules& rules) {
  if (on == data_.unlit) return true;
  if (!on && !rules.lighting) return false;  // no lighting: always unlit
  push();
  data_.unlit = on;
  return true;
}
bool MaterialDocument::setAlphaTest(bool on) {
  if (on == data_.alphaTest) return true;
  push();
  data_.alphaTest = on;
  return true;
}
bool MaterialDocument::normalize(const MaterialRules& rules) {
  bool changed = false;
  if (!rules.lighting && !data_.unlit) { data_.unlit = true; changed = true; }
  if (!rules.blend && data_.color[3] != 255) { data_.color[3] = 255; changed = true; }
  return changed;
}
bool MaterialDocument::undo() {
  if (undo_.empty()) return false;
  redo_.push_back(data_);
  data_ = undo_.back();
  undo_.pop_back();
  return true;
}
bool MaterialDocument::redo() {
  if (redo_.empty()) return false;
  undo_.push_back(data_);
  data_ = redo_.back();
  redo_.pop_back();
  return true;
}

}  // namespace editor
}  // namespace rensga
