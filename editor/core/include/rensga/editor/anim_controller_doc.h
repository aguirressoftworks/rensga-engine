// F6.03 (Editor 9): the animator controller document behind the Animator graph window (.controller.json, format in
// docs/rensga-engine/animation.md). States, parameters and transitions are plain data with an undo history. No ImGui, no console names.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

enum class AnimParamType { Float, Int, Bool, Trigger };
struct AnimParamDef {
  std::string name;
  AnimParamType type = AnimParamType::Float;
  double defaultValue = 0;
};
struct AnimStateDef {
  std::string name;
  std::string motion;          // GUID text of a property clip or a skeletal clip; "" = empty state
  bool loop = true;
  double speed = 1;
  std::string speedParameter;  // float parameter that multiplies the speed
  float x = 0, y = 0;          // node position in the graph
};
struct AnimCondDef {
  std::string param;
  std::string mode = "if";     // if ifNot greater less equals notEqual
  double value = 0;
};
struct AnimTransDef {
  std::string from;            // state name, or "*" for any state
  std::string to;
  bool hasExitTime = false;
  double exitTime = 1.0;
  double duration = 0;         // seconds
  std::vector<AnimCondDef> conditions;
};

class AnimControllerDoc {
 public:
  bool load(const std::filesystem::path& file, std::string& err);
  bool save(std::string& err);
  bool loaded() const { return loaded_; }
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const { return dirty_; }

  const std::string& name() const { return name_; }
  const std::string& defaultState() const { return default_; }
  const std::vector<AnimParamDef>& params() const { return params_; }
  const std::vector<AnimStateDef>& states() const { return states_; }
  const std::vector<AnimTransDef>& transitions() const { return trans_; }
  int findState(const std::string& name) const;
  int findParam(const std::string& name) const;

  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);

  // ---- edits, one undo step each; false (nothing changed) when the request is invalid ----
  int addState(const std::string& name, float x, float y);  // -1 when the name is empty or taken
  bool removeState(const std::string& name);                // also drops its transitions
  bool renameState(const std::string& from, const std::string& to);
  bool setDefaultState(const std::string& name);
  bool setMotion(const std::string& state, const std::string& guid);
  bool setStateOptions(const std::string& state, bool loop, double speed, const std::string& speedParameter);
  bool moveState(const std::string& name, float x, float y);  // no undo step per drag frame: call push through endMove
  void beginMove() { push(); }
  int addParam(const std::string& name, AnimParamType type, double def = 0);
  bool removeParam(const std::string& name);                // also drops conditions on it
  bool setParamDefault(const std::string& name, double v);
  int addTransition(const std::string& from, const std::string& to);  // -1 when a state is missing
  bool removeTransition(int index);
  bool setTransition(int index, bool hasExitTime, double exitTime, double duration);
  bool addCondition(int transition, const AnimCondDef& c);
  bool removeCondition(int transition, int cond);
  bool setCondition(int transition, int cond, const AnimCondDef& c);

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  bool undo();
  bool redo();

  // Checks the controller the way the cooker does (bakeController) with motions accepted as they are. "" when valid.
  std::string validate() const;

 private:
  void push();
  void touch() { dirty_ = true; }
  std::filesystem::path path_;
  std::string name_, default_;
  std::vector<AnimParamDef> params_;
  std::vector<AnimStateDef> states_;
  std::vector<AnimTransDef> trans_;
  bool loaded_ = false, dirty_ = false;
  std::vector<nlohmann::json> undo_, redo_;
};

}  // namespace editor
}  // namespace rensga
