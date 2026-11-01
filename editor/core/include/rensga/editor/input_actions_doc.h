// F6.04 Editor 10: ProjectSettings/InputActions.json as an editable document (no ImGui). The file format and its vocabulary are the
// cooker's (tools/cooker/include/rensga/cooker/input_cook.h): validate() runs the real cook, so what the window accepts is what the
// cooker accepts. A missing file loads as no actions. Keys the editor does not know survive a round trip (`extra`).
//
//   {"format":1,"stickDeadZone":0.2,"stickOuterClamp":0.95,"actions":[
//     {"name":"Jump","kind":"button","source":"south","alt":"key:Space"},
//     {"name":"Move","kind":"axis2","source":"nub"},
//     {"name":"Keys","kind":"axis2","source":"composite","up":"key:W","down":"key:S","left":"key:A","right":"key:D"}]}
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

struct InputActionEntry {
  std::string name;
  std::string kind = "button";  // button | axis | axis2
  std::string source = "south";
  // Extra bindings by role: "alt" (button, 1), "up"/"down"/"left"/"right" (axis2 composite, 2 each), "positive"/"negative" (axis composite, 2 each).
  std::vector<std::string> alt, up, down, left, right, positive, negative;
  double gravity = 3, sensitivity = 3, dead = 0;
  bool snap = false, invert = false;
  nlohmann::json extra = nlohmann::json::object();
};

struct InputActionsDoc {
  double stickDeadZone = 0.2, stickOuterClamp = 0.95;
  std::vector<InputActionEntry> actions;
  nlohmann::json extra = nlohmann::json::object();

  static InputActionsDoc defaults() { return InputActionsDoc(); }
  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);
  // Problems the cooker would report (one line each), plus duplicate names. Empty = ok.
  std::vector<std::string> validate() const;
  int indexOf(const std::string& name) const;
  // Adds an action with sensible defaults for `kind` and a unique name ("Action", "Action 2", ...). Returns its index.
  int add(const std::string& kind);
  bool remove(int index);
  bool move(int from, int to);
  bool rename(int index, const std::string& name);  // false when the name is empty, too long (23) or taken
  // Changing the kind resets the source and the bindings to that kind's defaults.
  bool setKind(int index, const std::string& kind);
};

// The vocabulary of the cooker, with the labels the window shows.
struct InputChoice {
  const char* id;     // what the file stores
  const char* label;  // what the window shows
};
const std::vector<InputChoice>& inputButtonSources();   // south east west north l r l2 r2 start select up down left right
const std::vector<InputChoice>& inputAxis2Sources();    // nub dpad rightstick composite
const std::vector<InputChoice>& inputAxisSources();     // leftx lefty rightx righty l2 r2 composite
const std::vector<std::string>& inputKeyNames();        // "Space", "W", "UpArrow", ...
// "south" -> "Cross (south)", "key:Space" -> "Key Space"; unknown ids come back as they are.
std::string inputBindingLabel(const std::string& id);
// Touch: on a phone or tablet the on-screen controls send the same virtual gamepad buttons, so a binding to a pad button is also a touch binding.
const char* inputTouchNote();

bool loadInputActions(const std::filesystem::path& projectRoot, InputActionsDoc& out, std::string& err);
bool saveInputActions(const std::filesystem::path& projectRoot, const InputActionsDoc& d, std::string& err);

}  // namespace editor
}  // namespace rensga
