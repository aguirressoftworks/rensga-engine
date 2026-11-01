// F6.07 (Editor 13): editor extensions written in C++ in the user's project.
//
//   <project>/Editor/**/*.{h,cpp}  --CMake template editor/data/editor_module-->  Library/EditorAssemblies/librensga_editor_ext.so
//
// The module is separate from the game module (Assets/Scripts): it is never cooked, never linked into a player and never built for a
// console. The editor builds it in the background when a file under Editor/ changes and hot-reloads it, the same way it reloads game
// scripts (shadow copy, old registrations dropped first). The module links no editor code: this header's functions, ImGui and the
// editor's own symbols resolve from the editor executable when the library loads (the executable is built with ENABLE_EXPORTS).
//
// What user code can register (one macro each, at file scope):
//
//   RENSGA_CUSTOM_INSPECTOR(Patrol, drawPatrol)            draws extra widgets under the fields of every Patrol component
//   RENSGA_CUSTOM_INSPECTOR_REPLACE(Patrol, drawPatrol)    draws instead of the generated fields (call DrawDefaultInspector(v) to keep them)
//   RENSGA_EDITOR_WINDOW("Waypoint Tool", drawTool)        a dockable window, listed in Window > Extensions
//   RENSGA_MENU_ITEM("Tools/Snap To Ground", snapToGround) a menu item; the first path part is the menu
//   RENSGA_MENU_ITEM_KEY("Tools/Snap To Ground", "Ctrl+Shift+G", snapToGround)
//
// and the editor API user code may call (namespace rensga::editor::ext): ComponentView (read and edit the inspected component with
// undo), Selection, Scene, Assets, Log. Everything that changes the scene is exactly one undo step, or one step per
// BeginGroup/EndGroup. No ImGui here: user code includes <imgui.h> itself. No console names.
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/asset_database.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {
namespace ext {

// ---- the component a custom inspector draws -------------------------------------------------------------------------
// Wraps the same-type components of the selected objects (primary first). Reads give the effective value (stored or default); every
// Set* writes all of them as ONE undo step and runs OnValidate, exactly like the generated fields do.
class ComponentView {
 public:
  ComponentView(SceneDocument& doc, std::vector<ComponentRef> refs, std::string type) : doc_(doc), refs_(std::move(refs)), type_(std::move(type)) {}
  const std::string& Type() const { return type_; }
  int Count() const { return static_cast<int>(refs_.size()); }  // components edited together (selected objects)
  bool Multi() const { return refs_.size() > 1; }
  std::string ObjectName() const;                                // the primary object
  std::string ObjectGuid() const { return refs_.empty() ? std::string() : refs_[0].guid; }
  bool Mixed(const char* field) const;                           // the selected objects disagree on `field`

  nlohmann::json Get(const char* field) const;
  float GetFloat(const char* field, float fallback = 0) const;
  int GetInt(const char* field, int fallback = 0) const;
  bool GetBool(const char* field, bool fallback = false) const;
  std::string GetString(const char* field) const;
  bool GetVector3(const char* field, float out[3]) const;        // false when the field is not a Vector3
  std::string GetGuid(const char* field) const;                  // "" when unset

  bool Set(const char* field, const nlohmann::json& value);      // false: unknown field, wrong JSON shape, or play mode
  bool SetFloat(const char* field, float v) { return Set(field, v); }
  bool SetInt(const char* field, int v) { return Set(field, v); }
  bool SetBool(const char* field, bool v) { return Set(field, v); }
  bool SetString(const char* field, const std::string& v) { return Set(field, v); }
  bool SetVector3(const char* field, const float v[3]) { return Set(field, nlohmann::json::array({v[0], v[1], v[2]})); }
  const TypeInfo* TypeOf() const;                                // reflection of the component type (null when the script is gone)

 private:
  SceneDocument& doc_;
  std::vector<ComponentRef> refs_;
  std::string type_;
};

// ---- registration (the macros call these) -----------------------------------------------------------------------------
using InspectorFn = void (*)(ComponentView&);
using VoidFn = void (*)();

struct InspectorEntry { std::string type; InspectorFn fn; bool replaceDefault; unsigned seq; };
struct WindowEntry { std::string title; VoidFn fn; unsigned seq; };
struct MenuEntry { std::string path; std::string shortcut; VoidFn fn; unsigned seq; };

void addInspector(const char* type, InspectorFn fn, bool replaceDefault);
void addWindow(const char* title, VoidFn fn);
void addMenuItem(const char* path, const char* shortcut, VoidFn fn);
const std::vector<InspectorEntry>& inspectors();
const std::vector<WindowEntry>& windows();
const std::vector<MenuEntry>& menuItems();
bool inspectorReplaces(const std::string& type);               // a registered inspector replaces the generated fields of `type`
unsigned registrationMark();                                   // sequence number the next registration gets
void unregisterFrom(unsigned mark);                            // drops every registration made since `mark` (before the library unloads)
bool& windowOpen(const std::string& title);                    // open state survives a reload (keyed by title)

struct Registrar {
  Registrar(const char* type, InspectorFn fn, bool replaceDefault) { addInspector(type, fn, replaceDefault); }
  Registrar(const char* title, VoidFn fn) { addWindow(title, fn); }
  Registrar(const char* path, const char* shortcut, VoidFn fn) { addMenuItem(path, shortcut, fn); }
};

// ---- the editor API ---------------------------------------------------------------------------------------------------
// Set by the editor for the loaded project. All calls are safe without a project (they return empty results).
struct Host {
  SceneDocument* doc = nullptr;
  AssetDatabase* assets = nullptr;
  std::filesystem::path projectRoot;
  std::function<void(const std::string&)> setStatus;           // the status bar text
};
Host& host();

namespace Selection {
std::vector<std::string> Guids();          // selected objects, the primary first
std::vector<std::string> Names();
std::string ActiveGuid();                  // the primary, "" when nothing is selected
bool Select(const std::string& nameOrGuid); // by name (first match) or GUID
bool SelectMany(const std::vector<std::string>& guids);
}  // namespace Selection

struct ObjectInfo { std::string guid, name; bool active = true; };
namespace SceneEdit {
std::vector<ObjectInfo> Find(const std::string& nameContains = "", const std::string& withComponent = "");
std::string NameOf(const std::string& guid);
// Field of the first component named `type` on the object: effective value, or null.
nlohmann::json GetField(const std::string& objectGuid, const std::string& type, const std::string& field);
// One undo step. False when the object or component is missing or the value does not fit the field.
bool SetField(const std::string& objectGuid, const std::string& type, const std::string& field, const nlohmann::json& value);
bool SetPosition(const std::string& objectGuid, float x, float y, float z);
bool GetPosition(const std::string& objectGuid, float out[3]);
void BeginGroup();                          // every edit until EndGroup is one undo step
void EndGroup();
bool Undo();
bool Redo();
}  // namespace SceneEdit

struct AssetInfo { std::string path, guid, kind; };   // path is project-relative ("Assets/Data/Grunt.asset.json"), kind like "dataasset", "texture"
namespace Assets {
std::vector<AssetInfo> Find(const std::string& nameContains = "", const std::string& kind = "");
std::string PathOf(const std::string& guid);
std::string GuidOf(const std::string& path);
std::string ProjectRoot();
// A data asset (*.asset.json) by GUID: {"type": "EnemyStats", "fields": {effective values of every field}}; null when it is none.
nlohmann::json ReadDataAsset(const std::string& guid);
// Writes one field of a data asset to its file (validated against the type; not part of the scene's undo history). False with a log line on failure.
bool SetDataAssetField(const std::string& guid, const std::string& field, const nlohmann::json& value);
}  // namespace Assets

namespace Log {
void Info(const std::string& text);
void Warning(const std::string& text);
void Error(const std::string& text);
void Status(const std::string& text);       // the status bar
}  // namespace Log

// Draws the generated fields of the inspected component (Unity: DrawDefaultInspector). Defined by the editor application.
void DrawDefaultInspector(ComponentView& view);

// ---- building and loading the module (editor core; the application calls these) --------------------------------------------
struct ExtensionLayout {
  std::filesystem::path sourceDir;   // the CMake template (editor/data/editor_module)
  std::filesystem::path editorDir;   // <project>/Editor
  std::filesystem::path buildDir;    // <project>/Library/EditorBuild
  std::filesystem::path output;      // <project>/Library/EditorAssemblies/librensga_editor_ext.so
  std::vector<std::vector<std::string>> commands;  // configure, then build
};
ExtensionLayout extensionLayout(const std::filesystem::path& projectRoot);
bool projectHasEditorCode(const std::filesystem::path& projectRoot);   // at least one .h/.hpp/.cpp under <project>/Editor
// A signature of the sources (names, sizes, times) used to notice a change; "" without sources.
std::string hashEditorSources(const std::filesystem::path& projectRoot);
bool extensionModuleUpToDate(const std::filesystem::path& projectRoot);
bool buildExtensionModuleBlocking(const std::filesystem::path& projectRoot, std::string& log);

class ExtensionModule {
 public:
  static ExtensionModule& instance();
  bool load(const std::filesystem::path& lib, std::string& err);    // copies to a shadow file and loads it; its registrations appear
  bool reload(const std::filesystem::path& lib, std::string& err);  // unload + load; the previous good library comes back if the new one fails
  void unload();                                                     // registrations first, then dlclose
  bool loaded() const { return handle_ != nullptr; }
  int generation() const { return generation_; }
  const std::filesystem::path& origin() const { return origin_; }
  // Registrations made by the loaded library.
  int inspectorCount() const;
  int windowCount() const;
  int menuCount() const;

 private:
  bool loadShadow(const std::filesystem::path& lib, std::string& err);
  void doUnload();
  void* handle_ = nullptr;
  unsigned mark_ = 0;
  int generation_ = 0;
  std::filesystem::path shadow_, lastGood_, origin_;
};

}  // namespace ext
}  // namespace editor
}  // namespace rensga

#define RENSGA_EXT_CAT2(a, b) a##b
#define RENSGA_EXT_CAT(a, b) RENSGA_EXT_CAT2(a, b)
#define RENSGA_EXT_REGISTER(...) static ::rensga::editor::ext::Registrar RENSGA_EXT_CAT(rensga_ext_registrar_, __COUNTER__){__VA_ARGS__};

#define RENSGA_CUSTOM_INSPECTOR(Type, fn) RENSGA_EXT_REGISTER(#Type, fn, false)
#define RENSGA_CUSTOM_INSPECTOR_REPLACE(Type, fn) RENSGA_EXT_REGISTER(#Type, fn, true)
#define RENSGA_EDITOR_WINDOW(title, fn) RENSGA_EXT_REGISTER(title, fn)
#define RENSGA_MENU_ITEM(path, fn) RENSGA_EXT_REGISTER(path, "", fn)
#define RENSGA_MENU_ITEM_KEY(path, shortcut, fn) RENSGA_EXT_REGISTER(path, shortcut, fn)
