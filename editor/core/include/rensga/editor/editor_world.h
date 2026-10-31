// F3.04 (Editor 5): a runtime World built from a scene document, inside the editor process.
//
// The open scene is cooked from its in-memory JSON (the file on disk is not read or written) together with the project's
// assets for the host desktop platform target (linux/windows: the project's profile and budgets, desktop texture and
// mesh encodings), into Library/Cooked/editor/<host>/game.rpak. The pack is loaded into a
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

#include "rensga/cooker/assets.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/plugin.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"

namespace rensga {
namespace editor {

// F5.07 / F5.08: the scene cooked in the project's console form (the target's own texture and mesh encodings), without loading
// it. Gives the Game view the textures as the console will show them (decoded back through the target's
// ICookerFormatPlugin::decodeCookedTexture, so palette banding and 16-bit colour are visible), the scene's budget usage, and the
// cooker's own validation findings (lights over the per-object limit, joints over the limit, texture downscales, budgets) in a
// form the Inspector can list and select from. No console names: the target id is data.
struct ValidationItem {
  enum Severity { Info, Warning, Error } severity = Warning;
  std::string category;                      // "lights" | "skinning" | "texture" | "audio" | "budget" | "other"
  std::string text;                          // the cooker's message
  std::string asset, assetGuid;              // project-relative path / GUID of the asset it concerns ("" if none)
  std::string objectName, objectGuid;        // scene object it names ("" if none); objectGuid is a guid of the scene document
};

struct ConsolePreview {
  bool ok = false;
  std::string error;
  std::string target;
  std::vector<ValidationItem> items;
  struct Texture {
    std::string guid;
    uint32_t format = 0;          // PackTexFormat of the cooked texture
    uint32_t srcWidth = 0, srcHeight = 0;
    uint32_t storedWidth = 0, storedHeight = 0;
    uint64_t bytes = 0;           // pixels + palette as stored in the console pack
    ImageRgba rgba;               // decoded: what the console samples (possibly downscaled / quantized)
  };
  std::vector<Texture> textures;
  uint64_t packBytes = 0, triangles = 0, objects = 0, textureBytes = 0;
  double ms = 0;
  const Texture* find(const std::string& guid) const {
    for (const Texture& t : textures) if (t.guid == guid) return &t;
    return nullptr;
  }
  int count(ValidationItem::Severity s) const {
    int n = 0;
    for (const ValidationItem& i : items) n += i.severity == s;
    return n;
  }
};

// Cooks `sceneDoc` for `target` into a private folder (Library/Cooked/editor/<target>-preview) and fills `out`. `target` must be
// a cooker plugin id. The document is the in-memory scene, the file on disk is not read.
bool cookConsolePreview(const std::filesystem::path& projectRoot, const std::string& target, const std::filesystem::path& sceneFile,
                        const nlohmann::ordered_json& sceneDoc, ConsolePreview& out);
// CookMessage -> ValidationItem (category, object name and guid looked up in `sceneDoc`).
std::vector<ValidationItem> classifyCookMessages(const std::vector<CookMessage>& msgs, const nlohmann::ordered_json& sceneDoc);

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
  // F5.07: the same scene in the console form (null when the project's target has no cooker plugin or the cook failed).
  const ConsolePreview* consolePreview() const { return preview_.ok ? &preview_ : nullptr; }
  // F5.07: the Game view lights each object with at most `n` lights (0 = what the pack says). Patches the in-memory pack only.
  void setMaxLightsPerObject(uint32_t n);

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
  ConsolePreview preview_;
  bool valid_ = false, overBudget_ = false, guardWas_ = true;
};

}  // namespace editor
}  // namespace rensga
