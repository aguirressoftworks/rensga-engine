// F3.07 (API 11): the edit-mode World. While no Play session runs, the open scene document is cooked from memory into an
// EditorWorld (same path as Play in editor) so that script callbacks can run in the editor:
//   - OnDrawGizmos / OnDrawGizmosSelected of every behaviour draw into the Gizmos list of the Scene view;
//   - behaviours of types declared EXECUTE_IN_EDIT_MODE() get Awake / OnEnable / Start and one Update + LateUpdate pass
//     after each rebuild (Unity runs Update in edit mode when something changed); all other behaviours stay dormant.
// The World is rebuilt (debounced) when the document changes and after a script hot reload, and destroyed before one. It
// never writes the document: only the normal SceneDocument edit API does. Application::isPlaying reads false inside it.
// Only one EditorWorld may own the engine's global state at a time, so call release() before a Play session starts.
// No ImGui, no console names (the profile id comes from the project).
#pragma once
#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "rensga/editor/editor_world.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

class EditModeWorld {
 public:
  EditModeWorld() = default;
  ~EditModeWorld();
  EditModeWorld(const EditModeWorld&) = delete;
  EditModeWorld& operator=(const EditModeWorld&) = delete;

  // Once per UI frame. `dt` is the real time since the last call. Rebuilds the World when the document changed and
  // `debounce` seconds passed (or at once when none exists yet and the document did not change for a frame). Does nothing
  // (and releases the World) while `suspended` (Play session active) or while no script module is loaded.
  void update(const SceneDocument& doc, const std::filesystem::path& projectRoot, const std::string& target, double dt,
              bool suspended);
  // Tests / tools: rebuild now if the document differs from the one the World was built from.
  bool sync(const SceneDocument& doc, const std::filesystem::path& projectRoot, const std::string& target);
  // Destroys the World (OnDisable / OnDestroy run for edit-mode behaviours). Safe to call twice.
  void release();

  bool valid() const { return world_ && world_->valid(); }
  EditorWorld* world() { return world_.get(); }
  void setDebounce(double seconds) { debounce_ = seconds; }
  // Default true: no World without a loaded script module (its types are what the scene's scripts need). Tests that link
  // their behaviours statically turn it off.
  void setRequireModule(bool on) { requireModule_ = on; }
  unsigned rebuilds() const { return rebuilds_; }
  const std::string& lastError() const { return lastError_; }

  // Appends the OnDrawGizmos output of every enabled behaviour (and OnDrawGizmosSelected of the behaviours on
  // `selectedKey`, a GUID text or a runtime key from playObjectKey) to the Gizmos list. Call after GizmoDraw::clear().
  void drawGizmos(const std::string& selectedKey);
  // The same on any World (the running one during Play in editor). Returns the number of callbacks made.
  static unsigned drawGizmos(World& world, const std::string& selectedKey);

 private:
  bool rebuild(const SceneDocument& doc, const std::filesystem::path& root, const std::string& target);
  void hook();

  std::unique_ptr<EditorWorld> world_;
  nlohmann::ordered_json built_, seen_;
  std::filesystem::path path_;
  double sinceChange_ = 0, debounce_ = 0.25, sinceCheck_ = 0;
  bool stale_ = true, hooked_ = false, requireModule_ = true;
  int hookBefore_ = 0, hookAfter_ = 0;
  unsigned rebuilds_ = 0;
  std::string lastError_, loggedError_;
};

}  // namespace editor
}  // namespace rensga
