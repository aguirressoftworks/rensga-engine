// F3.03 (Editor 4): game scripts live in their own shared library, rebuilt and hot-reloaded by the editor.
//
//   <project>/Assets/Scripts/*.{h,cpp}  --CMake template editor/data/script_module-->
//   <project>/Library/ScriptAssemblies/librensga_game.so   (rensga_game.dll on Windows)
//
// The library links no engine code: engine/core and engine/api symbols resolve from the editor executable (built
// with ENABLE_EXPORTS). Its static registrars add the script types to the process TypeRegistry on load; unload
// removes them again (TypeRegistry::mark / unregisterFrom). Player, cooker, CLI and console builds still link the
// same sources statically. No Sony anything, no console names here: this is the console-agnostic editor core.
//
// Reload contract (the play session of Editor 5 builds on it):
//   addBeforeUnload hooks run while the old types are still registered (serialize live state, destroy Worlds that
//   hold script components), addAfterLoad hooks run once the new types are registered (rebuild Worlds, restore state).
//   Edit mode needs neither: the scene document JSON is the source of truth, and a component whose type vanished
//   stays in the JSON untouched (the inspector shows "Missing script (Type)").
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/reflect.h"

namespace rensga {
class World;
namespace editor {

struct ScriptModuleLayout {
  std::filesystem::path sourceDir;  // the CMake template (editor/data/script_module)
  std::filesystem::path buildDir;   // <project>/Library/ScriptBuild
  std::filesystem::path output;     // <project>/Library/ScriptAssemblies/librensga_game.so
  std::vector<std::vector<std::string>> commands;  // argv lists: configure, then build
};

ScriptModuleLayout scriptModuleLayout(const std::filesystem::path& projectRoot);

// True when the project has at least one .h/.hpp/.cpp under Assets/Scripts. Projects without scripts need no module.
bool projectHasScripts(const std::filesystem::path& projectRoot);

// The library exists and is newer than every script source, the template and this executable.
bool scriptModuleUpToDate(const std::filesystem::path& projectRoot);

// Runs configure + build in this thread at low priority (-j${RENSGA_JOBS:-2}). `log` receives the compiler output.
bool buildScriptModuleBlocking(const std::filesystem::path& projectRoot, std::string& log);

class GameModule {
 public:
  static GameModule& instance();

  // Copies `lib` to Library/ScriptAssemblies/shadow/ (the original stays unlocked and rebuildable) and loads the
  // copy. Fails when a module is already loaded; use reload().
  bool load(const std::filesystem::path& lib, std::string& err);
  // unload() + load(). When the new library does not load, the previous good one is loaded again.
  bool reload(const std::filesystem::path& lib, std::string& err);
  void unload();  // also deletes the shadow copy
  bool loaded() const { return handle_ != nullptr; }
  int generation() const { return generation_; }  // +1 for every successful load
  std::vector<const TypeInfo*> types() const;     // the types the loaded module registered
  bool ownsType(const TypeInfo* t) const;
  const std::filesystem::path& shadowPath() const { return shadow_; }
  const std::filesystem::path& origin() const { return origin_; }  // the library last loaded (before shadowing)

  int addBeforeUnload(std::function<void()> fn);
  int addAfterLoad(std::function<void()> fn);
  void removeHook(int id);

 private:
  GameModule() = default;
  bool loadShadow(const std::filesystem::path& lib, std::string& err);
  void runHooks(bool before);
  void doUnload();  // keeps the shadow file: reload() falls back to it

  struct Hook { int id; bool before; std::function<void()> fn; };
  std::vector<Hook> hooks_;
  int nextHook_ = 1;
  void* handle_ = nullptr;
  u32 firstType_ = 0, endType_ = 0;
  int generation_ = 0;
  std::filesystem::path shadow_;     // the loaded copy
  std::filesystem::path lastGood_;   // what reload() falls back to
  std::filesystem::path origin_;
};

// Project open: makes the module of `projectRoot` the loaded one (unloading another project's module, or any module
// when the project has no scripts). A stale or missing module is built right here when `blockingBuild` (headless
// runs: --play, --screenshot, --ui-event) and left to the background job otherwise (state NeedsBuild: the caller's
// script-build job builds it, then calls reloadGameModule). Messages go to EditorLog.
enum class ModuleState { NoScripts, Loaded, NeedsBuild, BuildFailed, LoadFailed };
ModuleState ensureGameModule(const std::filesystem::path& projectRoot, bool blockingBuild);
// After a successful build: load the module, or hot-reload it when one is loaded.
bool reloadGameModule(const std::filesystem::path& projectRoot, std::string& err);

// Live state of scripted components (those of GameModule types) by object GUID + component index, as
// fieldsToJson. Restore ignores unknown and removed fields and keeps the defaults of new ones.
nlohmann::json serializeScriptState(World& world);
void restoreScriptState(World& world, const nlohmann::json& state);

}  // namespace editor
}  // namespace rensga
