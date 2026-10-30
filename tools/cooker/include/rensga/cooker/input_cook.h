// F3.06: ProjectSettings/InputActions.json -> INPT / INPX / INPS pack records. Header-only so the cooker and its tests share it.
//
//   {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": [
//     {"name": "Jump",  "kind": "button", "source": "south", "alt": "key:Space"},
//     {"name": "Move",  "kind": "axis2",  "source": "nub"},
//     {"name": "Keys",  "kind": "axis2",  "source": "composite", "up": "key:W", "down": "key:S", "left": "key:A", "right": "key:D",
//      "gravity": 3, "sensitivity": 3, "snap": true},
//     {"name": "Steer", "kind": "axis",   "source": "rightx", "positive": ["key:E", "r"], "negative": "key:Q", "dead": 0.1, "invert": false}]}
//
// Button sources: south east west north l r l2 r2 start select up down left right, or key:<KeyCode name>.
// Axis2 sources: nub dpad rightstick composite. Axis sources: leftx lefty rightx righty l2 r2 composite (bindings only).
#pragma once
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <string>
#include <vector>

#include "rensga/core/keycode.h"
#include "rensga/core/pack_format.h"

namespace rensga {

struct CookedInput {
  std::vector<PackInputAction> actions;
  std::vector<PackInputActionExt> ext;
  PackInputSettings settings = {0.2f, 0.95f, {0, 0}};
};

namespace input_cook_detail {
inline std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}
// Same order as the KeyCode enumerators (index == value).
inline const char* const* keyNames() {
  static const char* const k[] = {
      "None", "Backspace", "Tab", "Return", "Escape", "Space", "Alpha0", "Alpha1", "Alpha2", "Alpha3", "Alpha4", "Alpha5", "Alpha6",
      "Alpha7", "Alpha8", "Alpha9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
      "U", "V", "W", "X", "Y", "Z", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "UpArrow", "DownArrow",
      "LeftArrow", "RightArrow", "LeftShift", "RightShift", "LeftControl", "RightControl", "LeftAlt", "RightAlt", "Home", "End",
      "PageUp", "PageDown", "Insert", "Delete", "Comma", "Period", "Slash", "Minus", "Equals", "Semicolon", "Quote", "LeftBracket",
      "RightBracket", "Backslash", "BackQuote"};
  static_assert(sizeof(k) / sizeof(k[0]) == static_cast<size_t>(KeyCode::Count), "key name table out of step with KeyCode");
  return k;
}
inline bool keyFromName(const std::string& name, KeyCode& out) {
  const std::string want = lower(name);
  for (unsigned i = 1; i < static_cast<unsigned>(KeyCode::Count); ++i)
    if (lower(keyNames()[i]) == want) {
      out = static_cast<KeyCode>(i);
      return true;
    }
  return false;
}
// "south" / "key:Space" -> binding value; false when unknown.
inline bool binding(const std::string& text, u32& out) {
  static const struct { const char* n; PackInputSource s; } kButtons[] = {
      {"south", PackInputSource::South}, {"east", PackInputSource::East}, {"west", PackInputSource::West},
      {"north", PackInputSource::North}, {"l", PackInputSource::L}, {"r", PackInputSource::R},
      {"l2", PackInputSource::L2}, {"r2", PackInputSource::R2}, {"start", PackInputSource::Start},
      {"select", PackInputSource::Select}, {"up", PackInputSource::Up}, {"down", PackInputSource::Down},
      {"left", PackInputSource::Left}, {"right", PackInputSource::Right}};
  if (text.size() > 4 && lower(text.substr(0, 4)) == "key:") {
    KeyCode k;
    if (!keyFromName(text.substr(4), k)) return false;
    out = kInputKeyBase + static_cast<u32>(k);
    return true;
  }
  const std::string t = lower(text);
  for (const auto& b : kButtons)
    if (t == b.n) {
      out = static_cast<u32>(b.s);
      return true;
    }
  return false;
}
// A string or an array of up to `max` strings, written into dst[0..max).
inline bool bindings(const nlohmann::json& j, const char* field, u32* dst, int max, std::string& err) {
  if (!j.contains(field)) return true;
  std::vector<std::string> list;
  const nlohmann::json& v = j[field];
  if (v.is_string()) list.push_back(v.get<std::string>());
  else if (v.is_array())
    for (const auto& e : v) list.push_back(e.get<std::string>());
  else {
    err = std::string("'") + field + "' must be a string or an array of strings";
    return false;
  }
  if (static_cast<int>(list.size()) > max) {
    err = std::string("'") + field + "' takes at most " + std::to_string(max) + " bindings";
    return false;
  }
  for (size_t i = 0; i < list.size(); ++i)
    if (!binding(list[i], dst[i])) {
      err = std::string("'") + field + "': unknown binding '" + list[i] + "'";
      return false;
    }
  return true;
}
}  // namespace input_cook_detail

// Parses the document. `addStr` returns the STRS offset of a name. Every problem is appended to `errors` (one line each).
inline void cookInputActions(const nlohmann::json& doc, const std::function<u32(const std::string&)>& addStr, CookedInput& out,
                             std::vector<std::string>& errors) {
  using namespace input_cook_detail;
  out = CookedInput();
  try {
    out.settings.deadZone = doc.value("stickDeadZone", 0.2f);
    out.settings.outerClamp = doc.value("stickOuterClamp", 0.95f);
    if (!(out.settings.deadZone >= 0 && out.settings.deadZone < out.settings.outerClamp && out.settings.outerClamp <= 1.0f))
      errors.push_back("stickDeadZone must be in [0, stickOuterClamp) and stickOuterClamp at most 1");
    for (const nlohmann::json& a : doc.at("actions")) {
      const std::string name = a.at("name").get<std::string>(), kind = a.at("kind").get<std::string>();
      const std::string src = lower(a.value("source", std::string(kind == "axis" ? "composite" : "")));
      const std::string where = "action '" + name + "': ";
      if (name.empty() || name.size() >= 24) {
        errors.push_back(where + "name must be 1 to 23 characters");
        continue;
      }
      PackInputActionExt x = {};
      for (u32& b : x.bind) b = kInputNone;
      x.gravity = a.value("gravity", 3.0f);
      x.sensitivity = a.value("sensitivity", 3.0f);
      x.dead = a.value("dead", 0.0f);
      x.flags = (a.value("snap", false) ? kInputFlagSnap : 0u) | (a.value("invert", false) ? kInputFlagInvert : 0u);
      PackInputAction pa = {};
      pa.nameStr = addStr(name);
      std::string err;
      bool ok = true;
      if (x.gravity < 0 || x.sensitivity <= 0 || x.dead < 0 || x.dead >= 1) {
        errors.push_back(where + "gravity must be >= 0, sensitivity > 0 and dead in [0, 1)");
        continue;
      }
      if (kind == "button") {
        pa.kind = static_cast<u32>(PackInputKind::Button);
        u32 s = 0;
        if (!binding(src, s)) {
          errors.push_back(where + "unknown button source '" + src + "'");
          continue;
        }
        pa.source = s;
        ok = bindings(a, "alt", &x.bind[0], 1, err);
      } else if (kind == "axis2") {
        pa.kind = static_cast<u32>(PackInputKind::Axis2);
        if (src == "nub") pa.source = static_cast<u32>(PackInputSource::Nub);
        else if (src == "dpad") pa.source = static_cast<u32>(PackInputSource::DPad);
        else if (src == "rightstick") pa.source = static_cast<u32>(PackInputSource::RightStick);
        else if (src == "composite") pa.source = static_cast<u32>(PackInputSource::Composite);
        else {
          errors.push_back(where + "unknown axis2 source '" + src + "'");
          continue;
        }
        ok = bindings(a, "up", &x.bind[0], 2, err) && bindings(a, "down", &x.bind[2], 2, err) &&
             bindings(a, "left", &x.bind[4], 2, err) && bindings(a, "right", &x.bind[6], 2, err);
      } else if (kind == "axis") {
        pa.kind = static_cast<u32>(PackInputKind::Axis);
        static const struct { const char* n; PackInputSource s; } kAxes[] = {
            {"leftx", PackInputSource::AxisLeftX}, {"lefty", PackInputSource::AxisLeftY}, {"rightx", PackInputSource::AxisRightX},
            {"righty", PackInputSource::AxisRightY}, {"l2", PackInputSource::AxisL2}, {"r2", PackInputSource::AxisR2},
            {"composite", PackInputSource::Composite}};
        bool found = false;
        for (const auto& e : kAxes)
          if (src == e.n) {
            pa.source = static_cast<u32>(e.s);
            found = true;
          }
        if (!found) {
          errors.push_back(where + "unknown axis source '" + src + "'");
          continue;
        }
        ok = bindings(a, "positive", &x.bind[0], 2, err) && bindings(a, "negative", &x.bind[2], 2, err);
      } else {
        errors.push_back(where + "unknown kind '" + kind + "' (button, axis2 or axis)");
        continue;
      }
      if (!ok) {
        errors.push_back(where + err);
        continue;
      }
      out.actions.push_back(pa);
      out.ext.push_back(x);
    }
  } catch (const std::exception& e) {
    errors.push_back(e.what());
  }
}

// Script files (relative to the project) under Assets/Scripts that call Input::SetVibration / StopVibration. The cooker warns
// when the ConsoleTarget has no "rumble" capability: the call is a no-op there.
inline std::vector<std::string> scriptsUsingVibration(const std::filesystem::path& project) {
  namespace fs = std::filesystem;
  std::vector<std::string> out;
  std::error_code ec;
  const fs::path dir = project / "Assets" / "Scripts";
  if (!fs::is_directory(dir, ec)) return out;
  for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
    const std::string ext = e.path().extension().string();
    if (!e.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".hpp")) continue;
    std::ifstream in(e.path());
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string txt = ss.str();
    if (txt.find("SetVibration") != std::string::npos || txt.find("StopVibration") != std::string::npos)
      out.push_back(fs::relative(e.path(), project, ec).generic_string());
  }
  return out;
}

}  // namespace rensga
