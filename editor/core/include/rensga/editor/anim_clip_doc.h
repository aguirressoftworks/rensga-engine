// F6.03 (Editor 9): the property clip document behind the Animation window. A clip (.clip.json, format in docs/rensga-engine/
// animation.md) is edited here as plain data with an undo history; the window only draws it. No ImGui, no console names.
//
// bake() turns the document into the same PCLP record the cooker writes (cooker/anim_cook.h bakePropClip), so the preview plays
// exactly what the game will play.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/prop_clip.h"

namespace rensga {
namespace editor {

struct AnimKey {
  u32 tick = 0;
  double value = 0;
  double inTangent = 0, outTangent = 0;  // per tick, used by "smooth" curves
  std::string guid;                      // Guid curves: the key value
};
struct AnimCurve {
  std::string target;    // child path below the animated object ("" = the object itself), e.g. "Arm/Hand"
  std::string property;  // "Component.field[.part]", e.g. "Transform.localPosition.x"
  std::string interp = "linear";  // linear | step | smooth
  std::vector<AnimKey> keys;
};
struct AnimEventDef {
  u32 tick = 0;
  std::string function;
  int intValue = 0;
  double floatValue = 0;
  std::string stringValue;
};

// The kind of value of a property, from reflection: how many channels it has and how its keys are written.
enum class AnimValueKind { Float, Int, Bool, Enum, Color, Guid, Unknown };
AnimValueKind animValueKind(const std::string& property);

struct AnimProperty {
  std::string property;  // "Transform.localPosition.x"
  std::string label;     // "Position . x"
};
struct AnimComponentProps {
  std::string component;  // "Transform", "SpriteRenderer", ...
  std::vector<AnimProperty> properties;
};
// Every animatable property of the given component type names (plus Transform and GameObject.active), channels expanded.
std::vector<AnimComponentProps> animatableProperties(const std::vector<std::string>& componentTypes);

// A clip baked into pack form in memory.
struct BakedClip {
  PackPropClip rec = {};
  std::vector<u8> blob;
  std::vector<char> strs;
  PropClipSource source() const {
    PropClipSource s;
    s.clip = &rec;
    s.blob = blob.data();
    s.blobSize = static_cast<u32>(blob.size());
    s.strs = strs.data();
    s.strsSize = static_cast<u32>(strs.size());
    return s;
  }
};

class AnimClipDoc {
 public:
  bool load(const std::filesystem::path& file, std::string& err);
  bool save(std::string& err);  // writes to path()
  bool loaded() const { return loaded_; }
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const { return dirty_; }

  const std::string& name() const { return name_; }
  u32 fps() const { return fps_; }
  u32 length() const { return length_; }
  bool loop() const { return loop_; }
  const std::vector<AnimCurve>& curves() const { return curves_; }
  const std::vector<AnimEventDef>& events() const { return events_; }
  double lengthSeconds() const { return fps_ ? static_cast<double>(length_) / fps_ : 0; }

  nlohmann::json toJson() const;
  bool fromJson(const nlohmann::json& j, std::string& err);

  // ---- edits, one undo step each ----
  void setFps(u32 fps);
  void setLength(u32 ticks);
  void setLoop(bool loop);
  int findCurve(const std::string& target, const std::string& property) const;  // -1 if none
  int addCurve(const std::string& target, const std::string& property);        // existing index when there is one
  bool removeCurve(int curve);
  bool setInterp(int curve, const std::string& interp);
  // Adds or replaces the key at `tick`; the clip grows when the key is past its end. Guid curves take setKeyGuid.
  bool setKey(int curve, u32 tick, double value);
  bool setKeyGuid(int curve, u32 tick, const std::string& guid);
  bool setTangents(int curve, int key, double in, double out);
  bool moveKey(int curve, int key, u32 newTick);  // false when another key is already there
  bool removeKey(int curve, int key);
  int addEvent(u32 tick, const std::string& function, int i = 0, double f = 0, const std::string& s = std::string());
  bool removeEvent(int event);
  bool setEvent(int event, const AnimEventDef& e);  // replaces it (the tick may change: the list is kept sorted, the new index is not returned)
  bool moveEvent(int event, u32 newTick);

  // The value the curve gives at `ticks` (fractional), through the runtime sampler.
  double valueAt(int curve, double ticks) const;
  bool hasKeyAt(int curve, u32 tick) const;
  // The curve's value at `ticks` in doubles with the runtime's rules (linear, step, Hermite); for drawing only, the game uses the baked form.
  static double evalCurve(const AnimCurve& c, double ticks);

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  bool undo();
  bool redo();

  // Bakes the document (the runtime form). False with `err` when the clip is invalid.
  bool bake(BakedClip& out, std::string& err) const;

 private:
  void push();                  // snapshot for undo, before a change
  void touch() { dirty_ = true; }
  void sort(int curve);
  void fixLength(u32 tick) { if (tick > length_) length_ = tick; }

  std::filesystem::path path_;
  std::string name_;
  u32 fps_ = 30, length_ = 30;
  bool loop_ = false, loaded_ = false, dirty_ = false;
  std::vector<AnimCurve> curves_;
  std::vector<AnimEventDef> events_;
  std::vector<nlohmann::json> undo_, redo_;
};

}  // namespace editor
}  // namespace rensga
