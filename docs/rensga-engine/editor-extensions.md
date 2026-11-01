# Editor extensions and data assets (Editor 13)

Two ways to adapt the editor to your game without touching the engine:

1. **Editor extensions**: C++ in `<project>/Editor/` that adds custom inspectors, dockable windows and menu items. It is compiled into an editor-only module that hot-reloads while the editor runs.
2. **Data assets**: tuning data (the ScriptableObject equivalent) as plain reflected structs, edited in the Inspector, cooked into the pack and read by scripts.

The runnable example is [`samples/extending_editor`](../../samples/extending_editor/README.md). Unity users: [guide-for-unity-users.md](guide-for-unity-users.md).

## 1. Editor extensions

### Layout and build

```text
MyGame/
  Assets/Scripts/*.cpp, *.h     game scripts: built into the game module, run in the player and on the console
  Editor/*.cpp, *.h             editor extensions: editor only
```

- `Editor/` sits next to `Assets/`, not inside it, so nothing in it is imported, cooked, packed or built for a console. A player never contains it.
- The editor builds `Editor/**/*.cpp` into `Library/EditorAssemblies/librensga_editor_ext.so` (`rensga_editor_ext.dll` on Windows) with CMake, in the background, at low priority, when a file changes (the same rules as game scripts: 0.5 s debounce, `--no-auto-rebuild` turns it off). Compiler errors land in the Console with file and line; double-click opens your code editor.
- A successful build is loaded between two frames. The old module's registrations are dropped first, then the library is unloaded and the new one loaded, so a window you had open stays open (its open state is kept by title) and a menu item you removed is gone. If the new library does not build or does not load, the last good one stays loaded.
- The module links nothing. The editor API, ImGui and the engine resolve from the editor executable when the library loads. Compile flags: C++17, exceptions and RTTI on (it is editor code), `-Wall -Wextra`. Include paths: the editor core headers, the engine headers, ImGui, nlohmann/json and your own `Editor/` folder.
- The module cannot name a console. Anything that depends on the target reads it through the editor API or the ConsoleTarget file, like the rest of the editor.
- **Windows**: not built or tested yet. The module links the editor's import library (`RENSGA_EDITOR_IMPORT_LIB`), the same open point as game scripts.

Window > Editor Extensions shows the state: loaded, building, failed, how many inspectors, windows and menu items the module registered, and a **Rebuild now** button.

### What you can register

All four are one macro at file scope. Include `<rensga/editor/extension.h>`; ImGui comes from `<imgui.h>`.

| Macro | Effect |
| --- | --- |
| `RENSGA_CUSTOM_INSPECTOR(Type, fn)` | `fn(ComponentView&)` draws extra widgets under the generated fields of every `Type` component. |
| `RENSGA_CUSTOM_INSPECTOR_REPLACE(Type, fn)` | The generated fields are not drawn; `fn` draws everything. Call `DrawDefaultInspector(view)` to get them back. |
| `RENSGA_EDITOR_WINDOW("Title", fn)` | `fn()` draws the contents of a dockable window. It is listed in Window and remembers if it was open. |
| `RENSGA_MENU_ITEM("Tools/Do Thing", fn)` | `fn()` runs on click. The first path part is the menu, more parts are submenus (`"Tools/Level/Snap"`). |
| `RENSGA_MENU_ITEM_KEY("Tools/Do Thing", "Ctrl+Shift+G", fn)` | The same with a shortcut: `Ctrl`, `Shift`, `Alt` plus a letter, digit or `F1` to `F12`. Not active while a text field has focus. |

`Type` is the component's class name as written in the scene file (`Patroller`), without quotes. A custom inspector works for engine components too (`Camera`, `Light`).

```cpp
#include <imgui.h>
#include <rensga/editor/extension.h>
using namespace rensga::editor::ext;

static void patrollerInspector(ComponentView& v) {
  ImGui::SeparatorText("Patrol tools");
  if (ImGui::Button("Swap A and B")) {
    float a[3], b[3];
    v.GetVector3("pointA", a);
    v.GetVector3("pointB", b);
    v.SetVector3("pointA", b);
    v.SetVector3("pointB", a);          // Ctrl+Z reverts both in one step
  }
}
RENSGA_CUSTOM_INSPECTOR(Patroller, patrollerInspector)
```

A callback that throws is caught: the exception is logged once in the Console and the editor goes on. A crash (null pointer, endless loop) in your code is still a crash of the editor, so keep callbacks small. The module is reloaded often, so keep state in the scene or in a data asset and treat static variables as cache.

### The editor API

Everything is in `rensga::editor::ext` (`extension.h`). Every call is safe with no project open and returns an empty result.

**`ComponentView`** (the argument of an inspector callback) wraps the component of the primary selected object, plus the same component of the other selected objects:

| Call | Meaning |
| --- | --- |
| `Type()`, `ObjectName()`, `ObjectGuid()`, `Count()`, `Multi()` | what is inspected |
| `Get(field)` (JSON), `GetFloat`, `GetInt`, `GetBool`, `GetString`, `GetVector3`, `GetGuid` | effective value (stored, else the default) |
| `Mixed(field)` | the selected objects disagree on the field |
| `Set(field, json)`, `SetFloat`, `SetInt`, `SetBool`, `SetString`, `SetVector3` | edit, on every selected object, as one undo step; runs `OnValidate`; false for an unknown field or a value that does not fit |
| `TypeOf()` | the reflection (`TypeInfo`) of the component |

**`Selection`**: `Guids()`, `Names()`, `ActiveGuid()`, `Select(nameOrGuid)`, `SelectMany(guids)`.

**`SceneEdit`** (the open scene; not named `Scene`, which is a runtime type): `Find(nameContains, withComponent)`, `NameOf(guid)`, `GetField` / `SetField(objectGuid, type, field, json)`, `GetPosition` / `SetPosition`, `BeginGroup()` / `EndGroup()` (everything between is one undo step), `Undo()`, `Redo()`. Edits made while the game plays in the editor go to the running world, like Inspector edits, and are undone on Stop.

**`Assets`**: `Find(nameContains, kind)` (kind: `texture`, `audio`, `model`, `scene`, `script`, `prefab`, `material`, `dataasset`, ...), `PathOf(guid)`, `GuidOf(path)`, `ProjectRoot()`, `ReadDataAsset(guid)` (`{"type", "fields"}`), `SetDataAssetField(guid, field, json)` (written to the file at once, not part of the scene's undo).

**`Log`**: `Info`, `Warning`, `Error` (Console), `Status` (status bar).

Not in the API on purpose: raw access to the scene JSON, the file system of the project outside `Assets`, anything that names a console. If you need something, ask for an API call rather than reaching into editor internals; the internals change.

### Behaviour worth knowing

- A custom inspector sees only components the script module knows. A component whose script is missing still shows "Missing script (Type)".
- With several objects selected the inspector runs once, with `Multi()` true. `Set` writes all of them.
- A widget drag in your callback (`DragFloat`, a slider) is one undo step: the editor opens a gesture around custom inspectors and closes it when no widget is active.
- The registry is keyed by order of registration. Two inspectors for one type both run.
- Unload order: registrations are removed, then the library is closed. Do not keep function pointers or `ImGui` pointers from the module in static data of the editor.

## 2. Data assets

A data asset is a struct of tuning values that is not a component and lives in no scene.

### Declare

```cpp
// Assets/Scripts/EnemyStats.h
#include <rensga/Rensga.h>
struct EnemyStats : DataAsset {
  REFLECT(EnemyStats)
  HEADER("Tuning")
  RANGE(1, 200)
  SERIALIZE_FIELD(i32, health, 30)
  SERIALIZE_FIELD(Scalar, speed, 2.0f)
  SERIALIZE_FIELD(String32, label, "Grunt")
  SERIALIZE_ENUM(kind, 0, "Walker|Scout|Brute")
  SERIALIZE_ARRAY(Scalar, pauses, 4)
};
```

- Any reflected struct that is not a `Component` or `Behaviour` is a data asset type; deriving from `DataAsset` documents the intent (it adds no data and no virtual functions).
- Field types are the reflected ones: `Scalar`, `i32`, `bool`, `Vector2`, `Vector3`, `Quaternion`, `Color32`, `Guid`, `String32`, `SERIALIZE_ENUM`, `SERIALIZE_ARRAY` of `i32`, `Scalar`, `Guid` or `Vector3`. `HEADER`, `TOOLTIP`, `RANGE`, `SPACE` and `HIDE_IN_INSPECTOR` work like on components.
- Limits (the cooker names the asset): at most 512 bytes per instance, and 24 data assets live at once at run time.
- Add fields at the end and put `APPENDED_FIELDS()` before the first new one so existing assets and packs stay readable. A field you remove or rename must also be removed from the asset files; the Inspector shows a warning and the next edit of the asset drops it.

### Create and edit

- **Project > Create > Data Asset > EnemyStats** (right-click in the Project window works too) writes `Assets/.../New EnemyStats.asset.json` with the defaults and selects it. The submenu lists every data asset type of the loaded scripts; with none it says how to write one.
- Selecting the asset shows its type and fields in the Inspector, through the same widgets as component fields (enum dropdown, array list, colour picker, asset picker). Every edit is written to the file at once and can be undone with the **Undo** and **Redo** buttons (a slider drag is one step).
- Rename, move and delete work like any asset; the GUID in the `.meta` keeps references intact.
- A data asset whose script is missing shows "Missing script (Type)" and is left untouched.

File format (`*.asset.json`, diff-friendly):

```json
{ "format": 1, "kind": "DataAsset", "type": "EnemyStats",
  "fields": { "label": "Grunt", "kind": 0, "health": 30, "speed": 2.0, "tint": [220, 80, 60, 255], "pauses": [0.5] } }
```

### Reference and read at run time

A component refers to a data asset with a `Guid` field: drag the asset onto it in the Inspector. At run time:

```cpp
SERIALIZE_FIELD(Guid, stats, Guid())
const EnemyStats* data = nullptr;
void Start() override { data = DataAssets::Get<EnemyStats>(stats); }   // null when unset, unknown or of another type (logged once)
```

- `DataAssets::Get<T>(guid)` returns a read-only pointer that stays valid for the run. The instances live in a fixed table, built from the pack on first use: no heap, safe on every profile. Call it from `Awake` or `Start`. To change a value at run time copy it into the component.
- `Resources::Load<T>("data/boss")` works for assets under an `Assets/**/Resources/` folder and returns a `Handle<T>`; it shares the pack data.
- Editor Play uses the same code path, so a value edited in the Inspector is what the next Play sees.

### Cooking

`rensga cook` writes every `*.asset.json` of the project into the `DATA` section (see [pack-format.md](pack-format.md)): the GUID, the type hash, the name and the reflected blob (trailing appended fields that are still default are left out, like components). A project without data assets gets no `DATA` section, so existing packs stay byte-identical. The section is sorted by path and cooking twice gives the same bytes.

The cooker refuses, naming the asset: invalid JSON, a missing or unknown `type`, a type that is a component, a field the type does not have, a value that does not fit its field.

## 3. Testing and `--ui-event` verbs

```bash
ctest --test-dir build/host -R "extension|data_assets|data_cook|editor_extensions_ui|editor_data_asset_ui"
```

| Verb | Effect |
| --- | --- |
| `extbuild` | build and load `Editor/` now (headless runs do this at project open) |
| `extwait` | hold the queue until no extension build is running |
| `extdump` | print `ext: module loaded=.. generation=.. inspectors=.. windows=.. menus=.. building=.. failed=..` |
| `extwindow:<Title_With_Underscores>[\|off]` | open or close a registered window |
| `extmenu:<Menu/Path>` | run a registered menu item (spaces as `_`) |
| `extstatus[:off]` | open the Editor Extensions window |
| `datacreate:<Type>[\|name]` | Create > Data Asset > Type in the current Project folder |
| `dataset:<field>=<json>` / `dataundo` / `dataredo` / `datadump` / `datatype:<Type>` | edit and inspect the selected data asset |

## 4. Open points

- Windows: the extension module is untested there (see Layout and build).
- No debugger integration: attach `gdb` to the editor and break in your callback.
- Custom property drawers per field type (Unity's `PropertyDrawer`) are not part of this phase; use `RENSGA_CUSTOM_INSPECTOR_REPLACE` and draw the field yourself.
- Data assets cannot hold other data assets by value or a list of structs (field types are the reflected ones). Use `Guid` references and `SERIALIZE_ARRAY` of `Guid`.
