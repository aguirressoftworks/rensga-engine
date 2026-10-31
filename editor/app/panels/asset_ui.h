// F3.02: UI state shared by the Project window, the asset inspector and the VRAM map.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "app.h"
#include "panels/thumbnails.h"
#include "rensga/editor/file_watcher.h"
#include "rensga/editor/import_settings.h"
#include "rensga/editor/vram_map.h"

namespace rensga {
namespace editor {

struct ImportUndo {
  std::string guid, tab;
  nlohmann::json before, after;
};

struct AssetUi {
  AssetSync sync;
  Thumbnails thumbs;
  std::filesystem::path root;  // project the sync is open for
  std::string cookTarget;      // the project's console target id (a cooker plugin id), "" when unknown
  ConsoleTargetData ct;

  // Project window
  std::string folder = "Assets";  // current folder (project-relative)
  char search[128] = {};
  std::string selGuid;            // selected asset ("" none)
  std::string selDocObject;       // scene selection when the asset was picked; a different one drops the asset selection
  std::string renameRel, deleteRel;  // pending inline rename / delete confirm
  bool openRename = false, openDelete = false, openNewFolder = false;
  bool openCreateMenu = false;   // --ui-event createmenu: pops the Project window's Create menu (screenshots)
  char nameBuf[128] = {};
  std::string errorText;

  // Asset inspector
  bool forceTab = false;          // select the tab named by `tab` on the next draw (set by --ui-event assettab)
  std::string tab;                // "" = Default, else a build target id
  std::string pendingGuid, pendingTab;
  nlohmann::json pending = nlohmann::json::object();  // unsaved settings of (pendingGuid, pendingTab)
  std::vector<ImportUndo> undo, redo;
  GlImage previewImg;
  std::string previewKey;
  ImportOutcome preview;

  // VRAM map
  bool showVram = false;
  std::vector<VramItem> vramItems;
  VramLayout vramLayout;
  int vramKeyGen = -1;
  size_t vramKeyCount = 0;
  uint64_t vramKeyCap = 0;
};

AssetUi& assetUi();  // leaked on purpose: its GL objects must not be freed after the GL context is gone

// Opens the sync for the loaded project (first call per project) and pumps it once per frame.
void assetUiTick(App& a);
void drawProjectPanel(App& a);
void drawVramMapPanel(App& a);
// Inspector dispatch: draws the selected asset's import settings; false when no asset is selected (draw the object).
bool drawAssetInspector(App& a);
// A project asset dropped on the Hierarchy / Scene view. `targetObject` may be empty. Texture -> new SpriteRenderer
// object, scene -> open, script -> add to the object (or the selection).
bool acceptAssetDrop(App& a, const std::string& guid, const std::string& targetObject);
// --ui-event verbs (documented in AGENTS.md).
// Project > Create > <label> (one of rensga::editor::stubKinds): creates the stub in the current folder, selects it. Prints "asset: created <rel>".
bool createAssetFromMenu(App& a, const std::string& label, const std::string& name);
bool assetUiHook(App& a, const std::string& act, const std::string& arg);
// Asset inspector actions (also the --ui-event verbs assettab / assetset / assetapply / assetrevert / assetundo).
void assetInspectSetTab(App& a, const std::string& tab);                      // "" or "default" = Default
bool assetInspectSetField(App& a, const std::string& key, const std::string& value);
bool assetInspectApply(App& a);
void assetInspectRevert(App& a);
bool assetInspectUndo(App& a, bool redo);
void selectAsset(App& a, const std::string& guid);
bool openSceneFile(App& a, const std::filesystem::path& file);

}  // namespace editor
}  // namespace rensga
