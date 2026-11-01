// F6.03 (Editor 9): Animation window preview and record mode. While a preview is active the open scene document is frozen behind
// a mirror Scene (the same mechanism Play in editor uses): the clip is sampled with the runtime sampler (PropClips) onto a copy of the
// scene, the Scene view shows that copy, and edits made in the Inspector or with the Scene view gizmos reach this class through the
// PlayEditSink. In record mode such an edit becomes a key at the current time; otherwise it only changes the preview. Ending the
// preview drops the copy: the document was never touched, so nothing needs undoing. No ImGui, no console names.
#pragma once
#include <string>
#include <vector>

#include "rensga/editor/anim_clip_doc.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

class AnimPreview : public PlayEditSink {
 public:
  // Starts a preview of `clip` on the subtree under the object `rootGuid` (curve paths are relative to it). False when the document
  // is playing, already previewing, or the root does not exist.
  bool begin(SceneDocument& doc, const std::string& rootGuid, AnimClipDoc& clip, std::string& err);
  void end(SceneDocument& doc);
  // Another owner took the document's mirror (Play started): drop the preview without touching the document.
  void forget() { active_ = false; record_ = false; clip_ = nullptr; }
  bool active() const { return active_; }
  const std::string& rootGuid() const { return root_; }

  // Time in clip ticks (fractional). Reapplies the clip to the baseline copy.
  void setTime(double ticks);
  double time() const { return time_; }
  void setRecord(bool on) { record_ = on; }
  bool recording() const { return record_; }
  // Rebakes the clip after it changed (keys added, moved, ...) and reapplies it at the current time.
  void refresh();

  const Scene& scene() const { return preview_; }
  // The preview's value of `property` ("Transform.localPosition.x", "SpriteRenderer.color.r", ...) on the object at `target`
  // (relative path, "" = root). False when the object or property does not exist.
  bool value(const std::string& target, const std::string& property, double* out) const;
  // Child path of `guid` below the root ("" for the root itself); false when it is not in the root's subtree.
  bool pathOf(const std::string& guid, std::string& out) const;
  const std::string& message() const { return message_; }
  int keysRecorded() const { return recorded_; }

  // PlayEditSink: edits while previewing.
  bool transform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) override;
  bool active(const std::string& guid, bool active) override;
  bool field(const std::string& guid, int component, const std::string& field, const nlohmann::json& value) override;

 private:
  void apply();
  SceneObject* find(Scene& s, const std::string& guid);
  SceneObject* findPath(Scene& s, const std::string& path);
  u32 tickNow() const { return time_ < 0 ? 0u : static_cast<u32>(time_ + 0.5); }
  bool keyChannel(const std::string& guid, const std::string& property, double v);

  Scene baseline_, preview_;
  AnimClipDoc* clip_ = nullptr;
  BakedClip baked_;
  bool bakedOk_ = false;
  std::string root_, message_;
  double time_ = 0;
  bool active_ = false, record_ = false;
  int recorded_ = 0;
};

}  // namespace editor
}  // namespace rensga
