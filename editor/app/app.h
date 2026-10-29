// Shared editor state and the panel entry points. No console names here: target data comes from TargetModule.
#pragma once
#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "rensga/editor/play_controller.h"
#include "rensga/editor/project.h"
#include "rensga/editor/script_assets.h"
#include "rensga/editor/script_build.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/target_module.h"
#include "render/scene_renderer.h"
#include "rensga/editor/viewport.h"
#include "rensga/core/reflect.h"
#include "theme.h"

namespace rensga {
namespace editor {

struct AssetNode {
  std::string name;
  bool dir = false;
  std::filesystem::path path;   // on disk
  std::vector<AssetNode> children;
};

// Scene view interaction state (camera, tool, gizmo, drag).
struct SceneViewState {
  EditorCamera cam;
  bool needFrame = true;      // no saved camera: frame the level on first draw
  bool camDirty = false;      // camera moved since the last save to Library/Editor
  int op = 0;                 // 0 translate, 1 rotate, 2 scale
  bool local = false;         // gizmo space
  int drag = 0;               // 0 none, 1 look (RMB), 2 pan (MMB), 3 orbit (Alt+LMB)
  bool gizmoUsing = false;
  bool rmbClick = false;      // RMB went down in the view and has not moved: a click opens the create menu
  ImVec2 rmbPos{0, 0};
  bool openCreateMenu = false;
  bool ctxHasPoint = false;   // where a new object goes: the cursor ray's hit (or 5 units ahead)
  float ctxPoint[3] = {0, 0, 0};
  bool clickValid = false;
  ImVec2 clickPos{0, 0};
};

// Scripts (MVP-54..57): what is on disk, and the auto rebuild that stands in for Unity's domain reload.
enum class ScriptSync { Clean, Building, Failed };
struct ScriptState {
  bool enabled = false;              // auto rebuild armed (needs the build dir compiled into this binary)
  ScriptBuilder builder;
  std::vector<ScriptInfo> list;      // scripts found on disk
  double scanStamp = -100;
  std::string pendingHash, failedHash;
  bool failed = false;               // last rebuild failed and the sources have not changed since
  bool playAfter = false;            // Play was pressed while the rebuild ran
  bool restartRequested = false;     // rebuild finished: main saves the state and re-executes
  // set by main
  std::filesystem::path buildDir, compiledProject;
  std::string cmake = "cmake";
  // New script prompt
  bool openNewScript = false;
  char newName[96] = {};
  std::string newScriptError;
};

struct App {
  Project project;
  bool loaded = false;
  SceneDocument doc;
  PlayController play;
  std::string target;
  Fonts fonts;

  // Console filters
  bool showInfo = true, showWarn = true, showError = true, showEditor = true, showGame = true;
  bool scrollLog = true;

  // Panels (closable from Window menu)
  bool showHierarchy = true, showInspector = true, showConsole = true, showProject = true, showScene = true,
       showGame2 = true;

  // Layout
  bool resetLayout = false;       // request: rebuild the default Unity-style layout this frame
  bool focusScene = false;
  bool persistLayout = false;     // save <project>/Library/Editor/layout.ini
  std::filesystem::path layoutIni;

  // Dialogs / deferred actions
  bool openAbout = false, openProjectDialog = false;
  char openPath[512] = {};
  std::string pendingProject;     // handled between frames
  bool quitRequested = false;
  ScriptState scripts;
  std::string restoreFocus;       // window to focus after a restart
  bool restoredPlayAfter = false;

  // Project panel cache
  AssetNode assets;
  double assetsStamp = -100;

  // Scene / Game views
  SceneRenderer renderer;
  MeshLibrary meshes;
  RenderTarget sceneRT, gameRT;
  SceneViewState sv;
  std::filesystem::path cameraFile() const { return project.root / "Library" / "Editor" / "scene_camera.json"; }
  void saveCamera();

  std::string status;             // last editor message for the status area

  // Hierarchy rename in progress (F2 / double-click)
  std::string renameGuid;
  bool renameFocus = false;

  bool openProject(const std::string& dir);  // loads project + first scene, sets up layout persistence
  bool saveScene();
  void refreshAssets();
  const TargetModule* targetModule() const;
};

void doPlay(App& a);

// scripts (panels/scripts.cpp)
nlohmann::json defaultFieldsFor(const TypeInfo& ti);               // reflected defaults of a component type
bool scriptCompiled(const std::string& className);                // registered in this binary
std::string scriptStatus(const App& a, const std::string& className);  // "" ready, else "compiling..." / "needs rebuild"
bool addScriptToObject(App& a, const std::string& objectGuid, const std::string& className);  // one undo step
bool createScriptAction(App& a, const std::string& name);         // Create > C++ Script (validates, writes, logs)
void tickScripts(App& a);                                          // once per frame: rescan, poll the build
void scriptsOnFocus(App& a);                                       // window focus gained
ScriptSync scriptsSync(App& a);                                    // hash check; starts a rebuild when sources changed
bool scriptsBlockPlay(const App& a);                               // last rebuild failed
bool saveReloadState(App& a, const std::string& focusedWindow, bool playAfter);
bool restoreReloadState(App& a);
void drawScriptDialogs(App& a);

// scene editing actions shared by the Hierarchy, the GameObject menu and the shortcuts (panels/actions.cpp)
std::string createEmptyObject(App& a, const std::string& parent, const float* at = nullptr);
// `at` = world position, else 5 units ahead of the Scene camera
std::string createPrimitiveObject(App& a, const std::string& shape, const std::string& parent, const float* at = nullptr);
std::string createCameraObject(App& a, const std::string& parent, const float* at = nullptr);
std::string createLightObject(App& a, const std::string& parent, const float* at = nullptr);
void duplicateSelected(App& a);
void deleteSelected(App& a);
void beginRename(App& a, const std::string& guid);
void drawCreateMenuItems(App& a, const std::string& parent, const float* at = nullptr);  // Create Empty, 3D Object >, Camera, Light

// panels (each draws its own ImGui window)
void drawMenuBar(App& a);
void drawToolbar(App& a);
void drawHierarchy(App& a);
void drawInspector(App& a);
void drawConsole(App& a);
void drawProjectPanel(App& a);
void drawSceneView(App& a);
void drawGameView(App& a);
void drawDialogs(App& a);

// layout
ImGuiID dockSpaceId();
void drawDockSpaceAndLayout(App& a);

// screenshot (stage tooling): reads the framebuffer and writes a PNG
bool writeScreenshotPng(const char* file, int w, int h);

}  // namespace editor
}  // namespace rensga
