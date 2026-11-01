#include "physics_cook.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace rensga {
namespace cooker {

using nlohmann::json;

bool cookPhysics3D(const std::string& physicsJson, const std::string& tagManagerJson, PackPhysics3D& out, std::string& error) {
  out = PackPhysics3D();
  out.gravity[0] = 0.0f;
  out.gravity[1] = -9.81f;
  out.gravity[2] = 0.0f;
  out.flags = kPhy3QueriesHitTriggers;
  for (u32& m : out.masks) m = 0xFFFFFFFFu;
  out.bounceThreshold = 2.0f;
  out.sleepThreshold = 0.05f;
  try {
    std::vector<std::string> names;
    if (!tagManagerJson.empty()) {
      const json t = json::parse(tagManagerJson);
      for (const json& n : t.value("layers", json::array())) names.push_back(n.is_string() ? n.get<std::string>() : std::string());
    }
    const json j = json::parse(physicsJson);
    if (j.value("format", 0) != 1) { error = "unsupported format version"; return false; }
    if (j.contains("gravity")) {
      const json& g = j["gravity"];
      if (!g.is_array() || g.size() != 3) { error = "\"gravity\" must be [x, y, z]"; return false; }
      for (size_t i = 0; i < 3; ++i) {
        if (!g[i].is_number()) { error = "\"gravity\" must be [x, y, z]"; return false; }
        const float v = g[i].get<float>();
        if (!std::isfinite(v) || std::fabs(v) > 1000.0f) { error = "gravity must be finite and within +-1000"; return false; }
        out.gravity[i] = v;
      }
    }
    if (j.contains("queriesHitTriggers")) {
      if (!j["queriesHitTriggers"].is_boolean()) { error = "\"queriesHitTriggers\" must be true or false"; return false; }
      out.flags = j["queriesHitTriggers"].get<bool>() ? kPhy3QueriesHitTriggers : 0u;
    }
    for (const char* key : {"bounceThreshold", "sleepThreshold"}) {
      if (!j.contains(key)) continue;
      if (!j[key].is_number()) { error = std::string("\"") + key + "\" must be a number"; return false; }
      const float v = j[key].get<float>();
      if (!(v >= 0.0f && v <= 1000.0f)) { error = std::string("\"") + key + "\" must be between 0 and 1000"; return false; }
      (std::string(key) == "bounceThreshold" ? out.bounceThreshold : out.sleepThreshold) = v;
    }
    auto layerOf = [&](const json& v, u32& idx) {
      if (v.is_number_integer()) {
        const long long n = v.get<long long>();
        if (n < 0 || n > 31) return false;
        idx = static_cast<u32>(n);
        return true;
      }
      if (!v.is_string()) return false;
      const std::string n = v.get<std::string>();
      if (n.empty()) return false;
      for (size_t i = 0; i < names.size() && i < 32; ++i) if (names[i] == n) { idx = static_cast<u32>(i); return true; }
      return false;
    };
    if (j.contains("ignore")) {
      if (!j["ignore"].is_array()) { error = "\"ignore\" must be an array of [layerA, layerB] pairs"; return false; }
      for (const json& pr : j["ignore"]) {
        u32 a = 0, b = 0;
        if (!pr.is_array() || pr.size() != 2 || !layerOf(pr[0], a) || !layerOf(pr[1], b)) {
          error = "bad \"ignore\" pair " + pr.dump() + " (layers are 0..31 or names from TagManager.json)";
          return false;
        }
        out.masks[a] &= ~(1u << b);  // symmetric, like Unity's matrix
        out.masks[b] &= ~(1u << a);
      }
    }
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
  return true;
}

}  // namespace cooker
}  // namespace rensga
