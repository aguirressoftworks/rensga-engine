# ProjectSettings schemas (Editor 6, F6)

Files in `<project>/ProjectSettings/`. All are diff-friendly JSON with `"format": 1`. The editor schemas live in `editor/core/include/rensga/editor/project_settings.h` (console-agnostic, no ImGui). A missing file means the defaults. Unknown keys survive a load/save round trip.

| File | Owner (reads at cook / run time) | Edited in |
| --- | --- | --- |
| `Project.json` | cooker, pipeline, CLI (`scenes`, `target`) | Build Settings window |
| `ConsoleTarget.json` | cooker, editor (unchanged) | target module |
| `InputActions.json` | cooker (unchanged) | Input Actions |
| `Physics2D.json` | cooker (unchanged: names + `ignore` by name) | Project Settings > Physics 2D |
| `TagManager.json` | cooker (tag and layer names for the editor; scenes keep tag text and layer index) | Project Settings > Tags and Layers |
| `Physics.json` | cooker -> `PHY3` pack section | Project Settings > Physics |
| `Time.json` | cooker -> `GameSettings` (fixed step) | Project Settings > Time |

## TagManager.json

```json
{ "format": 1,
  "tags": ["Enemy", "Pickup"],
  "layers": ["Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI", "Player", "Enemy", "", ...32 entries] }
```

- `tags`: user tags. `Untagged` is implicit (an object with no tag). Unique, non-empty, at most 63 characters, not `Untagged`.
- `layers`: exactly 32 entries (shorter arrays are padded with `""`). Index = layer number stored in a scene object (`"layer": n`). `""` = unnamed. Layer 0 is always `"Default"`. Names are unique, at most 31 characters.
- Defaults: `Default`, `TransparentFX`, `Ignore Raycast`, unnamed, `Water`, `UI`, the rest unnamed.
- `Physics2D.json` keeps its own layer names and `ignore` pairs (by name). 3D physics and `LayerMask` use `TagManager.json`.

## Physics.json

```json
{ "format": 1,
  "gravity": [0, -9.81, 0],
  "ignore": [[6, 7], [7, 7]],
  "queriesHitTriggers": true }
```

- `gravity`: m/s^2, finite, within +-1000.
- `ignore`: layer-index pairs that do **not** collide. Everything else collides, so an empty list means "all collide". Pairs are stored normalised (`a <= b`), sorted and unique; `[7, 7]` is a layer ignoring itself.
- `queriesHitTriggers`: whether `Raycast`/`OverlapSphere` report trigger colliders by default.
- Cooked form (`PHY3`, see `pack-format.md`): gravity plus one 32-bit mask per layer. `PhysicsSettings::collisionMasks()` computes it: bit `j` of mask `i` is set when layers `i` and `j` collide (symmetric).

## Time.json

```json
{ "format": 1, "fixedTimestep": 0.016666668, "maximumDelta": 0.1, "timeScale": 1.0 }
```

- `fixedTimestep`: seconds per FixedUpdate and physics step, 0.001 to 0.1. The default 1/60 is what the determinism checks use; changing it changes the game's results by design.
- `maximumDelta`: longest frame time given to the accumulator, from `fixedTimestep` up to 1 second.
- `timeScale`: 0 to 100.

## Project.json (build settings)

```json
{ "format": 1,
  "name": "PSP Template",
  "target": "psp",
  "development": false,
  "scenes": ["<guid of an enabled scene>", ...],
  "buildScenes": [ {"guid": "<guid>", "enabled": true}, {"guid": "<guid>", "enabled": false} ] }
```

- `scenes` is what the cooker and the pipeline read today: the **enabled** scenes in build order (the first is the start scene). The editor always rewrites it from `buildScenes`, so no reader had to change.
- `buildScenes` is the editor's full list, disabled entries included. Optional on load: a file with only `scenes` (every older project) loads with all entries enabled.
- `target`: ConsoleTarget id. `development`: development build (profiler hooks, logging; release checks off).
- Other keys are kept verbatim.
