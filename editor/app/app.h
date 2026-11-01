// Shared editor state and the panel entry points. No console names here: target data comes from TargetModule.
#pragma once
#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "rensga/editor/editor_log.h"
#include "rensga/editor/hub.h"
#include "rensga/editor/play_controller.h"
#include "rensga/editor/edit_mode_world.h"
#include "rensga/editor/play_session.h"
#include "rensga/editor/project.h"
#include "rensga/editor/script_assets.h"
#include "rensga/editor/script_build.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/target_module.h"
#include "render/scene2d_gl.h"
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
  // F3.01: 2D mode, picking, box select, fly speed
  bool mode2d = false;        // orthographic XY view (default on for scenes with 2D components and no meshes)
  Editor2DCamera cam2;
  std::string modeScene;      // scene file the mode was chosen for
  bool needFrame2d = true;
  float flySpeed = 8.0f;      // units/s, changed with the wheel while RMB is held
  bool boxing = false;        // LMB drag in progress: rectangle selection
  ImVec2 boxStart{0, 0};
  bool boxAdd = false;        // shift/ctrl held when the drag started
  // The last drawn frame, for the --ui-event pick verbs: view area (screen px) and matrices.
  ImVec2 lastP0{0, 0}, lastSize{0, 0};
  M4 lastView = {}, lastProj = {};
  std::vector<std::string> lastIds;  // pick id - 1 -> object GUID
  std::string lastPickName;          // object picked by the last click / pickobj verb
  // --ui-event verbs (run inside the next Scene view draw, where the frame's data lives)
  std::string testPickName;          // pickobj:<name>: pick at the object's centre through the ID buffer, print the result
  bool testBox = false;              // boxsel:x0,y0,x1,y1 (window pixels)
  float testRect[4] = {0, 0, 0, 0};
  bool testBoxAdd = false;
  bool testFrame = false;            // framesel: press F
};

// Scripts (F3.03): what is on disk, and the background build + hot reload that stands in for Unity's domain reload.
enum class ScriptSync { Clean, Building, Failed };
struct ScriptState {
  bool enabled = false;              // watch Assets/Scripts and rebuild in the background
  ScriptBuildJob builder;
  std::vector<ScriptInfo> list;      // scripts found on disk
  double scanStamp = -100;
  std::string pendingHash;           // hash the running build started from
  std::string builtHash;             // hash of the last build that ended (ok or not): no retry until files change
  std::string seenHash;              // debounce: changed hash waiting to stay stable for 500 ms
  double seenSince = 0, pollStamp = 0;
  bool failed = false;               // last build failed and the sources have not changed since
  bool playAfter = false;            // Play was pressed while the build ran
  bool prefsReady = false;
  int buildNumber = 0;               // groups console diagnostics by build
  int lastErrors = 0, lastWarnings = 0;
  std::string statusText;            // last result, for the status bar
  int statusKind = 0;                // 0 neutral, 1 ok, 2 error
  // New script prompt
  bool openNewScript = false;
  char newName[96] = {};
  int newKind = 0;                   // 0 Behaviour, 1 empty Component
  std::string newScriptError;
};

// Startup flow (F1.03): splash -> project picker -> editor. One SDL window changes size/borders per mode.
enum class Mode { Splash, Picker, Editor };
struct HubState {
  Mode mode = Mode::Editor;
  HubSettings settings;
  HubData data;
  std::vector<ProjectEntry> entries;
  int sel = -1;
  bool rescan = true;
  bool goPicker = false;            // request: leave the editor for the picker (handled between frames)
  std::string error;                // last picker error line
  // Create Project dialog
  bool openCreate = false;
  char name[96] = {};
  int api = -1, build = 0;
  std::string createError;
  // Projects path dialog
  bool openChange = false;
  char pathBuf[512] = {};
  std::string pathError;
  // Splash
  int initStep = 0;
  std::string status;
  float progress = 0;
};

struct App {
  HubState hub;
  Project project;
  bool loaded = false;
  SceneDocument doc;
  PlayController play;                // Play on console (emulator pipeline)
  PlaySession session;                // Play in editor (F3.04)
  EditModeWorld editWorld;            // edit-mode script callbacks: gizmos, EXECUTE_IN_EDIT_MODE (F3.07)
  bool playInEditor = true;           // Play dropdown: true = Play in editor (default), false = Play on console
  bool gameIntegerScale = false, gameStats = true, gameFocused = false;  // Game view options / focus
  std::string playKeys;               // --ui-event playkeys: SDL key names held as the Game view input
  std::string target;
  Fonts fonts;

  // Console filters
  bool showInfo = true, showWarn = true, showError = true, showEditor = true, showGame = true, showEmulator = true;
  char logSearch[128] = {};           // F5.22: text filter of the Console (case-insensitive substring)
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
  std::string pendingLayout;      // layout .ini to load between frames (Window > Layouts)
  bool openSaveLayout = false;
  char layoutName[64] = {};
  ScriptState scripts;

  // Project panel cache
  AssetNode assets;
  double assetsStamp = -100;

  // Scene / Game views
  SceneRenderer renderer;
  MeshLibrary meshes;
  Scene2DResources res2d;       // F3.01: textures and tilemaps of the 2D layer
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

void doPlay(App& a);          // the Play button: follows the dropdown (Play in editor by default)
void doPlayConsole(App& a);   // dev build + emulator (--play)
std::string consoleStatusText(App& a);  // toolbar status word of the console run (Running / Running (paused) / Stopped (failed) ...)
// F5.19-F5.22 console-play --ui-event verbs (panels/console_play.cpp): consolepause | consoleresume | consolestep[:N] | consolestop |
// consoledebugger | consolewait:<what>|<timeoutSec> | consolestate | consoledump | consolectl:<raw command> | consolesignal:<STOP|CONT|SEGV|KILL> |
// consolesearch:<text> | consolefilter:<level or source>=<0|1> | consolesave
bool consoleUiHook(App& a, const std::string& act, const std::string& arg);
bool consoleUiHolds(App& a, const std::string& event);   // consolewait holds the --ui-event queue

// Play in editor (panels/play.cpp)
bool startPlayInEditor(App& a);
void stopPlayInEditor(App& a);
void tickPlayInEditor(App& a, double dt);          // once per frame before the panels: steps the World with the Game view input
bool playUiHook(App& a, const std::string& act, const std::string& arg);   // --ui-event playedit|pause|resume|step[:N]|stopedit|playkeys:..|playset:..|playmove:..|playdump:..
bool playUiHolds(App& a, const std::string& event);                        // --ui-event playwait:N holds the queue
void applyPlayTint(App& a);                         // editor chrome tinted while a session runs (brand token primary)
void gameViewRelease();                             // drop the Game view's GL objects bound to the play World

// scripts (panels/scripts.cpp)
nlohmann::json defaultFieldsFor(const TypeInfo& ti);               // reflected defaults of a component type
bool scriptCompiled(const std::string& className);                // registered in this binary
std::string scriptStatus(const App& a, const std::string& className);  // "" ready, else "compiling..." / "needs rebuild"
bool addScriptToObject(App& a, const std::string& objectGuid, const std::string& className);  // one undo step
bool createScriptAction(App& a, const std::string& name, ScriptKind kind = ScriptKind::Behaviour);  // Create > C++ Script (validates, writes, logs)
void tickScripts(App& a);                                          // once per frame, between frames: rescan, pump the build, reload
void scriptsOnFocus(App& a);                                       // window focus gained
void scriptsOnProjectOpen(App& a);                                 // load the module if current, else build it
ScriptSync scriptsSync(App& a);                                    // hash check; starts a build when sources changed
bool scriptsBlockPlay(const App& a);                               // scripts have compile errors
void scriptsCancel(App& a);                                        // status bar Cancel
bool openLogLine(const LogLine& l);                                // console double-click: file:line in the code editor
bool openBuildError(int index);                                    // --ui-event openerror[:N]
bool uiEventHolds(App& a, const std::string& event);               // --ui-event waitbuild|waitbusy: hold the queue
bool scriptsTestEdit(App& a, const std::string& arg);              // --ui-event edit:<file>|<old>|<new> (stands in for the code editor)
void drawScriptDialogs(App& a);

// scene editing actions shared by the Hierarchy, the GameObject menu and the shortcuts (panels/actions.cpp)
std::string createEmptyObject(App& a, const std::string& parent, const float* at = nullptr);
// `at` = world position, else 5 units ahead of the Scene camera
std::string createPrimitiveObject(App& a, const std::string& shape, const std::string& parent, const float* at = nullptr);
std::string createCameraObject(App& a, const std::string& parent, const float* at = nullptr);
std::string createLightObject(App& a, const std::string& parent, const float* at = nullptr);
std::string createPointLightObject(App& a, const std::string& parent, const float* at = nullptr);  // F6.02
std::string createAudioSourceObject(App& a, const std::string& parent, const float* at = nullptr);   // F6.02
void duplicateSelected(App& a);
void deleteSelected(App& a);
void copySelected(App& a);       // objects (with children) to the editor clipboard
void pasteSelected(App& a);      // from the clipboard, under the root, one undo step
void selectAllObjects(App& a);
void beginRename(App& a, const std::string& guid);
void drawCreateMenuItems(App& a, const std::string& parent, const float* at = nullptr);  // Create Empty, 3D Object >, Camera, Light

// panels (each draws its own ImGui window)
void drawMenuBar(App& a);
void drawToolbar(App& a);
void drawHierarchy(App& a);
void drawInspector(App& a);
void drawConsole(App& a);
void drawStatusBar(App& a);
void drawProjectPanel(App& a);
void drawSceneView(App& a);
bool sceneUiHook(App& a, const std::string& act, const std::string& arg);  // --ui-event new, selectmany, reparent, pickobj, ...
void drawGameView(App& a);
void drawDialogs(App& a);

// hub (panels/hub.cpp): splash, project picker, create project
void hubInit(App& a, const std::string& homeOverride);       // settings, hub data, templates
void hubNoteOpened(App& a);                                   // stamp the open project as last opened
void drawSplashBackground(float t, int w, int h);             // GLSL 3.3 grid, before ImGui renders
void drawSplashUi(App& a, float t);                            // lockup, version, status, progress (inside an ImGui frame)
void drawPicker(App& a);
void drawCreateDialogs(App& a);                                // Create Project + projects path popups (any mode)
bool hubHook(App& a, const std::string& act, const std::string& arg);  // --ui-event hub-*
void drawSaveLayoutDialog(App& a);

// layout
ImGuiID dockSpaceId();
void drawDockSpaceAndLayout(App& a);

// screenshot (stage tooling): reads the framebuffer and writes a PNG
bool writeScreenshotPng(const char* file, int w, int h);

}  // namespace editor
}  // namespace rensga
