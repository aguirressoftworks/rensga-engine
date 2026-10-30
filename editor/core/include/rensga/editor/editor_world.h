// F3.04 (Editor 5): a runtime World built from a scene document, inside the editor process.
//
// The open scene is cooked from its in-memory JSON (the file on disk is not read or written) together with the project's
// assets for the project's console profile, into Library/Cooked/editor/<profile>/game.rpak. The pack is loaded into a
// World exactly like the desktop player does it (World::loadPack, input actions, scene manager, fixed-step physics hook).
// Play sessions use it for Play in editor; edit-mode script callbacks can reuse it (F3 stage 3).
// Scripts come from GameModule: build the world only while the module is loaded, destroy it before an unload.
// No ImGui, no console names: the profile is data from ConsoleTarget.json.
#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"

namespace rensga {
namespace editor {

class EditorWorld {
 public:
  EditorWorld() = default;
  ~EditorWorld() { destroy(); }
  EditorWorld(const EditorWorld&) = delete;
  EditorWorld& operator=(const EditorWorld&) = delete;

  // Cooks `sceneDoc` (the document of the scene file `sceneFile`) for `target` and loads it. False and `err` on failure
  // (the first cook error names the asset). Over-budget scenes still load; see overBudget() and messages().
  bool build(const std::filesystem::path& projectRoot, const std::string& target, const std::filesystem::path& sceneFile,
             const nlohmann::ordered_json& sceneDoc, std::string& err);
  // OnDisable/OnDestroy for every object, then the memory goes. Safe to call twice.
  void destroy();
  bool valid() const { return valid_; }
  uint64_t id() const { return id_; }  // unique per successful build(): renderers re-bind their textures when it changes

  World& world() { return world_; }
  GameLoop& loop() { return loop_; }
  const LoadedPack& loaded() const { return loaded_; }
  const PackView& view() const { return view_; }
  const ConsoleTargetData& consoleTarget() const { return target_; }
  const std::vector<CookMessage>& messages() const { return messages_; }  // cook output (warnings include budget overruns)
  bool overBudget() const { return overBudget_; }
  std::string sceneName() const { return sceneName_; }
  uint32_t sceneIndex() const { return sceneIndex_; }
  size_t packBytes() const { return view_.size; }
  size_t arenaUsed() const { return arena_.used(); }
  size_t arenaCapacity() const { return arena_.capacity(); }
  uint32_t cookedAssets() const { return cooked_; }
  uint32_t cachedAssets() const { return cached_; }
  uint64_t textureBytes() const { return textureBytes_; }
  double cookMs() const { return cookMs_; }

  // One editor frame: GameLoop::frame (fixed steps with catch-up capped by the loop). False if the heap guard failed it.
  bool frame(Scalar dt, const VirtualGamepad& pad) { return loop_.frame(dt, pad); }
  // Exactly one fixed step (Pause + Step).
  bool step(const VirtualGamepad& pad) { return loop_.frame(loop_.config().fixedDelta, pad); }

 private:
  World world_;
  GameLoop loop_;
  Arena packArena_, arena_;
  PackView view_;
  LoadedPack loaded_;
  ConsoleTargetData target_;
  std::vector<CookMessage> messages_;
  std::string sceneName_;
  uint32_t sceneIndex_ = 0, cooked_ = 0, cached_ = 0;
  uint64_t textureBytes_ = 0;
  double cookMs_ = 0;
  uint64_t id_ = 0;
  bool valid_ = false, overBudget_ = false, guardWas_ = true;
};

}  // namespace editor
}  // namespace rensga
