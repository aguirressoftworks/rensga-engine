// F3.04 (Editor 5): Play in editor. Press Play: the open scene document is snapshotted (JSON, selection, undo depth), cooked from
// memory for the project's profile and loaded into an EditorWorld; the editor steps its GameLoop every frame. Stop destroys
// the World and checks the document against the snapshot: nothing of the run is saved or recorded in undo.
//
// While a session runs the scene document is frozen. Hierarchy, Inspector and Scene view read the *play mirror* (a Scene
// refreshed from the World after every frame: all transforms by GUID, the selected object's component fields, and objects
// spawned at run time marked "(runtime)"). Edits made to transforms, active flags and component fields are applied to the
// running components and the mirror; they never reach the saved document or the undo stack (PlayEditSink).
// Scripts hot reloaded while playing (GameModule) keep their state: the World is rebuilt in the after-load hook and
// serializeScriptState / restoreScriptState / transforms bring the running values back.
// Budget findings (VRAM, memory, polygons, sprites) are logged once per crossing and listed for the Game view overlay.
// No ImGui, no console names.
#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/editor_world.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

enum class PlayState { Stopped, Playing, Paused };

struct PlayStats {
  double fps = 0;          // editor frames per second while playing (smoothed)
  double simMs = 0;        // last GameLoop::frame
  uint32_t draws = 0, sprites = 0;       // reported by the Game view's renderer
  uint32_t triangles = 0;                // active MeshRenderers this frame
  uint32_t objects = 0, runtimeObjects = 0;
  uint64_t vramUsed = 0, vramCapacity = 0;       // cooked textures + framebuffers
  uint64_t memUsed = 0, memCapacity = 0;         // pack + world arena vs ConsoleTarget ramBytes
  uint64_t trianglesBudget = 0;
  uint64_t gameFrames = 0;                       // loop frames since Play
};

struct BudgetFinding {
  std::string key;   // "vram" | "memory" | "triangles" | "cook"
  std::string text;
};

class PlaySession : public PlayEditSink {
 public:
  PlaySession() = default;
  ~PlaySession() override;

  // Starts a session for the open document. `target` is the project's console profile id (the cooker plugin name).
  // False and `err` when it cannot start (document not loaded, scripts missing, cook error, ...).
  bool start(SceneDocument& doc, const std::filesystem::path& projectRoot, const std::string& target, std::string& err);
  // Destroys the World, unfreezes the document and verifies it against the snapshot (restoring it if it differs).
  void stop(SceneDocument& doc);

  PlayState state() const { return state_; }
  bool active() const { return state_ != PlayState::Stopped; }
  void setPaused(bool paused);
  void togglePause() { setPaused(state_ == PlayState::Playing); }
  void requestStep() { if (state_ == PlayState::Paused) stepRequests_++; }  // one fixed frame while paused

  // Once per editor frame (UI thread) while active. `dt` is the real time since the last editor frame; `pad` is the
  // input of the Game view (all-zero when it is not focused). Steps the World, then refreshes the mirror.
  void tick(double dt, const VirtualGamepad& pad);
  void setFrameStats(uint32_t draws, uint32_t sprites) { stats_.draws = draws; stats_.sprites = sprites; }

  EditorWorld* world() { return world_.get(); }
  const Scene& mirror() const { return mirror_; }
  const PlayStats& stats() const { return stats_; }
  const std::vector<BudgetFinding>& findings() const { return findings_; }  // currently over a budget
  uint64_t frames() const { return gameFrames_; }
  uint64_t fixedSteps() const { return world_ ? world_->loop().fixedStepCount() : 0; }
  int generation() const { return generation_; }  // +1 whenever the World was rebuilt (hot reload): the view re-uploads textures
  bool worldRebuilt() { bool r = rebuilt_; rebuilt_ = false; return r; }
  bool isRuntimeOnly(const std::string& guid) const { return runtimeOnly_.count(guid) != 0; }
  const std::string& lastError() const { return lastError_; }

  // The snapshot taken at Play (tests compare the document against it).
  const nlohmann::ordered_json& snapshotJson() const { return snapDoc_; }
  size_t snapshotUndoDepth() const { return snapUndo_; }
  bool snapshotDirty() const { return snapDirty_; }
  const std::string& snapshotSelected() const { return snapSelected_; }

  // Runtime access for the Game view / tests.
  GameObject* objectByKey(const std::string& key) const;

  // PlayEditSink
  bool transform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) override;
  bool active(const std::string& guid, bool active) override;
  bool field(const std::string& guid, int component, const std::string& field, const nlohmann::json& value) override;

 private:
  bool buildWorld(std::string& err);
  void refreshMirror(bool fullFields);
  void checkBudgets(bool force);
  void onBeforeUnload();
  void onAfterLoad();
  void finding(const std::string& key, bool over, const std::string& text);

  SceneDocument* doc_ = nullptr;
  std::filesystem::path root_;
  std::string target_;
  std::unique_ptr<EditorWorld> world_;
  PlayState state_ = PlayState::Stopped;
  int stepRequests_ = 0;
  uint64_t gameFrames_ = 0;
  double accum_ = 0;

  // snapshot
  nlohmann::ordered_json snapDoc_;
  std::string snapSelected_;
  std::vector<std::string> snapSelection_;
  size_t snapUndo_ = 0;
  bool snapDirty_ = false;
  std::filesystem::path snapPath_;

  // mirror
  Scene mirror_;
  std::unordered_map<std::string, size_t> mirrorIndex_;   // guid / runtime key -> mirror_.objects
  std::unordered_map<std::string, uint32_t> keyToObject_; // guid / runtime key -> World object index
  std::map<std::string, bool> runtimeOnly_;
  std::string mirrorSelected_;

  // hot reload
  int hookBefore_ = 0, hookAfter_ = 0;
  nlohmann::json savedScripts_, savedTransforms_;
  bool reloading_ = false, rebuilt_ = false;
  int generation_ = 0;

  PlayStats stats_;
  std::vector<BudgetFinding> findings_;
  std::map<std::string, bool> over_;
  std::string lastError_;
  double fpsAvg_ = 0;
};

// Pure helper (tests): the key used for a runtime GameObject.
std::string playObjectKey(const GameObject& go);

}  // namespace editor
}  // namespace rensga
