# Rensga Engine — Task list

Every task from an empty repository to a shipped 1.0, in two steps:

1. **MVP.** A prototype 3D PSP game that boots in PPSSPP from both the editor's Play button and the `rensga play` command. It is built today, and it leaves every hook the full suite plugs into.
2. **Full suite, 0 → 100%.** Every phase of the 13 roadmaps, grouped into milestones, with progress measured in percent.

Sources: [design document](docs/rensga-engine/design-doc/rensga-engine-design-doc.md), [roadmaps](docs/rensga-engine/index.html), [RULES.md](RULES.md).

## How to use this list

- Tick a box (`- [x]`) when the task's "done when" is true. Don't tick partial work: add a note under the task instead.
- Task IDs (`MVP-07`, `F5.12`) are stable. Never renumber them. New tasks go at the end of their block with the next free number.
- `Engine 3`, `PSP Editor 5` and similar references mean phase 3 of the Engine roadmap and so on, as in the design doc. The roadmap page has the scope, topics and Claude prompts for that phase.
- **Percentages.** Each Step 2 task carries its share of the whole project, taken from its roadmap estimate (`estimate / 237.5 weeks`). The weeks are human part-time effort and are used only as relative weights, not as a schedule. Progress = sum of ticked percentages.
- **Gates** carry no weight but must be ticked before their milestone counts as closed.
- Legend: 🔶 the MVP gives this task a head start (it still counts only when the whole phase is done) · ⏭ optional, not counted.
- If a roadmap phase is added, removed or re-estimated, update this file in the same change (see AGENTS.md).

## Progress

| Step / milestone | Weight | Cumulative | Status |
| --- | --- | --- | --- |
| **Step 1 · MVP** | own 0–100% | — | 100%: all tasks done and accepted (clean build, play + editor Play + build, screenshot); S7 editor usability (MVP-45..57) done after the review, 13/13 tests |
| F1 · Foundations (harden the MVP) | 4.8% | 4.8% | 0% |
| F2 · Shared core | 9.3% | 14.1% | 0% |
| F3 · Editor core | 9.3% | 23.4% | 0% |
| F4 · 2D slice everywhere + PSP base | 8.0% | 31.4% | 0% |
| F5 · PSP track | 16.4% | 47.8% | 0% |
| F6 · Full editor and API | 16.2% | 64.0% | 0% |
| F7 · PSP template | 2.3% | 66.3% | 0% |
| F8 · PS2 track | 7.4% | 73.7% | 0% |
| F9 · PSX track | 24.2% | 97.9% | 0% |
| F10 · Ship 1.0 | 2.1% | 100.0% | 0% |
| **Step 2 total** | **100%** | | **0%** |

---

## Step 1 · MVP: PSP prototype from the editor and the CLI

### Goal

On Linux, both of these build the PSP template project inside the pinned pspdev Docker image and boot it in the bundled, pinned PPSSPP:

- `rensga play --target psp templates/psp`
- **Play** in the minimal editor

In the game, a primitive capsule character moves with the analog nub around a primitive test level. It has a follow camera, gravity, and a jump that lands on boxes, and it collects 5 pickups shown as HUD pips. **Stop** closes PPSSPP. The game's log appears in the editor's Console.

### Ground rules

- Use the final architecture, in thin form. Every MVP shortcut sits behind the interface the full suite keeps, so phase 2 fills in implementations and never rewires callers.
- Follow RULES.md from day one: data-driven, editor core never names a console, no per-frame heap allocation, GUID references, `Scalar` in game code.
- Any deviation from the design doc needs a written reason in the [hook register](#mvp-hook-register) below.

### S0 · Repository and toolchains — 0 → 10%

- [x] **MVP-01** `git init`; `.gitignore` covering `Library/`, `build/`, `out/` and emulator binaries; MIT `LICENSE`; `THIRD_PARTY.md` listing every dependency with license and source link.
- [x] **MVP-02** Top-level `CMakeLists.txt` with the planned layout (`engine/`, `editor/`, `tools/`, `templates/`, `third_party/`). C++17. Runtime targets build with no exceptions and no RTTI. Warnings are errors.
- [x] **MVP-03** `CMakePresets.json` with working `psp` and `linux-host` presets (`linux-host` builds tests, cooker, pipeline, CLI and editor). `windows`, `android`, `ps2` and `psx` are declared but disabled, as slots for F1 and later.
- [x] **MVP-04** Pin the `pspdev/pspdev` Docker image by digest. `tools/docker/psp.sh` runs any command inside it with the repo mounted.
- [x] **MVP-05** `tools/emulators/fetch_ppsspp.sh` downloads or builds a pinned PPSSPP into `third_party/emulators/ppsspp/`. The binary is git-ignored; `VERSION`, `LICENSE` and the source link are committed. PPSSPP is GPL, so it only ever runs as a separate process.
- [x] **MVP-06** Smoke test: a clear-screen ELF from the `psp` preset boots in PPSSPP, launched by hand.

### S1 · Core and interfaces — 10 → 30%

- [x] **MVP-07** `engine/core/log`: channels, levels and sinks (stdout, file). One sink later feeds the editor Console.
- [x] **MVP-08** `engine/core/memory`: arena and frame allocators sized from the ConsoleTarget budget, with 16-byte alignment. A debug counter fails the frame if anything allocates on the heap during it.
- [x] **MVP-09** `engine/core/math`: `Scalar` chosen by profile (`float` on PSP; the fixed-point slot is reserved), plus Vector2, Vector3, Quaternion, Matrix4 and a Mathf subset.
- [x] **MVP-10** The four backend interfaces: `IPlatform`, `IRenderer`, `IAudioBackend`, `IFileSystem`. `IRenderer` draws a mesh with a material and a **sort key**, and draws 2D overlay quads. `null` implementations of all four for headless tests.
- [x] **MVP-11** Fixed-timestep loop at 60 Hz with an interpolation factor. Each frame calls FixedUpdate, Update, then LateUpdate.
- [x] **MVP-12** Object model storage: GameObject IDs, Transform hierarchy, fixed-capacity contiguous component arrays sized from scene data, and a generational `Ref<T>` that reads as null after destroy.
- [x] **MVP-13** Reflection: `REFLECT`, `SERIALIZE_FIELD`, `RANGE` and `HEADER` macros that register field descriptors at static init. JSON load/save, the cooker and the inspector all read them. Editor 1 later swaps registration for codegen without changing the macros.
- [x] **MVP-14** `Guid` type (128-bit, text form). Every asset and object reference uses it.
- [x] **MVP-15** Host unit tests (`linux-host`, null backends) for math, allocators, `Ref<T>` after destroy, reflection round-trip, and lifecycle order.

### S2 · Data and cooker — 30 → 45%

- [x] **MVP-16** ConsoleTarget JSON schema and `templates/psp/ProjectSettings/ConsoleTarget.json`: profile `psp`, scalar `float`, 480x272, budgets for RAM (32 MB PSP-1000), VRAM (2 MB) and triangles per frame, plus allowed components. A generator emits a constants header, and `ENGINE_PROFILE_PSP` is defined.
- [x] **MVP-17** Project layout: `Assets/`, `ProjectSettings/`, and `Library/` (cache, never committed). A `.meta` with a GUID next to every asset. A project format version field.
- [x] **MVP-18** Scene JSON: GameObjects, Transform, components with reflected fields, references by GUID, and a prefab field (reserved, empty). Also `InputActions.json`: Move → nub, Jump → south button, Camera orbit → L/R.
- [x] **MVP-19** Shared primitive generator: cube, sphere, capsule, plane and ramp, with tessellation from the ConsoleTarget and matching colliders.
- [x] **MVP-20** `tools/cooker`: a shared core (loads scenes and assets through reflection, recooks only changed assets by content hash) plus an `ICookerFormatPlugin` interface. PSP plugin: meshes to GU vertex format, with vertex colors and baked directional shading.
- [x] **MVP-21** Pack format v1: magic, version, table of contents, 16-byte aligned sections. A 2048-byte sector alignment option is reserved for disc targets. Spec written to `docs/rensga-engine/pack-format.md`.
- [x] **MVP-22** Budget validator: pack size against the RAM budget, triangles against the budget, object counts against component capacity. Errors name the asset and its GUID and give a non-zero exit code.

### S3 · PSP runtime — 45 → 65%

- [x] **MVP-23** `engine/backends/platform/psp`: module info, exit callback (Home quits cleanly), CPU at 333 MHz, sceCtrl analog and buttons mapped to the virtual gamepad, timer. *If SDL2 alongside GU costs time today, use native calls and record the reason; PSP Engine 1 moves it to SDL2 as designed.*
- [x] **MVP-24** PSP `IFileSystem`: `host0:` for Play, with `ms0:` and `umd0:` path slots reserved. Loads the pack into the arena.
- [x] **MVP-25** `engine/backends/render/gu`: two framebuffers and depth in VRAM, one display list per frame, perspective camera, cooked meshes drawn in sort-key order, vsync.
- [x] **MVP-26** Overlay pass: screen-space quads for the HUD, through the same `IRenderer`.
- [x] **MVP-27** Pack loader: maps the pack into the component arrays with no parsing. Refuses a wrong pack version with a log message.
- [x] **MVP-28** PSP log sink: stdout plus `host0:/rensga.log`. An assert writes its message before exiting.

### S4 · API and game — 65 → 80%

- [x] **MVP-29** `engine/api/Rensga.h` umbrella header:
  - `Behaviour` with Awake, Start, FixedUpdate, Update and LateUpdate in Unity order; OnEnable, OnDisable and OnDestroy called on load and unload
  - `GetComponent<T>`
  - `Transform`: position, rotation, `Translate`, `Rotate`, `forward`
  - `Time::deltaTime` and `Time::fixedDeltaTime`
  - `Input::GetAxis`, `GetButton` and `GetButtonDown` by action name
  - `Debug::Log`
- [x] **MVP-30** Minimal physics: `CharacterController::Move` with gravity, AABB collision against static box colliders and a grounded flag. Sphere triggers with `OnTriggerEnter`.
- [x] **MVP-31** Script registration: a `REFLECT`ed Behaviour gets a factory, so scenes instantiate scripts by type name. Project scripts link statically into the PSP build.
- [x] **MVP-32** Template scene `templates/psp/Assets/Scenes/TestLevel.scene`: ground plane, 4–6 box platforms and a ramp, a capsule player with a sphere head, 5 pickups, a camera and a directional light setting.
- [x] **MVP-33** Template scripts in `templates/psp/Assets/Scripts/`, using `Scalar` only (never `float`):
  - `PlayerController`: camera-relative nub movement, jump
  - `FollowCamera`: follows the player, orbits on L/R
  - `Pickup`: spins, collected on trigger
  - `PickupCounter`: HUD pips plus a log line

### S5 · Pipeline, CLI and emulator — 80 → 90%

- [x] **MVP-34** `tools/pipeline` library, shared by the CLI and the editor:
  - `BuildRequest` with project, target, and `dev` or `release`
  - a stage interface, run in order: Validate → Cook → Compile → Package → Launch
  - progress events (stage, status, message)
- [x] **MVP-35** Compile stage: configures and builds the `psp` preset in Docker with the project's scripts and cooked pack. Output goes to `Library/Build/psp-dev/`.
- [x] **MVP-36** Package stage. `dev` skips packaging: the ELF boots directly and assets load from `host0:`. `release` (`rensga build`) produces PRX, PARAM.SFO and EBOOT.PBP with a placeholder ICON0.
- [x] **MVP-37** `EmulatorLauncher` interface: direct-boot command line, a per-project config folder at `Library/Emulators/ppsspp/`, a BIOS policy (none for PPSSPP), start, stop, is-running, and log capture (process output plus a tail of `rensga.log`). Implement it for PPSSPP, and check its command-line flags against the pinned version.
- [x] **MVP-38** `rensga` CLI: `validate`, `cook`, `build` and `play`, with `--target psp` and `--json` (one JSON event per line, for the editor, CI and agents). It is a thin wrapper over `tools/pipeline`.

### S6 · Minimal editor — 90 → 100%

- [x] **MVP-39** `editor/core` shell: SDL2, OpenGL 3.3 and Dear ImGui (docking branch, pinned); open-project dialog; saved layout.
- [x] **MVP-40** `TargetModule` interface in `editor/core`: importers, cooker plugin, preview rules, inspector rules, validator, packager and emulator launcher. `editor/targets/psp` registers PSP. `tools/check_editor_core.sh` fails if `editor/core` names a console.
- [x] **MVP-41** Panels:
  - Hierarchy: read-only tree from the scene
  - Inspector: read-only, drawn from reflection
  - Console: editor log plus the game log from the launcher, with level filters
- [x] **MVP-42** Toolbar:
  - Play and Stop run the pipeline library on a worker thread.
  - The status shows Building → Launching → Running → Stopped.
  - A dropdown next to Play has "Play on console" active and "Play in editor" greyed out, with the reason "arrives in Editor 5".
  - Validator errors appear in the Console with the asset named.
- [x] **MVP-43** Acceptance, on a clean clone:
  - run `fetch_ppsspp.sh`, then `rensga play --target psp templates/psp`
  - press editor **Play**

  Both must boot the game. The nub moves the character, jump lands on platforms, the pickups count to 5, Stop closes PPSSPP, and the Console shows the game log. Save a screenshot to `docs/rensga-engine/mvp/`.
- [x] **MVP-44** Close-out:
  - Record every MVP deviation and its reason in the hook register.
  - Put the real build, test and run commands in AGENTS.md.
  - Tick the 🔶 head-start notes into the Step 2 tasks they affect.

### S7 · Editor usability (added after the MVP review)

The MVP review found three blockers: Dear ImGui default look, loose overlapping panels, no Scene view. This stage fixes them (full basic editing, Unity-style).

- [x] **MVP-45** Editor brand theme: colors from the embedded `palette.json` (semantic tokens only, gold = Play), Inter and JetBrains Mono, pequi icon.
- [x] **MVP-46** Unity default layout: menu bar, toolbar strip, Hierarchy / Inspector / Project / Console / Scene / Game docked, saved per project in `Library/Editor/layout.ini`; Window > Reset Layout.
- [x] **MVP-47** `SceneDocument` in `editor/core`: load and save (ordered JSON, key order kept), selection, dirty flag, undo and redo, edit operations.
- [x] **MVP-48** Scene view: GL scene renderer into an FBO, Unity-style camera (fly, orbit, pan, zoom, frame), grid, orientation cube.
- [x] **MVP-49** Scene editing: click picking, Move / Rotate / Scale gizmos (ImGuizmo), one undo step per drag.
- [x] **MVP-50** Game tab: preview at the target's resolution (`previewRules()`) through the scene's Camera and light.
- [x] **MVP-51** Inspector editing via reflection: name, active, Transform (Euler degrees), every component field with a widget (slider for RANGE, combo for Guid), HEADER labels, one undo step per edit; Add Component lists registered types (those outside the target's allowed list are disabled with the reason); Remove Component from the header context menu.
- [x] **MVP-52** Hierarchy editing: drag-and-drop reparent, F2 / double-click rename, context menu and GameObject menu (Create Empty, 3D Object > Cube / Sphere / Capsule / Plane / Ramp, Duplicate, Delete), Ctrl+D / Delete. New primitives get MeshRenderer and a collider and appear 5 units in front of the Scene camera.
- [x] **MVP-53** Play integration and close-out: Play saves a dirty scene first (Console line), so edits reach the build. Checked end to end on a scratch copy of `templates/psp`: a test moves a pickup and adds a cube, saves; `rensga cook` and `rensga_headless --pack` load 22 objects (template: 21). `rensga-editor --play` exits non-zero when the pipeline fails. Screenshot tooling: `--screenshot`, `--select`, `--focus`, `--ui-event`.
- [x] **MVP-54** Assign scripts: the Project window lists `Assets/` and shows scripts by class name (file name = class name). Drag a script onto a Hierarchy row, onto an object in the Scene view (pick under the cursor) or onto the Inspector to add that Behaviour (one undo step; the class is also added to the project's `allowedComponents` so the cook accepts it). Add Component has a Scripts submenu; scripts on disk but not yet compiled into this editor are greyed with "compiling..." or "needs rebuild". Double-click opens the file in `$VISUAL`, `$EDITOR` or `xdg-open`.
- [x] **MVP-55** Create objects from the Scene view: a right-click without drag opens the Hierarchy create menu (Create Empty, 3D Object, Camera, Light); the object lands where the cursor ray hits the scene or the ground, else 5 units ahead, and is selected. Right-drag stays fly-look.
- [x] **MVP-56** Project window > Create > C++ Script: name prompt (C++ identifier, unique), writes `Assets/Scripts/<Name>.cpp` from a template (Behaviour, REFLECT, HEADER, `SERIALIZE_FIELD(Scalar, speed, 1.0f)`, Start, Update) and its `.meta` with a new GUID.
- [x] **MVP-57** Auto rebuild like Unity: on window focus gain, on project open and before Play, the editor hashes `Assets/Scripts/*.{cpp,h}` against `Library/Editor/scripts.hash` (written after the last good build). If changed it rebuilds `rensga-editor`, `rensga`, `rensga-cooker` and `rensga_headless` in the background (reconfiguring its own build dir for the project if needed), shows progress in the toolbar, and streams the compiler output to the Console with errors as `file:line` at Error level. On success it saves `Library/Editor/reload_state.json` (unsaved scene incl. dirty flag, selection, Scene camera, focused tab), re-executes itself with `--restore-state` and restores. On failure there is no restart; Play is disabled with "Fix script compile errors first" until the files change and the editor regains focus. Play with changed scripts waits for the rebuild and resumes after the restart. Flags: `--no-auto-rebuild`, `--auto-rebuild` (off by default in `--screenshot` runs).
- [x] **Fix** The target shows as "PSP" in the toolbar, not "PlayStation Portable" (RULES §5: no Sony marks in the UI).

### MVP hook register

Each MVP shortcut, where it lives, and the Step 2 task that finishes it. Add a row for any new shortcut.

| Hook / endpoint | Lives in | MVP state | Finished by |
| --- | --- | --- | --- |
| `IPlatform`, `IFileSystem` | `engine/core`, `backends/platform/psp` | PSP + null | **F2.01**&nbsp;Engine 1, **F4.03**&nbsp;PSP Engine 1 |
| `IRenderer` with sort keys | `engine/core`, `backends/render/gu` | GU 3D + overlay quads, null | **F2.02**&nbsp;Engine 2, **F5.01**&nbsp;PSP Engine 6, **F9.04**&nbsp;PSX Engine 3 |
| `IAudioBackend` | `engine/core` | null only | **F2.04**&nbsp;Engine 4, **F9.06**&nbsp;PSX Engine 5 |
| `Scalar` | `engine/core/math` | `float` | **F9.02**&nbsp;PSX Engine 1 (20.12 fixed-point) |
| Reflection macros | `engine/core/reflect` | static-init registration | **F1.05**&nbsp;Editor 1 (header scanner codegen, undo) |
| `Ref<T>`, object model | `engine/core` | GameObject, Transform, GetComponent | **F2.07**&nbsp;API 2 |
| Lifecycle | `engine/api` | Awake, Start, FixedUpdate, Update, LateUpdate | **F2.08**&nbsp;API 3 |
| Input actions | `InputActions.json`, `engine/api` | Move, Jump, Camera | **F3.06**&nbsp;API 5, **F6.04**&nbsp;Editor 10 |
| Physics | `engine/api` | CharacterController, box colliders, sphere triggers | **F6.08**&nbsp;API 6 |
| ConsoleTarget + profile defines | `ProjectSettings/`, generated header | PSP profile | **F1.02**&nbsp;API 0, **F5.12**&nbsp;PSP API 0 |
| Project layout, `.meta`, `Library/` | templates, cooker | layout and GUIDs; no asset database | **F3.02**&nbsp;Editor 3 |
| Pack format | `tools/cooker`, runtime loader | v1 | **F2.03**&nbsp;Engine 3 |
| `ICookerFormatPlugin` | `tools/cooker` | PSP meshes only | **F4.05**&nbsp;PSP Engine 3, **F9.05**&nbsp;PSX Engine 4 |
| Primitive generator | shared lib | 5 shapes | **F7.01**&nbsp;PSP Template 0 |
| Budget validator | `tools/pipeline` | RAM, triangles, capacity | **F6.05**&nbsp;Editor 11 |
| Pipeline stages | `tools/pipeline` | dev skips Package | **F6.05**&nbsp;Editor 11, **F5.21**&nbsp;Emulator Play 2 |
| `EmulatorLauncher` | `tools/pipeline` | PPSSPP; log via file tail | **F5.19**&nbsp;Emulator Play 0, **F5.22**&nbsp;Emulator Play 3 |
| EBOOT packaging | `editor/targets/psp` packager | placeholder icon | **F5.03**&nbsp;PSP Engine 8 |
| `TargetModule` | `editor/core`, `editor/targets/psp` | PSP only | **F1.04**&nbsp;Editor 0.5, **F5.04**&nbsp;PSP Editor 0 |
| Inspector | `editor/app`, `editor/core` | reflection-driven editing, Add / Remove Component; no prefabs, multi-edit or custom drawers | **F3.01**&nbsp;Editor 2 |
| GL scene renderer | `editor/app/render` | OpenGL 3.3, vertex color x lambert, no textures | **F2.02**&nbsp;Engine 2, **F8.01**&nbsp;Engine 7 (matching GL look) |
| `SceneDocument` undo | `editor/core` | JSON snapshots, 200 steps | **F1.05**&nbsp;Editor 1 |
| Scene editing | `editor/app/panels` | pick, gizmos, hierarchy and inspector edits | **F3.01**&nbsp;Editor 2 |
| Game tab preview | `editor/app/panels/game_view.cpp` | GL approximation at the target resolution | **F3.04**&nbsp;Editor 5, **F5.07**&nbsp;PSP Editor 3 (preview look) |
| Editor theme | `editor/app/theme.cpp` | built from `brand/palette.json` at configure time | **F6.12**&nbsp;Editor 13 polish |
| Play dropdown | `editor/core` | "Play in editor" greyed out | **F3.04**&nbsp;Editor 5 |
| CLI `--json` events | `tools/cli` | `validate`, `cook`, `build`, `play` | **F6.05**&nbsp;Editor 11 and CI |
| Script rebuild + restart | `editor/app/panels/scripts.cpp`, `editor/core/script_build` | rebuilds the editor build tree and re-executes the editor with its state restored | **F3.03**&nbsp;Editor 4 (hot reload) |
| Create C++ Script | `editor/core/script_assets` | fixed template, `Assets/Scripts/<Name>.cpp` | **F3.03**&nbsp;Editor 4 |
| Script drag-and-drop | `editor/app/panels` | Project to Hierarchy, Scene view, Inspector; scripts found by regex scan for `REFLECT` | **F3.01**&nbsp;Editor 2, **F3.02**&nbsp;Editor 3 (asset database) |
| Template project | `templates/psp` | test level and controller | **F7.04**&nbsp;PSP Template 3 |
| CMake presets | `CMakePresets.json` | `psp`, `linux-host`; others declared | **F1.01**&nbsp;Engine 0 |

Recorded deviations:

- **Native PSP platform instead of SDL2.** The PSP backend uses sceCtrl/sceDisplay directly (pspsdk); SDL2 stays on the editor and host side. Reason: pspdev does ship SDL2, but running SDL2 input/timing alongside raw GU rendering was deferred to keep the MVP to one day; `IPlatform` isolates it, and PSP Engine 1 (F4.03) moves the platform layer to SDL2 as the design doc specifies.
- **Project scripts are compiled into the cooker, CLI and editor at build time** (`RENSGA_PROJECT_DIR`, `rensga_cooker_scripts`). Reason: reflection registers types at static init, so scenes can only be read by an executable that links the scripts. Step 2 replaces this with the header-scanner codegen (F1.05).
- **Project scripts are sources of the runtime executables, not a static library.** Reason: static registrars would otherwise be dropped by the linker.
- **Dev Play boots the ELF directly with assets from `host0:`;** the release PRX/EBOOT is only built by `rensga build` (placeholder ICON0).
- **PPSSPP is the distro package (`/usr/bin/PPSSPPSDL`) found by `fetch_ppsspp.sh`, never linked.** Reason: GPL, separate process only.
- **MVP-43 screenshot comes from the game, not a window grab.** PPSSPP's GL window reads black to xwd/import/ffmpeg under Xvfb and PPSSPPHeadless cannot compare/emit screenshots. `tools/emulators/screenshot.sh` builds the ELF with `RENSGA_EXIT_AFTER_FRAMES` (dumps the displayed frame to `host0:/rensga_shot.bmp` on exit), runs it in PPSSPPHeadless (software renderer) and converts it to `docs/rensga-engine/mvp/psp-template.png`.
- **Script changes rebuild the editor and restart it.** Scripts compile into the editor, CLI, cooker and headless runtime at build time (see the entry above), so there is no hot reload. After a change the editor runs CMake on its own build tree and re-executes itself with the scene, selection, camera and tab restored; undo history is not kept across the restart. Finished by Editor 4 hot reload (F3.03).
- **Editor exit code** (fixed in MVP-53): `rensga-editor --play` now exits 1 when the pipeline fails or cannot start.
- **Bugs found in the user's play-testing of the MVP** (all fixed; none was caught by acceptance):
  - PSP imports got their NIDs from link order, so the game booted to a black screen in PPSSPP.
  - The 2-triangle ground plane was dropped by the GU; the plane primitive now takes `cellSize` and the cooker subdivides it.
  - Strafe and camera orbit were mirrored; the template now takes the right axis from the camera.
- **Open gap: acceptance never ran PPSSPP's GPU renderer.** MVP-43 used PPSSPPHeadless (software renderer), which hid the three bugs above. A GPU-renderer run (windowed or a GL-capable headless) must join the acceptance checks in F5.

---

## Step 2 · Full suite: 0 → 100%

Milestones follow the design doc's build order: shared core → editor and TargetModule → PSP → PS2 → PSX → ship. Milestones can overlap once their inputs exist. The percentages measure how much is done, not when.

### F1 · Foundations (harden the MVP) — 4.8% (cumulative 4.8%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F1.01** Engine 0 · Toolchain and build matrix — one CMake repo, four targets, CI green · `0.84%` 🔶
- [x] **F1.02** API 0 · API design rules — naming, handles, ownership, profile gating · `0.63%` 🔶
  - Rules: `docs/rensga-engine/api-rules.md`; `tools/check_api_rules.sh` in ctest. Audit notes: F2.08 makes `Time::*` getters; F2.07 adds `Transform` local getters/setters and the `RENSGA_REQUIRES_FEATURE` / `RENSGA_API_VERSION_*` macros.
- [ ] **F1.03** Editor 0 · Editor shell and projects — docking UI, project folders, console · `0.84%` 🔶
- [x] **F1.04** Editor 0.5 · TargetModule interface — console plug-in boundary · `0.84%` 🔶
  - Reviewed: preview and inspector rules are now data from ConsoleTarget.json; `docs/rensga-engine/target-module.md`; test-only `editor/targets/dummy`. Left for F5.04: the cooker CLI default `--target psp` and its PSP plugin registration.
- [ ] **F1.05** Editor 1 · Reflection, serialization and undo — the backbone everything else needs · `1.69%` 🔶

**Gates**

- [ ] **F1.G1** CI builds every enabled preset on every push.
- [ ] **F1.G2** `tools/check_editor_core.sh` runs in CI.
- [x] **F1.G3** The API design rules are written up as a page under `docs/` before API 1 begins.
- [x] **F1.G4** Every MVP hook in the register has an owner task in this list.

### F2 · Shared core — 9.3% (cumulative 14.1%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F2.01** Engine 1 · Core and platform layer — loop, memory, input, files, logging · `1.27%` 🔶
- [ ] **F2.02** Engine 2 · 2D renderer — gsKit backend + GL/GLES backend · `1.69%`
- [ ] **F2.03** Engine 3 · Asset pipeline — c++ cooker, per-target formats, pack files · `1.27%` 🔶
- [ ] **F2.04** Engine 4 · Audio — own software mixer on SDL audio · `0.84%`
- [ ] **F2.05** Engine 5 · Gameplay framework — entities, collision, animation, saves, UI · `1.27%`
- [ ] **F2.06** API 1 · Math and core types — vector3, Quaternion, Mathf, Random · `0.84%` 🔶
- [ ] **F2.07** API 2 · Object model — gameObject, Component, Transform · `1.27%` 🔶
- [ ] **F2.08** API 3 · Lifecycle, Time, Debug, Application — execution order and global services · `0.84%` 🔶

**Gates**

- [ ] **F2.G1** The no-per-frame-allocation check passes on every target.
- [ ] **F2.G2** Determinism test: the same scene and inputs give the same state hash on Linux and PSP.

### F3 · Editor core — 9.3% (cumulative 23.4%)

Roadmaps: [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F3.01** Editor 2 · Scene, hierarchy and inspector — gameObjects, Transforms, gizmos · `1.69%` 🔶
- [ ] **F3.02** Editor 3 · Asset database and project window — .meta files, importers, VRAM map · `1.69%` 🔶
- [ ] **F3.03** Editor 4 · C++ scripting workflow — behaviours, hot reload, compile errors · `2.11%`
- [ ] **F3.04** Editor 5 · Play mode and Game view — play, pause, step, revert, profile previews · `1.27%` 🔶
- [ ] **F3.05** API 4 · Coroutines, Invoke and events — waiting over time, C# events, UnityEvent · `1.05%`
- [ ] **F3.06** API 5 · Input — axes, buttons, actions, rumble · `0.63%` 🔶
- [ ] **F3.07** API 11 · Editor-facing attributes — serializeField, Range, RequireComponent · `0.84%`

**Gates**

- [ ] **F3.G1** A scene saved, reloaded and saved again produces no diff.
- [ ] **F3.G2** Every inspector edit can be undone and redone.
- [ ] **F3.G3** The Play dropdown's "Play in editor" works and Stop restores the scene.

### F4 · 2D slice everywhere + PSP base — 8.0% (cumulative 31.4%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [Editor](docs/rensga-engine/roadmaps/editor.html) · [PSP Engine](docs/rensga-engine/roadmaps/psp-engine.html)

- [ ] **F4.01** Engine 6 · 2D vertical slice on all targets — a small real game, shipped four ways · `1.69%`
- [ ] **F4.02** PSP Engine 0 · pspdev toolchain and PSP target — preset, EBOOT.PBP, bundled PPSSPP, CI · `0.63%` 🔶
- [ ] **F4.03** PSP Engine 1 · PSP platform layer — files, input, home button, memory, clock · `0.84%` 🔶
- [ ] **F4.04** PSP Engine 2 · GU 2D backend — display lists, 2 MB VRAM, 480x272 · `1.27%` 🔶
- [ ] **F4.05** PSP Engine 3 · PSP cooker plugin — swizzled CLUT textures, GU vertex formats · `0.63%` 🔶
- [ ] **F4.06** PSP Engine 4 · Audio on PSP — shared mixer, streaming from memory stick · `0.42%`
- [ ] **F4.07** PSP Engine 5 · 2D slice on PSP — same game, new target · `0.84%`
- [ ] **F4.08** Editor 7 · 2D toolset — sprites, tilemaps, 2D physics · `1.69%`

**Gates**

- [ ] **F4.G1** CI boots the PSP build in headless PPSSPP.
- [ ] **F4.G2** The 2D slice runs on Windows, Linux, Android, PS2 and PSP from one codebase.

### F5 · PSP track — 16.4% (cumulative 47.8%)

Roadmaps: [PSP Engine](docs/rensga-engine/roadmaps/psp-engine.html) · [PSP Editor](docs/rensga-engine/roadmaps/psp-editor.html) · [PSP API](docs/rensga-engine/roadmaps/psp-api.html) · [Emulator Play](docs/rensga-engine/roadmaps/emulator-play.html)

- [ ] **F5.01** PSP Engine 6 · GU 3D backend — hardware T&L, lights, skinning, clipping · `2.11%` 🔶
- [ ] **F5.02** PSP Engine 7 · PSP performance — budgets, VFPU, profiling · `1.27%`
- [ ] **F5.03** PSP Engine 8 · PSP packaging and release — eBOOT, saves, icons, open-source build · `0.84%`
- [ ] **F5.04** PSP Editor 0 · PSP target module — register PSP with the editor · `0.42%` 🔶
- [ ] **F5.05** PSP Editor 1 · PSP project template — consoleTarget PSP, default scene, input map · `0.42%` 🔶
- [ ] **F5.06** PSP Editor 2 · PSP import settings — texture, mesh and audio rules · `0.63%`
- [ ] **F5.07** PSP Editor 3 · PSP preview mode — 480x272, 16-bit color, live budgets · `0.84%`
- [ ] **F5.08** PSP Editor 4 · PSP inspector rules — profile gating and validation · `0.63%`
- [ ] **F5.09** PSP Editor 5 · Build and Run in PPSSPP — one click to a running EBOOT · `0.84%` 🔶
- [ ] **F5.10** PSP Editor 6 · PSP debugging and profiling — logs, profiler feed, PPSSPP debugger · `0.84%`
- [ ] **F5.11** PSP Editor 7 · PSP samples and docs — sample project and getting-started page · `0.84%`
- [ ] **F5.12** PSP API 0 · PSP profile definition — what the PSP profile allows · `0.42%` 🔶
- [ ] **F5.13** PSP API 1 · PSP input and app events — one nub, Home button, suspend · `0.42%` 🔶
- [ ] **F5.14** PSP API 2 · Rendering API on PSP — light limits, materials, skinning · `0.84%`
- [ ] **F5.15** PSP API 3 · PSP system services — save dialog, keyboard, language, battery · `0.63%`
- [ ] **F5.16** PSP API 4 · Performance API on PSP — cPU clock, memory stats, VFPU · `0.42%`
- [ ] **F5.17** PSP API 5 · Audio API on PSP — same API, PSP-safe defaults · `0.42%`
- [ ] **F5.18** PSP API 7 · PSP docs and API tests — profile badges, tests in PPSSPP · `0.63%`
- [ ] **F5.19** Emulator Play 0 · Emulator launch contract — cLI args, sandboxed configs, BIOS · `0.42%` 🔶
- [ ] **F5.20** Emulator Play 1 · Play, Pause, Stop — toolbar buttons driving the emulator · `0.63%` 🔶
- [ ] **F5.21** Emulator Play 2 · Fast iteration path — skip disc images, load assets from host · `0.63%` 🔶
- [ ] **F5.22** Emulator Play 3 · Console log and crash handling — game output in the editor console · `0.42%` 🔶
- [ ] **F5.23** Emulator Play 4 · Docked Game view — emulator window inside the editor · `0.84%`

**Gates**

- [ ] **F5.G1** Real-hardware pass on a PSP-1000 plus slim or Go.
- [ ] **F5.G2** Review: no PSP-specific code leaked into `editor/core`. The TargetModule interface grew instead.

### F6 · Full editor and API — 16.2% (cumulative 64.0%)

Roadmaps: [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F6.01** Editor 6 · Prefabs — instances, overrides, nesting, variants · `1.69%`
- [ ] **F6.02** Editor 8 · 3D toolset — meshes, materials, lights, cameras, audio · `1.69%`
- [ ] **F6.03** Editor 9 · Animation — clips, Animation window, Animator graph · `1.69%`
- [ ] **F6.04** Editor 10 · UI, input and project settings — canvas UI, input actions, tags and layers · `1.27%`
- [ ] **F6.05** Editor 11 · Build pipeline — build Settings, packaging, Build and Run · `1.27%` 🔶
- [ ] **F6.06** Editor 12 · Profiler and debugging — live stats from the running game · `1.27%`
- [ ] **F6.07** Editor 13 · Extensibility and polish — custom inspectors, data assets, samples · `1.27%`
- [ ] **F6.08** API 6 · Physics — rigidbody, callbacks, queries · `1.26%` 🔶
- [ ] **F6.09** API 7 · Rendering and camera — renderers, materials, particles, Gizmos · `1.26%`
- [ ] **F6.10** API 8 · Animation and audio — animator, AudioSource, mixer · `0.84%`
- [ ] **F6.11** API 9 · Scenes, assets and saves — sceneManager, Resources, save data · `1.26%`
- [ ] **F6.12** API 10 · UI API — buttons, text, sliders, navigation · `0.84%`
- [ ] **F6.13** API 12 · Platform and performance helpers — defines, profiling, pooling, budgets · `0.63%`

**Gates**

- [ ] **F6.G1** Every component type is tested through reflection, inspector, prefab override and the cooker.
- [ ] **F6.G2** Build and Run works from Build Settings for every enabled target.

### F7 · PSP template — 2.3% (cumulative 66.3%)

Roadmaps: [PSP Template](docs/rensga-engine/roadmaps/psp-template.html)

- [ ] **F7.01** PSP Template 0 · Primitive generator (shared) — gameObject > 3D Object menu · `0.63%` 🔶
- [ ] **F7.02** PSP Template 1 · Primitive character — hierarchy prefab, lit and textured · `0.42%` 🔶
- [ ] **F7.03** PSP Template 2 · Primitive test level — textured blocks, 4 lights, fog · `0.42%` 🔶
- [ ] **F7.04** PSP Template 3 · Character controller (shared base) — move, jump, one-nub camera, pickups · `0.63%` 🔶
- [ ] **F7.05** PSP Template 4 · Package the PSP template — new-project entry, CI boot test · `0.21%`

**Gates**

- [ ] **F7.G1** A first-time user goes from New Project (PSP) to Play without touching an SDK.

### F8 · PS2 track — 7.4% (cumulative 73.7%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [PS2 Template](docs/rensga-engine/roadmaps/ps2-template.html)

- [ ] **F8.01** Engine 7 · 3D renderer — ps2gl backend + matching GL look · `3.37%`
- [ ] **F8.02** Engine 8 · 3D gameplay and performance — characters, physics, animation, budgets · `2.11%`
- [ ] **F8.03** PS2 Template 0 · PS2 primitive settings — smoother tessellation, VU1 path · `0.21%`
- [ ] **F8.04** PS2 Template 1 · Primitive character and blob shadow — shared prefab, smoother look · `0.42%`
- [ ] **F8.05** PS2 Template 2 · Primitive test level — bigger layout, textures, fog · `0.63%`
- [ ] **F8.06** PS2 Template 3 · Dual-analog controller — right-stick camera, rumble · `0.42%`
- [ ] **F8.07** PS2 Template 4 · Package the PS2 template — new-project entry, boot test without public BIOS · `0.21%`

**Gates**

- [ ] **F8.G1** The PCSX2 launcher joins the emulator launch contract, with the BIOS prompt (no BIOS is ever bundled).
- [ ] **F8.G2** A self-hosted runner with a maintainer's private BIOS boots the PS2 template on every engine change.
- [ ] **F8.G3** Real-hardware pass on a PS2.

### F9 · PSX track — 24.2% (cumulative 97.9%)

Roadmaps: [PSX Engine](docs/rensga-engine/roadmaps/psx-engine.html) · [PSX Editor](docs/rensga-engine/roadmaps/psx-editor.html) · [PSX API](docs/rensga-engine/roadmaps/psx-api.html) · [PSX Template](docs/rensga-engine/roadmaps/psx-template.html)

- [ ] **F9.01** PSX Engine 0 · PSn00bSDK toolchain and PSX target — pS-EXE, BIN/CUE, bundled PCSX-Redux · `0.84%`
- [ ] **F9.02** PSX Engine 1 · Fixed-point core — same core systems, integer math · `1.26%`
- [ ] **F9.03** PSX Engine 2 · PSX platform layer — no SDL: pad, CD, memory card, vsync · `0.84%`
- [ ] **F9.04** PSX Engine 3 · GPU 2D backend — ordering tables, VRAM, 320x240 · `1.26%`
- [ ] **F9.05** PSX Engine 4 · PSX cooker plugin and VRAM packer — tIM-style textures, CLUTs, VAG, sectors · `1.26%`
- [ ] **F9.06** PSX Engine 5 · SPU audio and CD streaming — hardware voices, XA music · `1.05%`
- [ ] **F9.07** PSX Engine 6 · 2D slice on PSX — same game code, smaller assets · `1.05%`
- [ ] **F9.08** PSX Engine 7 · GTE 3D backend — transforms, sorting, subdivision, lights · `2.11%`
- [ ] **F9.09** PSX Engine 8 · PSX performance and streaming — scratchpad, area streaming, budgets · `1.26%`
- [ ] **F9.10** PSX Engine 9 · PSX packaging and release — bIN/CUE, memory card icon, licenses · `0.63%`
- [ ] **F9.11** PSX Editor 0 · PSX target module — register PSX with the editor · `0.42%`
- [ ] **F9.12** PSX Editor 1 · PSX project template — consoleTarget PSX, 320x240, starter scene · `0.42%`
- [ ] **F9.13** PSX Editor 2 · PSX import settings and VRAM map — bit depth, CLUT sharing, live VRAM view · `1.05%`
- [ ] **F9.14** PSX Editor 3 · PSX preview mode — wobble, affine warp, sorting, dither · `1.05%`
- [ ] **F9.15** PSX Editor 4 · PSX inspector rules — gating and validation · `0.63%`
- [ ] **F9.16** PSX Editor 5 · Build and Run in PCSX-Redux — one click to a booting disc image · `0.84%`
- [ ] **F9.17** PSX Editor 6 · PSX debugging and profiling — pCSX-Redux debugger, budget reports · `0.84%`
- [ ] **F9.18** PSX Editor 7 · PSX samples and docs — sample project, getting started, credits · `0.63%`
- [ ] **F9.19** PSX API 0 · PSX profile and fixed-point rules — what scripts can and can't do · `0.63%`
- [ ] **F9.20** PSX API 1 · Fixed-point math API — vector3, Quaternion, Mathf, same names · `0.84%`
- [ ] **F9.21** PSX API 2 · Rendering API on PSX — 3 lights, blend modes, segmented meshes · `0.84%`
- [ ] **F9.22** PSX API 3 · Physics on PSX — kinematic, simple shapes, queries · `0.63%`
- [ ] **F9.23** PSX API 4 · Audio and music API on PSX — sPU voices, XA tracks, residency · `0.63%`
- [ ] **F9.24** PSX API 5 · Scenes and saves on PSX — area streaming, memory card blocks · `0.63%`
- [ ] **F9.25** PSX API 7 · PSX docs and API tests — fixed-point guide, tests in PCSX-Redux · `0.63%`
- [ ] **F9.26** PSX Template 0 · PSX primitive settings — low tessellation, vertex color, fixed-point · `0.21%`
- [ ] **F9.27** PSX Template 1 · Segmented primitive character — box body, sphere head, cylinder limbs · `0.42%`
- [ ] **F9.28** PSX Template 2 · Primitive test level — grid ground, platforms, ramps, pickups · `0.63%`
- [ ] **F9.29** PSX Template 3 · Fixed-point character controller — move, jump, camera, pickups · `0.42%`
- [ ] **F9.30** PSX Template 4 · Package the PSX template — new-project entry, CI boot test · `0.21%`

**Gates**

- [ ] **F9.G1** Decide on SplashEdit collaboration before PSX Engine 0.
- [ ] **F9.G2** PCSX-Redux with OpenBIOS boots the PSX template in public CI.
- [ ] **F9.G3** PC and Android builds of a PSX game use fixed-point and match the PSX state hash.
- [ ] **F9.G4** Using `float` in PSX game code is a compile error with a hint.
- [ ] **F9.G5** Real-hardware pass on a PS1.

### F10 · Ship 1.0 — 2.1% (cumulative 100.0%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F10.01** Engine 9 · Ship — packaging, open-source release, legal notes · `0.84%`
- [ ] **F10.02** API 13 · Docs, samples and stability — reference, Unity mapping, versioning · `1.26%`

**Gates**

- [ ] **F10.G1** Name and pequi icon cleared against existing trademarks.
- [ ] **F10.G2** Each console SDK's license checked; notices included in every package.
- [ ] **F10.G3** Bundled emulators ship unmodified, each with its license and source link.
- [ ] **F10.G4** Real-hardware pass on every console for the release build.
- [ ] **F10.G5** Docs site with the API reference and Unity mapping is live.
- [ ] **F10.G6** GitHub release of the editor for Windows and Linux, plus pages for the sample games.

---

## Optional (not counted)

- [ ] ⏭ **OPT-1** PSP API 6 · Ad hoc multiplayer: local wireless play.
- [ ] ⏭ **OPT-2** PSX API 6 · FMV playback: VideoPlayer on MDEC.
- [ ] ⏭ **OPT-3** Emulator Play 5 · In-view emulation: the emulator renders into the Game view. Needs a GPL licensing review first.

## Open questions

From the design doc. Each must be answered by the milestone shown.

- [ ] **Q1** Do the Rensga Engine name and pequi icon clash with existing trademarks? *(by F10)*
- [ ] **Q2** Final check of each console SDK's license against MIT distribution. *(by F5 for PSP, F8 for PS2, F9 for PSX)*
- [ ] **Q3** Collaborate with SplashEdit on PSX, or build the PSX pipeline from scratch? *(by F9)*
- [ ] **Q4** Is the optional libretro in-view play mode worth its licensing review? *(before OPT-3)*
- [ ] **Q5** How much of the PSX memory card gap will OpenBIOS close before the PSX track starts? *(by F9)*
