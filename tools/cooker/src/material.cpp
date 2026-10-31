// F5.14 (cook side): .mat.json Material assets -> MATL. Schema: docs/rensga-engine/pack-format.md ("Material asset").
#include <nlohmann/json.hpp>

#include "rensga/cooker/model3d.h"

namespace rensga {

bool parseMaterialJson(const std::string& text, MaterialDef& out, std::string& error) {
  using nlohmann::json;
  try {
    const json j = json::parse(text);
    if (!j.is_object()) throw std::runtime_error("the file is not a JSON object");
    if (j.value("format", 1) != 1) throw std::runtime_error("unsupported material format version");
    out = MaterialDef();
    if (j.contains("texture") && !j["texture"].is_null()) {
      if (!j["texture"].is_string()) throw std::runtime_error("\"texture\" must be an asset GUID string");
      out.textureGuid = j["texture"].get<std::string>();
      Guid g;
      if (!out.textureGuid.empty() && !Guid::parse(out.textureGuid.c_str(), g))
        throw std::runtime_error("\"texture\" is not a GUID: '" + out.textureGuid + "'");
    }
    if (j.contains("color")) {
      const json& c = j["color"];
      if (!c.is_array() || c.size() != 4) throw std::runtime_error("\"color\" must be [r, g, b, a] (0..255)");
      for (size_t i = 0; i < 4; ++i) {
        if (!c[i].is_number()) throw std::runtime_error("\"color\" entries must be numbers");
        const double v = c[i].get<double>();
        if (v < 0 || v > 255) throw std::runtime_error("\"color\" entries must be 0..255");
        out.color[i] = static_cast<uint8_t>(v + 0.5);
      }
    }
    out.unlit = j.value("unlit", false);
    out.alphaTest = j.value("alphaTest", false);
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

uint32_t materialFlags(const MaterialDef& m) { return (m.unlit ? kMatUnlit : 0u) | (m.alphaTest ? kMatAlphaTest : 0u); }

uint32_t colorAbgr(const uint8_t c[4]) {
  return static_cast<uint32_t>(c[0]) | (static_cast<uint32_t>(c[1]) << 8) | (static_cast<uint32_t>(c[2]) << 16) | (static_cast<uint32_t>(c[3]) << 24);
}

}  // namespace rensga
