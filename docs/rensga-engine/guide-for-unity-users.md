# Rensga Engine for Unity developers

You know Unity. This guide maps what you know to Rensga Engine, shows the first hour (project, script, play, ship), and lists what is different on purpose. It assumes the Step 1 and F1 to F6 editor: Hub, Hierarchy, Inspector, Project, Scene, Game, Console, Play in editor, prefabs, 3D physics, Animator, UI, build settings.

The short version: the editor looks like Unity's and the scripting API keeps Unity's names, but scripts are **C++17** and the game must fit a PSP, PS2 or PSX. The editor checks the limits for you before you run.

## 1. The first hour

1. **Hub > New Project**, pick a template (`psp-3d`, `psp-2d`, `ps2-2d`). A project is a folder with `Assets/`, `ProjectSettings/` and (generated) `Library/`. The folder is the whole project; commit `Assets/` and `ProjectSettings/`.
2. **Project > Create > C++ Script** and type a name (`Coin`). Two things happen that Unity does not do: the file appears under `Assets/Scripts/Coin.cpp`, and the editor compiles it into a shared library in the background (status bar). Edit in any editor; save; the library hot-reloads, even in Play mode.
3. Drag the script onto an object (or **Add Component > Scripts > Coin**).
4. **Play** runs the game in the editor window (the Game view). The dropdown next to Play also offers *Play on console*: it cooks the project and starts the emulator (PPSSPP, PCSX2).
5. **File > Build Settings** lists the scenes in the build and the targets (Linux, Windows, Android, the console of the project) and has Build and Run. The console build is a real `EBOOT.PBP` or disc image.

Everything you can do in the editor is also on the command line: `rensga cook <project>`, `rensga build --target psp <project>`, `rensga play ...` (see `AGENTS.md` for the commands).

## 2. Concepts

| Unity | Rensga Engine | Notes |
| --- | --- | --- |
| `GameObject` | `GameObject` | Same name. Owned by the engine. Scripts never `new` or `delete` it. |
| `Component` | `Component` | Fixed-capacity pools per type, sized from your scene data. |
| `MonoBehaviour` | `Behaviour` | `struct Coin : Behaviour { REFLECT(Coin) ... }` |
| `Transform` | `Transform` | In a script: `transform()->position()`, `setPosition(v)` (a getter and a `setX` setter instead of a property). On another object both spellings work: `other->transform()->position()` (the same as inside a Behaviour) and `other->transform.position()`. |
| `ScriptableObject` | `DataAsset` | A reflected struct, `*.asset.json`. See section 7. |
| Prefab | Prefab (`.prefab`) | Instances, overrides, nesting, variants; expanded at cook time. |
| Scene | Scene (`.scene`) | JSON, diff-friendly. `SceneManager::LoadScene`, additive and async loads. |
| `[SerializeField]` / public field | `SERIALIZE_FIELD(type, name, default)` | The only way to expose a field. See section 3. |
| `[Range]`, `[Header]`, `[Tooltip]`, `[Space]`, `[HideInInspector]` | `RANGE`, `HEADER`, `TOOLTIP`, `SPACE`, `HIDE_IN_INSPECTOR` | Written before the field. |
| `[RequireComponent]` | `REQUIRE_COMPONENT(Type)` | |
| `[ContextMenu]` | `CONTEXT_MENU("Label")` before a `void()` method | |
| `[ExecuteInEditMode]` | `EXECUTE_IN_EDIT_MODE()` | |
| `OnValidate`, `Reset` | `OnValidate()`, `Reset()` | Run on a detached copy; touch only your own fields. |
| `Resources.Load<T>` | `Resources::Load<T>("path")` | Returns a `Handle<T>`; call `Resources::Unload`. |
| `PlayerPrefs` | `PlayerPrefs::SetInt / GetInt / SetFloat / SetString / HasKey / DeleteKey / Save` | 64 keys. Saved on quit; call `Save()` to be sure. |
| `JsonUtility` / save files | `SaveSystem::Write / Read / Exists / Delete` | Versioned slots on a memory card, `SAVEDATA` or a file. |
| `Debug.Log` | `Debug::Log("fmt %d", x)`, `Debug::LogFormat("speed {0}", speed)` | `Log` is printf style with no `%f` for `Scalar`; `LogFormat` takes a `Format` string where `Scalar`s print with 2 decimals (see section 8). |
| Unity Editor scripting | `<project>/Editor/*.cpp` | Section 6 and [editor-extensions.md](editor-extensions.md). |
| Inspector, Hierarchy, Project, Scene, Game, Console | the same windows | Plus Animation, Animator, Profiler, Lighting, Material Editor, Input Actions, Project Settings. |

## 3. A script, side by side

Unity:

```csharp
public class Coin : MonoBehaviour {
  [SerializeField, Range(0, 10)] float spin = 90f;
  [SerializeField] int value = 1;
  void Update() { transform.Rotate(0, spin * Time.deltaTime, 0); }
  void OnTriggerEnter(Collider other) {
    if (other.CompareTag("Player")) { Score.Add(value); Destroy(gameObject); }
  }
}
```

Rensga:

```cpp
#include <rensga/Rensga.h>
using namespace rensga;

struct Coin : Behaviour {
  REFLECT(Coin)
  RANGE(0, 360)
  SERIALIZE_FIELD(Scalar, spin, 90.0f)
  SERIALIZE_FIELD(i32, value, 1)

  void Update() override { transform()->Rotate(Vector3(0, spin * Time::deltaTime(), 0)); }
  void OnTriggerEnter(GameObject* other) override {
    if (other->CompareTag("Player")) { /* tell the score object */ Destroy(gameObject()); }
  }
};
```

Rules of thumb:

- **One include**: `<rensga/Rensga.h>`. Never a console or platform header.
- `REFLECT(Type)` must be the first line of the struct. Every field you want in the Inspector, a scene file, a prefab or the pack is a `SERIALIZE_FIELD(type, name, default)`. A plain C++ member (`i32 hits = 0;`) is runtime state and is not saved. The scanner (`reflect_scan.py`) reads these macros; it runs when the module builds.
- Field types: `Scalar`, `i32`, `bool`, `Vector2`, `Vector3`, `Quaternion`, `Color32`, `Guid` (a reference to an object or asset), `String32` (31 characters), `UnityEvent`, `SERIALIZE_ENUM(name, default, "A|B|C")` (a dropdown, stored as an `i32`) and `SERIALIZE_ARRAY(i32|Scalar|Guid|Vector3, name, capacity)` (fixed capacity 1 to 64). There is no `std::string`, no `std::vector`, no `List<T>`.
- A reference to another object is a `Guid` field (drag the object or asset onto it). Resolve it in `Start`: `GameObject::Find("Name")`, `FindWithTag`, or `DataAssets::Get<T>(guid)`.
- Lifecycle order is Unity's: `Awake`, `OnEnable`, `Start`, `FixedUpdate`, `Update`, `LateUpdate`, `OnDisable`, `OnDestroy`. `EXECUTION_ORDER(n)` replaces Script Execution Order.
- Adding a field later: put `APPENDED_FIELDS()` before the first new field, so scenes and packs written before it stay valid.

## 4. API mapping

**Components and objects.** `GetComponent<T>()`, `GetComponentInChildren<T>()`, `GetComponentInParent<T>()`, `AddComponent<T>()`, `GameObject::Find`, `FindWithTag`, `CompareTag`, `SetActive`, `Destroy(obj)`, `Instantiate(original, position, rotation)`. The pointers they return are transient: use them in the same function. To keep one across frames store a `Ref<T>` (`go->ref()`), which reads as null after `Destroy` (Unity's "fake null"), or look it up again.

**Prefabs.** A `Guid` field that names a prefab asset is cooked as an inactive **template** object in the scene. The pattern for spawning:

```cpp
SERIALIZE_FIELD(Guid, coinPrefab, Guid())     // drag Coin.prefab onto it in the Inspector
void Start() override {
  GameObject* coin = Instantiate(coinPrefab, Vector3(2, 0.5f, 1), Quaternion());   // like Unity: finds the template, clones it, activates the clone
  if (!coin) Debug::Log("rensga: no coin prefab or the pool is full");
}
```

`Instantiate(guid)` also exists without a position. It looks the template up with `World::findByGuid` (the active World) and returns null for a null or unknown Guid.

Instances placed in the scene are plain objects after the cook. Many spawns at run time: reserve pool slots with `"componentReserve": {"MeshRenderer": 32}` in `Project.json`, or use `GameObjectPool<N>` ([scenes-assets-saves.md](scenes-assets-saves.md)). `Instantiate` returns null when a pool is full (and logs which one).

**Coroutines.** C++17 has no `yield`. Chains read almost the same:

```cpp
StartCoroutine(Wait(1.5f).Then([this] { Open(); }).Wait(0.5f).Then([this] { Close(); }));
Invoke(&Coin::Respawn, 2.0f);     // a member function pointer; Invoke("Respawn", 2.0f) also works for a METHOD(Respawn) declaration
```

**Input.** Actions are defined in `ProjectSettings/InputActions.json` (the Input Actions editor): `Input::GetButtonDown("Jump")`, `Input::GetAxis2("Move")` (a `Vector2`), `Input::GetAxis("Horizontal")`. A console has no keyboard: bind to the pad (`south`, `l`, `r`, `nub`, `up`...) and the desktop builds map the keyboard.

**Physics.** `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `Rigidbody` (`AddForce(v, ForceMode::Impulse)`, `velocity()`, `isKinematic`), `CharacterController`. `OnTriggerEnter/Stay/Exit(GameObject*)`, `OnCollisionEnter/Stay/Exit(const Collision&)`. `Physics::Raycast(origin, direction, &hit, maxDistance, mask)`, `SphereCast`, `OverlapSphere`, `LayerMask`. Layers and the collision matrix are in Project Settings. It is deliberately **console-light**: oriented boxes, spheres and capsules, no mesh colliders, no joints, no rotational dynamics. 2D: `Rigidbody2D`, `BoxCollider2D`, `Tilemap`, `Physics2D::Raycast`.

**Rendering.** `MeshRenderer` (with a Material asset: texture, colour, unlit, alpha test), `SpriteRenderer`, `Light` (directional, point), `Camera` (`Camera::main()`, `ScreenToWorldPoint`), `ParticleSystem`, `LineRenderer`, `Gizmos::Draw*` in `OnDrawGizmos`. No shader graph, no per-pixel lighting, no post-processing. The Lighting window can bake light into vertex colours at cook time.

**Animation.** `Animator` with a controller asset (states, transitions, parameters: `SetFloat/SetBool/SetTrigger`, `CrossFade`), property clips (keyframe any reflected field), skeletal clips from glTF, animation events that call script methods.

**Audio.** `AudioSource` (`Play`, `playOnAwake`, `outputGroup`), `AudioSource::PlayClipAtPoint`, mixer groups `AudioMixerGroups::SetVolume`. Voices are limited by the console (the editor shows the count).

**UI.** `Canvas` (scaler), `UIText`, `UIImage`, `UIButton`, `Slider`, `Toggle`, `LayoutGroup`, `EventSystem`. Anchors are the nine Unity-like points. Buttons are focused with the d-pad and pressed with the south button; `onClick` is a `UnityEvent` you wire in the Inspector, or override `OnUIClick()`. `UIText::SetFormat("Score {0}", score)` formats without allocating.

**Scenes.** `SceneManager::LoadScene("Level2")`, `LoadSceneAsync` (with `AsyncOperation` progress for a loading screen), additive loads, `DontDestroyOnLoad(go)`. The scenes in **Build Settings** are what the pack contains.

**Time.** `Time::deltaTime()`, `fixedDeltaTime()`, `timeScale`. Gameplay should be deterministic: the same inputs give the same result on the console and on the PC build.

**Quitting and platform.** `Application::Quit()`, `Application::profile()` (PSX, PSP, PS2), `Budget::` queries for memory.

## 5. What is different, and why

| Topic | Rule | Reason |
| --- | --- | --- |
| Language | C++17, no exceptions, no RTTI, no `dynamic_cast`, no `std::function` in hot code | Console toolchains and size. |
| Numbers | Use **`Scalar`**, never `float` or `double` in game scripts (the API check fails the build) | `Scalar` is `float` on PSP and PS2 and 20.12 fixed point on PSX. The same script then runs on all three. Integer maths with `i32` is always fine. |
| Memory | **No allocation per frame.** No `new`, `std::vector::push_back`, `std::string` concatenation in `Update`. Allocate in `Start` or use fixed arrays. | The consoles have 2 to 32 MB. The runtime logs `alloc-check allocs=0` and the CI fails otherwise. |
| Pointers | Engine pointers are valid for the current call. Store `Ref<T>` handles. | Objects live in fixed pools; slots are reused. |
| Limits | Objects, components per type, triangles, textures, VRAM, voices: set by the **ConsoleTarget** for the project's console. | The cooker and the Scene view validator reject a scene that does not fit, naming the asset. |
| Features | A component or call the console cannot do is greyed out in the Inspector with a reason, rejected by the cooker, and a compile error with a hint if you call it from a script (`RENSGA_REQUIRES_FEATURE`). | One API, honest limits. |
| One console per project | The project targets one console (PSX, PSP or PS2) and is always also built for Windows, Linux and Android. | The data is cooked for that console. |
| Strings | `String32` fields (31 characters), `const char*` parameters, `Format(buf, size, "HP {0}/{1}", a, b)`. | No heap. |
| Printing | `Debug::LogFormat("x={0} y={1:.3}", x, y)` (also `LogWarningFormat`, `LogErrorFormat`). | `Scalar` has no portable `printf` format; `LogFormat` uses `Format` (no heap). |
| Assets | Textures are power-of-two friendly and paletted on consoles, audio is mono 16-bit, meshes are small. The Project window shows the cost of each asset for the target. | VRAM and RAM. |
| Reflection | The Inspector, scene files, prefab overrides, undo, hot reload and the cooker read one description (the macros). | No second hand-written description of a type. |
| No Mono, no C# | Non-goals: C#/.NET, shader graphs, per-pixel lighting, URP/HDRP, DOTS, store integration. | Scope. |

## 6. Editor scripting

Unity's `Editor/` folder and `[CustomEditor]` exist, shaped for C++: put `.cpp` files in `<project>/Editor/`, register with a macro, and the editor compiles and hot-reloads them. They never ship.

| Unity | Rensga |
| --- | --- |
| `[CustomEditor(typeof(Patrol))]` + `OnInspectorGUI` | `RENSGA_CUSTOM_INSPECTOR(Patrol, fn)` (extra widgets) or `RENSGA_CUSTOM_INSPECTOR_REPLACE` (with `DrawDefaultInspector(view)`) |
| `EditorWindow` + `[MenuItem("Window/My")]` | `RENSGA_EDITOR_WINDOW("My", fn)` (ImGui) |
| `[MenuItem("Tools/Do", ...)]` with `%#g` shortcut | `RENSGA_MENU_ITEM_KEY("Tools/Do", "Ctrl+Shift+G", fn)` |
| `Selection.activeGameObject` | `Selection::ActiveGuid()`, `Selection::Guids()` |
| `Undo.RecordObject` + assignment | `ComponentView::Set...`, `SceneEdit::SetField`, `SceneEdit::SetPosition`: every edit is undoable; `SceneEdit::BeginGroup/EndGroup` groups them |
| `AssetDatabase.FindAssets` | `Assets::Find(name, kind)` |
| `Debug.Log` in the editor | `Log::Info/Warning/Error` |
| IMGUI / UIElements | Dear ImGui (`<imgui.h>`) |

The API is small on purpose; see [editor-extensions.md](editor-extensions.md). `samples/extending_editor` has a custom inspector, a window, two menu items and a data asset.

## 7. ScriptableObject becomes DataAsset

```cpp
// Assets/Scripts/EnemyStats.h
struct EnemyStats : DataAsset {
  REFLECT(EnemyStats)
  SERIALIZE_FIELD(i32, health, 30)
  SERIALIZE_FIELD(Scalar, speed, 2.0f)
};
// in a component
SERIALIZE_FIELD(Guid, stats, Guid())
const EnemyStats* data = nullptr;
void Start() override { data = DataAssets::Get<EnemyStats>(stats); }
```

Create it with **Project > Create > Data Asset > EnemyStats**, edit it in the Inspector (undo works), drag it onto the `stats` field. `data` is read-only and valid for the run. Differences: no `Resources.Load` needed (but available), no `CreateAssetMenu` attribute (every data asset type is listed), the instance is read-only at run time, and the size is limited to 512 bytes.

## 8. Habits to unlearn

- **`float` and `double`** in scripts. Write `Scalar` and `1.5f` literals (the check allows `1.5f`; it rejects the word `float`).
- **`Debug.Log($"...")`.** Use printf style with integers and `%s` for `String32::c_str()` or `const char*`.
- **`FindObjectOfType` every frame.** Find in `Start` and keep a `Ref`.
- **Lists that grow.** Use `SERIALIZE_ARRAY` for authored data and fixed arrays (`Foo items[16]; u32 count`) for runtime data, with a clear limit.
- **Quaternion maths from Euler angles in `Update`** is fine, but avoid `sin`/`cos` heavy loops on PSP; the Profiler window shows script time per marker (`Profiler::BeginSample("name")`).
- **Waiting for assets to import.** The editor imports on focus; the cook runs in the build step.

## 9. When something is rejected

- *"Over budget"* in the Scene validator or the cook: the message names the scene or asset and the budget. Open Window > Profiler or the VRAM map.
- *"Missing script (Type)"* in the Inspector: the script module has not compiled or the class was renamed. Check the Console for the compiler error; double-click it.
- *"unknown component type"* from `rensga cook`: the CLI only knows the scripts of the project it was built with unless you build it for your project (F6.05 changes this for projects opened in the editor).
- A feature greyed out in the Inspector with a reason: the console profile lacks it (for example dynamic shadows). The reason comes from the project's `ConsoleTarget.json`.

## 10. Where to go next

- [`samples/extending_editor`](../../samples/extending_editor/README.md): custom inspector, window, menu items and a data asset driving gameplay.
- `templates/psp-3d/Assets/Scripts/Pickup.cpp` and `PlayerController.cpp`: the shortest working trigger pickup and camera-relative movement.
- [`samples/unity_playtest`](../../samples/unity_playtest/README.md) and [`playtest-notes.md`](playtest-notes.md): a small game built following only this guide, and the frictions found.
- [prefabs.md](prefabs.md), [physics.md](physics.md), [animation.md](animation.md), [rendering-api.md](rendering-api.md), [scenes-assets-saves.md](scenes-assets-saves.md), [project-settings.md](project-settings.md), [api-rules.md](api-rules.md).
