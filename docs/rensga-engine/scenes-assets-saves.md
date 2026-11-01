# Scenes, assets, saves and helpers (API 9 and API 12)

What a script can do about loading scenes, finding assets by name, keeping settings and saves, measuring itself and asking how much room is left. The signatures are in `rensga/Rensga.h` (`SceneManager` is in `rensga/core/scene_manager.h`). Samples: `samples/levels_save` (two levels, an async loading screen, a save that survives a restart) and `samples/pool_spawner` (a pooled and an unpooled projectile spawner measured with profiler samples).

## SceneManager

Scenes are the pack's `SCNE` list, which is the "scenes in build" order. Every call is deferred to the end of the frame, so a script can call it from `Update` or a trigger.

| Call | What happens |
|---|---|
| `LoadScene(name or index)` | Single: every loaded scene is replaced. Objects marked with `DontDestroyOnLoad` survive. Same behaviour as before F6. |
| `LoadScene(.., LoadSceneMode::Additive)` | The scene's objects are created next to the loaded ones. Loading a scene that is already loaded is refused with a log line. |
| `LoadSceneAsync(.., mode)` | Returns an `AsyncOperation` (two integers, no heap). `progress()` runs 0 to 0.9 while loading, 1 when active. With `setAllowSceneActivation(false)` it waits at 0.9 until you allow it. Up to 4 operations are alive; they run one at a time, oldest first. |
| `UnloadSceneAsync(..)` | Destroys the objects the scene made. The last loaded scene cannot be unloaded. |
| `GetActiveScene` / `SetActiveScene` | The active scene is the last Single-loaded one. Unloading the active scene makes the lowest loaded scene active. |
| `addSceneLoaded(fn)` / `addSceneUnloaded(fn)` | Plain function pointers, up to 4 each. `sceneLoaded` runs after `Awake` and `OnEnable` of the new objects and before their `Start`. |

How an async load works:

- **Additive:** the scene is built over several frames, `SceneManager::asyncChunk()` work items per frame (default 32: an object, a parent link or a component each). The new objects are inactive and not awake until the load is finished, so nothing half built is drawn or updated. At 0.9 the scene waits for activation, then the objects get their real active flags and `Awake` / `OnEnable`.
- **Single:** the old scene keeps running while a sweep checks the new scene's components (type known, field blob inside the pack), a chunk per frame. At activation the old scene is destroyed and the new one is created in one frame. This keeps the peak memory at one scene on a console. A failed sweep finishes the operation without activating anything.
- **Memory:** before an additive load starts the world checks the free object slots, the free slots of each component pool it needs, and the arena bytes for new pools. A load that does not fit is refused with a message that names the pool (`component pool 'UIImage' needs 60 slots, 12 free`). Nothing is half loaded. Nothing allocates from the heap during a load.
- **Pool sizing:** pools are sized for the largest scene. To load a HUD or a loading screen next to a level, list the extra scene in `ProjectSettings/Project.json` as `"additiveScenes": [<scene guid>]`. The cooker sets `kSceneAdditive` on those scenes and the pools are sized for the largest ordinary scene plus all of them. Without the key the pools are sized as before.

Instantiate and pools: every component pool is the scene's count plus 8 spare slots. A game that clones many objects (a bullet pool, `GameObjectPool<96>`) adds `"componentReserve": {"MeshRenderer": 96}` to `ProjectSettings/Project.json`; the cooker writes a `PCAP` record and the pool of that type grows by that many slots (`pool_spawner` does this). Without it `Instantiate` fails with `component pool 'MeshRenderer' is full`.

`GameObject::sceneIndex` says which scene made an object (-1 for objects made at run time and for `DontDestroyOnLoad` roots).

## Resources

`Resources::Load<T>("sfx/jump")` finds an asset below any folder named `Resources` in `Assets/`, by its path without the extension, matched without case. The cooker writes the names into the `RSRC` section (see `pack-format.md`).

```cpp
Handle<ResAudioClip> h = Resources::Load<ResAudioClip>("sfx/jump");   // texture, audio clip, material, mesh, font, prefab, ...
if (h.valid()) /* h.guid is the asset's GUID: use it where a Guid field is expected */;
Handle<EnemyStats> orc = Resources::Load<EnemyStats>("data/orc");      // any other T: a data asset of that reflected type
int hp = orc->hp;
Resources::Unload(h);                                                  // once per Load
```

A handle is `{guid, kind, slot}`. Loading the same asset twice shares one slot and counts references; `Unload` drops one and clears the handle you pass. 32 slots. Textures, audio and the other pack kinds stay in the pack for the whole run (the pack is in RAM); only a data asset has an instance of its own, built from its `DATA` record with `readBlob` (at most 256 bytes) and destroyed with the last reference. `Resources::loadedCount()` and `entryCount()` are there for debug overlays.

## PlayerPrefs

Unity's key/value store for settings. `SetInt`, `SetFloat`, `SetString`, the matching getters with a default, `HasKey`, `DeleteKey`, `DeleteAll`, `Save`. At most 64 keys of at most 31 characters, strings of at most 63. The table is read from the save store on first use and written by `Save()` and when the game quits (`Application::Quit`, or a runtime main calling `Application::runQuitHooks()` on a clean exit; the mains do). It is one store file named `prefs`, so it lives wherever the platform keeps saves.

## SaveSystem

Versioned slots on top of the save store, for saves that outlive a game update.

```cpp
struct Save2 { i32 level, coins, lives; };                               // version 2
SaveSystem::Metadata m; snprintf(m.title, 32, "FOREST"); snprintf(m.detail, 32, "LEVEL 2");
SaveSystem::Write(0, 2, &data, sizeof data, &m);
i32 n = SaveSystem::Read(0, 2, &data, sizeof data, &migrate);            // migrate upgrades version 1 files
```

- 8 slots, payload up to `SaveSystem::kMaxPayload` (16 KB minus 128 bytes). `Read` returns the payload size, or -1 when the slot is empty, damaged, written by a newer version, too big for the buffer, or older with no working migration.
- A slot is the store file `slotN` with a 96-byte header (magic, version, size, title, detail, icon GUID) inside the store's own size and CRC wrapper, so a torn write reads as "no save". `Delete` writes a "deleted" header (the store has no remove); `Exists` and `Read` treat it as empty.
- Where it goes: files under the save root on Linux, Windows and Android; the memory card `mc0:/RENSGA/slotN` on a PS2 (the folder has the game's `icon.sys` and icon); ms0 files on a PSP, or `SAVEDATA/<GAMEID>SLOTN` with the system dialog when the game calls `Save::setMode(SaveMode::SystemDialog)`. `title` and `detail` are handed to the platform (`IPlatform::setSaveMetadata`) before the write; the PSP uses them in the `PARAM.SFO` of that save. The PS2 card shows the game's icon and title for the whole folder; per-slot titles are inside the slot file and come back through `GetInfo`.
- `GetInfo(slot, &info)` fills `exists`, `version`, `size` and the metadata without needing the payload type.

## Helpers (API 12)

| API | Notes |
|---|---|
| `ObjectPool<T, N>` | `N` objects inside the pool, `Get()` returns a default-constructed `T` or null, `Release(p)`. No heap. |
| `GameObjectPool<N>` | `Init(template)` makes up to `N` inactive clones once; `Get(pos, rot)` activates one, `Release(go)` deactivates it. Returns null when all are in use (it never calls `Instantiate` behind your back). Avoids `Instantiate`/`Destroy` churn in the object and component pools. |
| `Profiler::BeginSample("name")` / `EndSample()` | Names must be string literals. Up to 32 names, 8 nested. The runtime prints one `rensga: prof sample name=<n> frames=<F> calls=<C> avg_us=<per frame> max_us=<worst frame>` line per name every 60 frames, next to the usual `rensga: prof frames=...` line. `RENSGA_PROFILE_SCOPE("name")` ends the sample at the end of the block and compiles away with `RENSGA_RELEASE_BUILD`. `Profiler::LastSampleMicros("name")` is for in-game overlays. On PPSSPP and PCSX2 the microseconds are emulator time. |
| `Budget::memoryUsed/Limit`, `vramUsed/Limit`, `trianglesLastFrame/Limit`, `voicesUsed/Limit`, `percentUsed` | Limits come from the ConsoleTarget record in the pack; used figures from the arena and from counters the runtime keeps. 0 means unknown. |
| `Application::platform()` | `LinuxPlayer`, `WindowsPlayer`, `Android`, `Psp`, `Ps2`, `Psx` or `Headless`. |
| `Application::profile()` | `Psp`, `Ps2`, `Psx` or `Unknown`: the profile the project was cooked for, also on its Windows, Linux and Android builds. The runtime sets it from the pack's ConsoleTarget. |

Compile-time switches a project build sees through `console_target.gen.h`: `ENGINE_PROFILE_PSP` / `ENGINE_PROFILE_PS2` / `ENGINE_PROFILE_PSX` (1 for the project's profile), `ENGINE_SCALAR_FLOAT` (or the fixed-point twin on PSX), `ENGINE_FEATURE_<NAME>` for each feature the profile has (`LIGHTING`, `SKINNING`, `FOG`, `OSK`, `SAVEDIALOG`, `BATTERY`, `CPUCLOCK`, `TEXTURESWIZZLE`), and the `console_target::k...` constants (screen size, RAM, VRAM, triangles per frame, voices). The engine's own `RENSGA_RELEASE_BUILD` turns script profiler scopes off. Game code should still prefer the run-time queries above over `#ifdef` (rule: consoles differ by data, not by code paths); the defines exist so a call to a feature the profile lacks becomes a compile error with a hint.
