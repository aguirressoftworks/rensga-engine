# Editor Tour

A small PSP-profile project (`ProjectSettings` as in `templates/psp-3d`) that backs the runnable F3 exercises. Open it with
`rensga-editor --project samples/editor_tour`. It has no art: every object is a primitive.

| Scene | Shows | Exercise |
| --- | --- | --- |
| `Tour` | `Spinner` (cube, `Rotator` script with an exposed `speed`) with a nested `Orbiter` > `Moon` | Editor 2 save and reopen a scene, Editor 4 hot-reload a Rotator, Editor 5 play and revert |
| `Door` | `Door` (coroutine, opens over `openSeconds`, then fires the `OnOpened` UnityEvent) and `Bell` (`DoorBell.Ring`) | API 4 coroutine door |
| `Patrol` | `Patrol` script drawing waypoints with `OnDrawGizmos` / `OnDrawGizmosSelected` | API 11 patrol script with gizmos |

The Editor 3 exercise (import a texture) runs on `samples/platformer2d`, and the API 5 input test scene is its `InputTest` scene.

## Run the exercises

Each exercise is a ctest with the label `f3_exercises`. It copies the project to a temp dir, drives the real editor or player under
xvfb with `--ui-event` verbs, checks the log and the saved files, and leaves a screenshot in `build/<dir>/tests/editor/<test>/`.

```bash
cmake --preset linux-host -B build/host && cmake --build build/host -j2
ctest --test-dir build/host -L f3_exercises --output-on-failure
```

| Test | What it checks |
| --- | --- |
| `exercise_scene_roundtrip` | five-level nesting, save, reopen, save again: the files are byte-identical |
| `exercise_hot_reload` | set `speed` while playing, edit the rotation formula, background build and reload: rotation and `speed` kept, new axis active |
| `exercise_play_revert` | pause, step exactly 30 frames, edit and move during play, Stop: scene file and document unchanged |
| `exercise_door` | wire `OnOpened` to `DoorBell.Ring` (`setfield`), save, play: half open at 1 s, rang exactly once at 2 s |
| `editor_assets_ui` | drop a PNG, auto-import, change the ps2 palette depth (Editor 3) |
| `exercise_input_desktop` | `InputTest` on `rensga_desktop` with injected pad and key events; builds a project build tree once (`-DRENSGA_EXERCISE_SAMPLE_BUILD=<dir>` reuses one) |
| `exercise_patrol` | the Patrol gizmo lines show in the scene view |

## Console side (needs Docker and PPSSPP)

`tools/ci/editor_tour.sh` cooks the project, runs `rensga play --target psp` (heartbeats, `alloc-check allocs=0`) and
`rensga build --target psp` (an `EBOOT.PBP` in `Library/Build/psp-release/`). By hand:

```bash
cmake --preset linux-host -B build/tour -DRENSGA_PROJECT_DIR=$PWD/samples/editor_tour && cmake --build build/tour -j2 --target rensga
xvfb-run -a build/tour/tools/cli/rensga play --target psp samples/editor_tour --json --quit-after 25
build/tour/tools/cli/rensga build --target psp samples/editor_tour
```

The CLI only knows the script types of the project it was built for (an MVP limit in TASKLIST.md), hence the extra build tree.
