// F6.01 (Editor 6): prefab UI state shared by the Hierarchy, Inspector and Project window, and the prefab edit mode (the "stage").
// Edit mode swaps the open SceneDocument for the prefab converted to a scene (editor/core prefab_edit.h); the scene is parked and
// restored on exit. Panels call these functions; the verbs are in prefab_ui.cpp.
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "app.h"
#include "rensga/editor/prefab_edit.h"
#include "rensga/editor/prefab_library.h"

namespace rensga {
namespace editor {

struct PrefabStageLevel {
  std::string prefabGuid;
  StageIds ids;
};

struct PrefabUi {
  PrefabLibrary lib;
  std::filesystem::path root;       // project the library is open for
  std::vector<PrefabStageLevel> stages;   // open prefabs, outermost first (empty = the scene is open)
  bool autoSave = true;
  // the scene parked while a prefab is open
  std::filesystem::path mainPath;
  nlohmann::ordered_json mainDoc;
  bool mainDirty = false;
  std::string mainSelection;
  std::string error;                // last failure shown in the breadcrumb bar
  std::vector<std::function<void(App&)>> deferred;   // menu actions that rebuild the scene view: run at the start of the next frame
};
PrefabUi& prefabUi();               // leaked on purpose (like assetUi)

// Runs `fn` at the start of the next frame (before any panel holds pointers into the scene view). Menus use it for Apply, Revert, Open and Unpack.
void prefabDefer(std::function<void(App&)> fn);
void prefabEnsure(App& a);                          // library open for the project and attached to a.doc (call before using a.doc's prefab API)
bool prefabInStage(const App& a);
std::string prefabStageName(const App& a);          // "" when no prefab is open
bool prefabOpen(App& a, const std::string& prefabGuid);                         // enter / descend into a prefab
bool prefabClose(App& a, int keepLevels = 0, const std::string& mode = "");     // back to the scene (0) or to an outer prefab; mode "save"|"discard"
bool prefabSaveStage(App& a);
// Project window and Hierarchy actions (each is one undo step in the scene)
std::string prefabCreateFromObject(App& a, const std::string& objectGuid, const std::filesystem::path& folder);
bool prefabInstantiateFromAsset(App& a, const std::string& assetGuid, const std::string& targetObject);
void drawPrefabBreadcrumbs(App& a);                 // Hierarchy header: Scene > Enemy > Weapon, Auto Save, Save
void drawPrefabInspectorHeader(App& a, const std::string& objectGuid);   // Inspector: asset, Open, Select, overrides menu
bool prefabUiHook(App& a, const std::string& act, const std::string& arg);   // --ui-event prefab* verbs

}  // namespace editor
}  // namespace rensga
