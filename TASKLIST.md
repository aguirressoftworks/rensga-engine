# Rensga Engine — Task list

Every task from an empty repository to a shipped 1.0, in two steps:

1. **MVP.** A prototype 3D PSP game that boots in PPSSPP from both the editor's Play button and the `rensga play` command. It is built today, and it leaves every hook the full suite plugs into.
2. **Full suite, 0 → 100%.** Every phase of the 13 roadmaps, grouped into milestones, with progress measured in percent.

Sources: [design document](docs/rensga-engine/design-doc/rensga-engine-design-doc.md), [roadmaps](docs/rensga-engine/index.html), [RULES.md](RULES.md).

## How to use this list

- Tick a box (`- [x]`) when the task's "done when" is true. Don't tick partial work: add a note under the task instead.
- Task IDs (`MVP-07`, `F5.12`) are stable. Never renumber them. New tasks go at the end of their block with the next free number.
- `Engine 3`, `PSP Editor 5` and similar references mean phase 3 of the Engine roadmap and so on, as in the design doc. The roadmap page has the scope, topics and Claude prompts for that phase.
- **Percentages.** Each Step 2 task carries its share of the whole project, taken from its roadmap estimate (`estimate / 252.5 weeks`). The weeks are human part-time effort and are used only as relative weights, not as a schedule. Progress = sum of ticked percentages.
- **Gates** carry no weight but must be ticked before their milestone counts as closed.
- Legend: 🔶 the MVP gives this task a head start (it still counts only when the whole phase is done) · ⏭ optional, not counted · ◐ half ticked: done except a check that is waiting on something outside the repo, not counted in Progress until fully ticked · 🟡 attention: the reason, at the end of the task line.
- If a roadmap phase is added, removed or re-estimated, update this file in the same change (see AGENTS.md).

## Progress

| Step / milestone | Weight | Cumulative | Status |
| --- | --- | --- | --- |
| **Step 1 · MVP** | own 0–100% | — | 100%: all tasks done and accepted (clean build, play + editor Play + build, screenshot); S7 editor usability (MVP-45..57) done after the review, 13/13 tests |
| F1 · Foundations (harden the MVP) | 4.5% | 4.5% | 4.5% — closed (all tasks and gates) |
| F2 · Shared core | 8.7% | 13.3% | 4.75% ticked + 3.97% half ticked (◐ F2.01, F2.02, F2.05, G1: Android run deferred) — API 1-3, Engine 3 and Engine 4 done and accepted; Engine 1, 2 and 5 are built and tested on the host and PS2 and wait only for the Android phone run (`tools/ci/android_device.sh`); gates: G2 met, G1 met except Android (built, not run) |
| F3 · Editor core | 9.9% | 23.2% | 8.72% ticked (F3.01-F3.07 done: 57/57 ctest, console and CI checks passed natively without Docker); F3.08 half ticked (see its note); all four gates met |
| F4 · 2D slice everywhere + PSP base | 8.3% | 31.5% | 5.95% ticked + 2.38% half ticked (◐ F4.01 and F4.G2: Android phone run deferred, user decision; ◐ F4.03: Home-button quit is a manual PPSSPP check) — F4.02, F4.04–F4.10 and gate F4.G1 done on PPSSPP, PCSX2, wine and the Linux desktop: 67/67 ctest, `run_local.sh` 13 PASS + android and android_device SKIP (android-build module not installed, not built locally), 2D determinism matches over 2700 frames; real PSP/PS2 hardware runs deferred to F5.G1 and F8.G3 |
| F5 · PSP track | 15.4% | 47.0% | 12.27% ticked + 3.17% half ticked (◐ F5.02, F5.03: real PSP deferred, user decision; ◐ F5.13: Home and suspend only injected in PPSSPP; ◐ F5.23: native Wayland only forced, Win32 compiled not run) — shared 3D (lights, materials, skinning) on GL and GU, PSP system services, editor PSP module, preview and inspector rules, emulator Play with pause/step/crash handling, docked PPSSPP and profiler; gate G2 met, G1 deferred; checked on the integrated tree: 95/95 ctest, psp, psp_eboot, psp_hello, psp_2d slice, templates (incl. ps2-2d in PCSX2), determinism 3d/2d/3dlit, psp_3d, psp_api, psp_services, api_profile; the full `run_local.sh` (windows, windows_run, ps2, ps2_check, editor_tour, psp_gpu_check, the linux_host slice) was skipped by user decision and runs at the F6 acceptance |
| F6 · Full editor and API | 15.2% | 62.2% | 0% |
| F7 · PSP template | 2.2% | 64.4% | 0% |
| F8 · PS2 track | 6.9% | 71.3% | 0% |
| F9 · PSX track | 23.1% | 94.5% | 0% |
| F10 · Ship 1.0 | 2.0% | 96.4% | 0% |
| F11 · Visual scripting (Blueprint) | 3.6% | 100.0% | 0% |
| **Step 2 total** | **100%** | | **36.2%** |

---

## Step 1 · MVP: PSP prototype from the editor and the CLI

### Goal

On Linux, both of these build the PSP template project with the pinned native psp-build support module (MVP-era shortcut was a Docker image; **F3.08** replaced it, users never need Docker) and boot it in the bundled, pinned PPSSPP:

- `rensga play --target psp templates/psp-3d`
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

- [x] **MVP-16** ConsoleTarget JSON schema and `templates/psp-3d/ProjectSettings/ConsoleTarget.json`: profile `psp`, scalar `float`, 480x272, budgets for RAM (32 MB PSP-1000), VRAM (2 MB) and triangles per frame, plus allowed components. A generator emits a constants header, and `ENGINE_PROFILE_PSP` is defined.
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
- [x] **MVP-32** Template scene `templates/psp-3d/Assets/Scenes/TestLevel.scene`: ground plane, 4–6 box platforms and a ramp, a capsule player with a sphere head, 5 pickups, a camera and a directional light setting.
- [x] **MVP-33** Template scripts in `templates/psp-3d/Assets/Scripts/`, using `Scalar` only (never `float`):
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
  - run `fetch_ppsspp.sh`, then `rensga play --target psp templates/psp-3d`
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
- [x] **MVP-53** Play integration and close-out: Play saves a dirty scene first (Console line), so edits reach the build. Checked end to end on a scratch copy of `templates/psp-3d`: a test moves a pickup and adds a cube, saves; `rensga cook` and `rensga_headless --pack` load 22 objects (template: 21). `rensga-editor --play` exits non-zero when the pipeline fails. Screenshot tooling: `--screenshot`, `--select`, `--focus`, `--ui-event`.
- [x] **MVP-54** Assign scripts: the Project window lists `Assets/` and shows scripts by class name (file name = class name). Drag a script onto a Hierarchy row, onto an object in the Scene view (pick under the cursor) or onto the Inspector to add that Behaviour (one undo step; the class is also added to the project's `allowedComponents` so the cook accepts it). Add Component has a Scripts submenu; scripts on disk but not yet compiled into this editor are greyed with "compiling..." or "needs rebuild". Double-click opens the file in `$VISUAL`, `$EDITOR` or `xdg-open`.
- [x] **MVP-55** Create objects from the Scene view: a right-click without drag opens the Hierarchy create menu (Create Empty, 3D Object, Camera, Light); the object lands where the cursor ray hits the scene or the ground, else 5 units ahead, and is selected. Right-drag stays fly-look.
- [x] **MVP-56** Project window > Create > C++ Script: name prompt (C++ identifier, unique), writes `Assets/Scripts/<Name>.cpp` from a template (Behaviour, REFLECT, HEADER, `SERIALIZE_FIELD(Scalar, speed, 1.0f)`, Start, Update) and its `.meta` with a new GUID.
- [x] **MVP-57** Auto rebuild like Unity: on window focus gain, on project open and before Play, the editor hashes `Assets/Scripts/*.{cpp,h}` against `Library/Editor/scripts.hash` (written after the last good build). If changed it rebuilds `rensga-editor`, `rensga`, `rensga-cooker` and `rensga_headless` in the background (reconfiguring its own build dir for the project if needed), shows progress in the toolbar, and streams the compiler output to the Console with errors as `file:line` at Error level. On success it saves `Library/Editor/reload_state.json` (unsaved scene incl. dirty flag, selection, Scene camera, focused tab), re-executes itself with `--restore-state` and restores. On failure there is no restart; Play is disabled with "Fix script compile errors first" until the files change and the editor regains focus. Play with changed scripts waits for the rebuild and resumes after the restart. Flags: `--no-auto-rebuild`, `--auto-rebuild` (off by default in `--screenshot` runs).
- [x] **Fix** The target shows as "PSP" in the toolbar, not "PlayStation Portable" (RULES §5: no Sony marks in the UI).

### MVP hook register

Each MVP shortcut, where it lives, and the Step 2 task that finishes it. Add a row for any new shortcut.

| Hook / endpoint | Lives in | MVP state | Finished by |
| --- | --- | --- | --- |
| `IPlatform`, `IFileSystem` | `engine/core`, `backends/platform/{sdl,psp,ps2}` | `data:/` and `save:/` paths, stream slots, `writeFile`; SDL (PC, Android assets), PSP (done in F4.03: SDL2 for pad and audio only, never video; data root probed from argv[0] folder, ms0, umd0, host0; exit and suspend callbacks, 333 MHz, arena sized from the pack's CTGT), PS2 (cdrom0/host/mass/mc0, `icon.sys` on the card); Android multi-touch virtual gamepad and the Android player APK built (phone run pending, F2.01); the PSP Home button and the real pad mapping are checked only by hand | **F2.01**&nbsp;Engine 1 (Android phone run), **F4.03**&nbsp;PSP Engine 1 (Home check in PPSSPP), **F5.02**&nbsp;PSP Engine 7 (real PSP) |
| `IRenderer` with sort keys | `engine/core`, `backends/render/gu` | GU 3D + 2D (done in F4.04: 5650 framebuffers, double-buffered display lists, `GU_SPRITES` batches with a radix sort by (sortKey, tex, seq), CLUT4/8/5551/4444/RGBA8 swizzled textures, first-fit VRAM at 0xCC000-0x200000 with a sampled-from-RAM fallback and no LRU, at most 3072 sprites per frame, the 3D viewport is not letterboxed in a non-480x272 space), null; additive 2D calls (`uploadTexture`, `drawSprite`, `drawRect`, `drawText`) in GL and gsKit (explicit PS2 VRAM manager with LRU eviction); GL and GS refuse swizzled textures | **F2.02**&nbsp;Engine 2, **F5.01**&nbsp;PSP Engine 6, **F5.02**&nbsp;PSP Engine 7 (texture streaming), **F9.04**&nbsp;PSX Engine 3 |
| `IAudioBackend` | `engine/core`, `backends/audio` | software mixer (16 voices, streamed music), SDL and audsrv backends, created by the desktop, Android and PS2 mains (done in F2.04); PSP uses the shared SDL backend at 22050 Hz with a read-ahead thread for music (done in F4.06; 16 voices cost 10.2% of a core at 222 MHz, 6.8% at 333 MHz in PPSSPP's virtual time); the cooker resamples console audio to the profile rate (done in F5.17, windowed sinc, downsampling only; PC targets keep the source rate); voices are stolen by `AudioSource.priority` (F5.17); the PSX backend is open | **F9.06**&nbsp;PSX Engine 5 |
| `Scalar` | `engine/core/math` | `float` | **F9.02**&nbsp;PSX Engine 1 (20.12 fixed-point) |
| Reflection macros | `engine/core/reflect` | build-time codegen tables (`tools/reflect`), thin static registrar | done in F1.05 |
| `Ref<T>`, object model | `engine/core` | done in F2.07 (tags, layers, Instantiate/Destroy, hierarchy API) | done in F2.07 |
| Lifecycle | `engine/api` | done in F2.08 (all eight callbacks, execution order, `Time` getters) | done in F2.08 |
| Input actions | `InputActions.json`, `engine/api` | done in F3.06 (axes, buttons, actions, rumble, composites; PSP pad map, SDL pads); PS2 libpad rumble and PSP static_asserts never compiled (needs the F3.08 modules or Docker) | **F6.04**&nbsp;Editor 10 (action editor) |
| 2D gameplay | `engine/core/gameplay`, `engine/api` | tile flags (solid, one-way, slopes), BoxCollider2D, TilemapCollider2D, Physics2D layers, Camera2D, DontDestroyOnLoad, anchored UI; Rigidbody2D (done in F4.08: dynamic, kinematic and static; gravity is a module constant, `Rigidbody2DSim::setGravity`, default 1400 px/s^2, because Physics2D has no gravity setting yet); 2D editor gizmos done in F3.01; the same slice runs on PS2, PSP, desktop and Windows (F4.01, F4.07); no PSX 2D | **F6.08**&nbsp;API 6 (gravity setting, joints, queries), **F9**&nbsp;PSX Engine 3 |
| Real-PS2 timing and memory card | `backends/platform/ps2`, `runtime_audio` | measured only in PCSX2 (emulated timing); `icon.sys` and `rensga.ico` are written on first card use (F4.01; checked in PCSX2's card image, not on a real card); no real-pad play-through | **F8.02**&nbsp;Engine 8 and **F8.G3** |
| Physics | `engine/api` | CharacterController, box colliders, sphere triggers | **F6.08**&nbsp;API 6 |
| ConsoleTarget + profile defines | `ProjectSettings/`, generated header | PSP profile | **F1.02**&nbsp;API 0, **F5.12**&nbsp;PSP API 0 |
| Project layout, `.meta`, `Library/` | templates, cooker, `editor/core` asset database | done in F3.02 (GUID index, importer settings with per-target overrides, Project window, VRAM map); moving or renaming the open scene file does not update `project.scenes`; the PS2 VRAM map uses the generic layout | **F8.07**&nbsp;PS2 Editor (PS2 TargetModule) |
| Pack format | `tools/cooker`, runtime loader | v2 (TEXR with stored size and `flags` since F4.05, AUDI, TMAP, ANIM, FONT, SCNE, PH2D, CTGT audio fields, optional MSHX; readers accept v1 and v2); per-asset cook cache (PNG, WAV) and desktop pack hot reload done in F2.03; the editor's asset database is done in F3.02; F3.05 added the CORO section (scheduler limits) | done in F3.02 |
| `ICookerFormatPlugin` | `tools/cooker` | PSP done in F4.05: textures (auto, CLUT4/8, 5551, 4444, RGBA8; power-of-two stored size up to 512; swizzled, `kTexSwizzled`) and compact `MSHX` meshes, only in a cook for the psp target itself (a PSP project cooked for linux, windows or android stays RGBA8 and float); `tools/ci/pack_check.py` keeps PSP packs out of GL/GS builds; PSP texture formats are not checked against the ConsoleTarget `textureFormats` list; F5: lit and skinned meshes (`MSHL`/`VTX3`, float or C16), bone batches, materials, skins and clips, textures over `maxTextureSize` are downscaled with a warning, audio is resampled | **F9.05**&nbsp;PSX Engine 4 |
| Primitive generator | shared lib | 5 shapes | **F7.01**&nbsp;PSP Template 0 |
| Budget validator | `tools/pipeline` | RAM, triangles, capacity | **F6.05**&nbsp;Editor 11 |
| Pipeline stages | `tools/pipeline` | dev skips Package | **F6.05**&nbsp;Editor 11, **F5.21**&nbsp;Emulator Play 2 |
| `EmulatorLauncher` | `tools/pipeline` | PPSSPP done in F5.19-F5.22 (headless, env, sandbox, debugger, version check, pause/step via `host0:/rensga.ctl`, crash and hang detection); PCSX2 and PCSX-Redux rows of `docs/rensga-engine/emulator-launch.md` are intent only | **F8**&nbsp;(PCSX2), **F9**&nbsp;(PCSX-Redux) |
| EBOOT packaging | `editor/targets/psp` packager | done in F5.03 except the real-PSP run: SFO title, disc ID, version, ICON0/PIC1 (generated placeholders from the brand tokens when the project has none), music, licenses; SND0 only if the project supplies an AT3 (no encoder) | **F5.G1** (real PSP) |
| `TargetModule` | `editor/core`, `editor/targets/psp` | PSP only | **F1.04**&nbsp;Editor 0.5, **F5.04**&nbsp;PSP Editor 0 |
| Inspector | `editor/app/panels/drawers`, `editor/core/inspector_model` | done in F3.01 and F3.07 (multi-edit, drawer registry, UnityEvent and reference drawers, component clipboard, attributes); no prefabs | **F6.02**&nbsp;Editor 8 (prefabs) |
| GL scene renderer | `editor/app/render` | OpenGL 3.3, vertex color x lambert, no textures (the 2D sprite batcher is separate, in `backends/render/gl`) | **F8.01**&nbsp;Engine 7 (matching GL look) |
| `SceneDocument` undo | `editor/core` | JSON snapshots, 200 steps | **F1.05**&nbsp;Editor 1 |
| Scene editing | `editor/app/panels`, `editor/core/scene_document` | done in F3.01 (multi-select, drag reparent, 2D mode, ID-buffer picking, box select, Gizmos API); a texture dropped on the 2D view lands at the origin | **F6.01**&nbsp;Editor 7 |
| Game tab preview | `editor/app/panels/game_view.cpp` | Play in editor renders the real runtime into an offscreen GL target at the profile resolution (letterbox, integer scale, budget warnings); no audio in editor play | **F5.07**&nbsp;PSP Editor 3 (preview look) |
| Editor theme | `editor/app/theme.cpp` | built from `brand/palette.json` at configure time | **F6.12**&nbsp;Editor 13 polish |
| Play dropdown | `editor/core/play_session`, `editor/app/panels/play.cpp` | "Play in editor" (default), Pause, Step, Stop with revert, hot reload during play; the document is read-only while playing; "Play on console" runs the emulator pipeline | **F5.19**&nbsp;Emulator Play 0 |
| CLI `--json` events | `tools/cli` | `validate`, `cook`, `build`, `play` | **F6.05**&nbsp;Editor 11 and CI |
| Script module hot reload | `editor/core/game_module`, `script_build` | done in F3.03: scripts build as a per-project shared module (background `ScriptBuildJob`, status bar progress and Cancel), loaded with dlopen and reloaded with script state kept; compile errors with file:line open in the external editor. Windows path (LoadLibrary, import lib) written but never built: `TODO(Windows)` in `editor/app/CMakeLists.txt` | **F10**&nbsp;Windows editor build |
| Create C++ Script | `editor/core/script_assets` | fixed template, `Assets/Scripts/<Name>.cpp`; builds and hot reloads on creation | **F6.03**&nbsp;Editor 9 (templates) |
| Script drag-and-drop | `editor/app/panels` | done in F3.01 and F3.02: `RENSGA_ASSET_GUID` payload, Project to Hierarchy, Scene view and Inspector | done |
| Template project | `templates/psp-3d`, `templates/psp-2d`, `templates/ps2-2d` | test level and controller; the two 2D templates (F4.09, F4.10) are generated from `samples/platformer2d` by `tools/templates/make_2d_template.py` (one room, shared `PlayerController2D`, coins, anchored HUD, Camera2D); the Hub lists six cards, `psx-2d`, `psx-3d` and `ps2-3d` are greyed out with "Coming in <phase>" | **F7.04**&nbsp;PSP Template 3, **F9.31**&nbsp;PSX Template 5 |
| Console and cross toolchains | `tools/docker/*.sh`, `tools/pipeline` Compile stage, `editor/targets/psp` (`dockerRun`), `tools/ci/*.sh`, `.github/workflows/ci.yml` | MVP: pinned Docker images (users and CI needed Docker). F3.08: pinned native build support modules, Docker only as optional contributor backend | **F3.08**&nbsp;Editor 11.5 (native build support modules from the Hub or `rensga modules`; CI installs the same modules; Docker stays an optional contributor backend, `RENSGA_TOOLCHAIN=docker`) |
| CMake presets | `CMakePresets.json` | `linux-host`, `psp`, `windows`, `android`, `ps2` enabled (cross builds run in pinned Docker images via `tools/docker/*.sh` today; native build support modules replace this in **F3.08**, Docker stays an optional contributor backend); `psx` declared | done in F1.01 (module toolchains: **F3.08**; psx: **F9**) |
| `rensga cook <project>` and the CLI know only their own scripts | `tools/cli`, `tools/cooker`, `RENSGA_PROJECT_DIR` | the CLI and the cooker link the scripts of the project they were built with (default `templates/psp-3d`), so a project created in the Hub (for example `templates/psp-2d`) fails with "unknown component type 'PlayerController2D'" in a console cook; `tools/ci/templates.sh` and `psp_2d.sh` use a project-built `rensga-cooker`; Play in editor is unaffected (scripts build as a module, F3.03) | **F6.05**&nbsp;Editor 11 (the build pipeline loads the project's script module in the cooker) |
| Tile tools UI | `editor/app/panels/tile_tools.cpp`, `editor/core/tilemap_edit` | Tile Palette, 2D painting, Sprite Editor and the sprite picker done in F4.08; `TilemapEdit::resize` and rule tiles exist in the model and are tested, but there is no UI to resize a map or to author a `*.ruletile.json` (rule tiles are read from the file); no multi-layer palette | **F6.07**&nbsp;Editor 13 (extensibility and polish) |
| Play in editor cooks the host form of the pack | `editor/core/src/play_session.cpp`, `EditorWorld` | cooks for the host platform (linux or windows), not the console profile, because the GL renderer refuses PSP swizzled textures; since F5.07 the Game view's budget bars come from a separate console-form cook of the open scene (`scenePreview`, at most once per second) and cooked PSP textures are shown decoded; the Game view is empty in edit mode (live only while playing) | **F6.05**&nbsp;Editor 11 |
| Desktop alloc-check needs software GL without a shader JIT | `tools/ci/linux_host.sh`, `windows_run.sh` | the 2D slice runs with `GALLIUM_DRIVER=softpipe`: llvmpipe's shader JIT allocates through `operator new` (147 allocations, all inside libgallium and libLLVM) and the player exits 4; the engine itself is at `allocs=0` on every target; a run on a real GPU driver is not part of CI | **F10.01**&nbsp;Engine 9 |
| Bot coin counts on wall-clock runs | `samples/platformer2d` `TitleBot`, `tools/ci/windows_run.sh` | the bot runs on wall-clock frames, so a very slow wine run can lose a coin (18 of 19) and game time varies per target (1890 to 2167 ticks); determinism is gated by the fixed-step sims instead (`rensga_platformer_tests`, `tools/ci/determinism.sh 2d`) | gate **F4.G2** (no fixed-step desktop bot planned) |
| PSP-1000 memory limit | `tools/cooker`, `editor/targets/psp` `validate()` | the 32 MiB ceiling and the 16-bit framebuffer are enforced by the cooker and `validate()` only; PPSSPP honours the SFO `MEMSIZE=1` (slim memory), so no emulator run proves the PSP-1000 fit | **F5.02**&nbsp;PSP Engine 7 (real PSP) |
| Shared 3D built ahead of F6 | `engine/core` (`lighting`, `animation`, `cull`), GL and GU renderers, cooker | user decision 2026-10-31: Light (directional, point), Material, SkinnedMeshRenderer, legacy Animation (no Animator graph, no blending between clips), glTF skins and clips resampled at cook, 8-bone batches; the PS2 GS renderer draws lit and skinned meshes with their baked fallback only (PS2 profile lists the features) | **F6.02**&nbsp;Editor 8, **F6.03**&nbsp;Animator, **F6.09**&nbsp;API rendering, **F8**&nbsp;(GS lighting and skinning) |
| GU lighting relies on the GE normalising normals | `engine/backends/render/gu` | C16 meshes fold their decode scale into the model and bone matrices; PPSSPP's software GE normalises normals, so C16 and scaled objects light like float ones there; hardware behaviour unproven | **F5.G1** |
| Point-light falloff differs between GL and GU | GL shader, GU `sceGuLightAtt` | GL uses `clamp01(1 - d/range)^2` like `lighting.h`; the GU uses quadratic attenuation reaching one tenth at `range`, so PSP floors are less tinted by point lights than the desktop (`room_side_by_side.png`) | **F6.09**&nbsp;API rendering |
| VFPU only in the render path | `engine/backends/render/gu` (`RENSGA_PSP_VFPU_RENDER`) | matrix products for C16 decode and bone matrices; never in `engine/core` or sim math (determinism) | **F5.G1** (hardware timing) |
| Home and suspend are injected in PPSSPP | `engine/backends/platform/psp`, `app_events` | the power callback and `sceImposeGetHomePopup` paths compile and are polled (every 8th frame, PPSSPP does not implement it); tests use `injectApplicationPause` and the dev `apppause` command | **F5.G1** |
| Emulator docking on Wayland and Windows | `editor/app/platform/{x11,win32}_embed.cpp` | X11 reparenting tested under xvfb; native Wayland keeps a floating window (tested only with `RENSGA_EMBED=wayland`); Win32 `SetParent` compiles for mingw, never run; the debugger probe is Linux only | **F10.01**&nbsp;Engine 9 |
| Build and Run owns its own session | `editor/app/panels/emulator_manager.cpp` | `PlayController::play` forces a dev build, so Build and Run runs its own `Session` | **F6.05**&nbsp;Editor 11 |

Recorded deviations:

- **Docker toolchains are an MVP shortcut, replaced in F3.08.** Reason: end users must not need Docker. Console and cross builds move to pinned, sha256-verified native build support modules (Unity Hub model); CI installs the same modules, so what CI tests is what users get. Docker remains only as an optional contributor backend (`RENSGA_TOOLCHAIN=docker`).
- **F2: VU0 acceleration is out of scope.** The API 1 roadmap lists it as a topic; `Mathf` and the vector types are portable C++ with no libm and `-ffp-contract=off`, which the Linux/PSP hash match depends on. A VU0 path would need its own determinism check (F8).
- **F2: PS2 runs are local only.** PCSX2 needs the user's own BIOS, referenced in place through `~/.config/rensga/emulators.json`; the `ps2_check` job exits 77 (skipped) in public CI. The sandbox has no controller, so PS2 samples use AUTOSTART/SELFTEST cmake options.
- **F2: Android device check is built, not run.** No phone was attached. The APK now ships the player (`libmain.so`, package `com.rensga.player`, sample pack and `music.wav` as assets); `tools/ci/android_device.sh` prints "built, not run" without a device. F2.01, F2.02, F2.05 and F2.G1 wait for the phone run.
- **F2: PS2 audio and VRAM numbers come from PCSX2.** Timing is emulated; real-PS2 numbers are F8.02 and F8.G3. The PS2 audio backend patches the BIOS module loader (`sbv_patch_enable_lmb`) to load audsrv on the 1.60 BIOS.
- **F2: `IFileSystem` fixed slot table.** 8 stream slots, no heap; PS2 memory card saves use flat names under `mc0:/RENSGA`; the F4.01 slice also writes `icon.sys` and `rensga.ico` on first use.
- **F2: Windows alloc-check ran under wine.** CI installs wine and skips if missing; a native Windows run is not done.
- **F4: manual checks that no emulator run can make.** The PSP Home button (PPSSPPHeadless takes no input; the exit and suspend callbacks are implemented and the suspend/resume counts are logged), the real PSP pad mapping (`psp_pad::mapSdl` is checked only against SDL's published PSP mapping, and the boot log line `input sdl controller=PSP builtin joypad`), and GLES pixels (see the open gap below) stay manual checks. The Android player APK is built in CI only: the `android-build` module is not installed on the development machine and only the user may accept Google's license, so `run_local.sh` records android and android_device as SKIP (exit 77, "not built locally").
- **F4: real-hardware runs deferred.** On 2026-10-31 the user deferred the F4 real PS2 and real PSP hardware runs and the Android phone run. F4 uses PCSX2 and PPSSPP only (the Android APK is built, not run, like F2). The real-hardware passes moved to later phases and gates: PSP in PSP Engine 7 (**F5.G1**), PS2 in Engine 8 (**F8.G3**), every console in **F10.G4**. The roadmaps (Engine 6, PSP Engine 5, PSP Engine 7, Engine 8) and the design doc say the same.
- **F5: real PSP deferred again.** On 2026-10-31 the user kept the real-PSP runs deferred for F5: F5.02 and F5.03 stay half ticked and F5.G1 stays open; `tools/psp/psplink_deploy.sh` and `docs/rensga-engine/psp-hardware-checklist.md` are prepared for it. F5 also builds a minimal shared 3D (lights, materials, skinning) ahead of F6 so that PSP Engine 6 and PSP API 2 have something to render; F6 extends it.

- **Native PSP platform instead of SDL2. (Resolved in F4.03 and F4.06.)** The PSP backend now runs SDL2 for the game controller, timer and audio (`SDL_MAIN_HANDLED`, no SDL2main, never SDL video: the GU draws directly), as the design doc specifies. `psp_pad::map` (sceCtrl) stays as the fallback when SDL finds no controller (`-DRENSGA_PSP_NATIVE_PAD=ON` forces it, tested). The module info, exit callback and suspend callback stay ours and only set flags.
- **Project scripts are compiled into the cooker, CLI and editor at build time** (`RENSGA_PROJECT_DIR`, `rensga_cooker_scripts`). Reason: reflection registers types at static init, so scenes can only be read by an executable that links the scripts. F1.05 replaced static-init field registration with build-time codegen (`tools/reflect/reflect_scan.py` writes `*.reflect.gen.cpp`; the macros only declare), which closes the reflection deviation. Scripts are still linked in until hot reload (Editor 4) loads them dynamically.
- **Project scripts are sources of the runtime executables, not a static library.** Reason: static registrars would otherwise be dropped by the linker.
- **Dev Play boots the ELF directly with assets from `host0:`;** the release PRX/EBOOT is only built by `rensga build` (placeholder ICON0).
- **PPSSPP is the distro package (`/usr/bin/PPSSPPSDL`) found by `fetch_ppsspp.sh`, never linked.** Reason: GPL, separate process only.
- **MVP-43 screenshot comes from the game, not a window grab.** PPSSPP's GL window reads black to xwd/import/ffmpeg under Xvfb and PPSSPPHeadless cannot compare/emit screenshots. `tools/emulators/screenshot.sh` builds the ELF with `RENSGA_EXIT_AFTER_FRAMES` (dumps the displayed frame to `host0:/rensga_shot.bmp` on exit), runs it in PPSSPPHeadless (software renderer) and converts it to `docs/rensga-engine/mvp/psp-template.png`.
- **Script changes rebuild the editor and restart it. (Resolved in F3.03.)** Scripts now compile into a per-project shared module that the editor loads and reloads without restarting; the CLI, cooker and headless runtime still link the scripts at build time (`-DRENSGA_PROJECT_DIR`). Reason for the per-project module: the editor stays one binary for every project. Undo history survives a reload.
- **F1 CI runs on GitHub** (private repo `aguirressoftworks/rensga-engine`). The first run failed only in the psp job (runner lacked GLU headers to build PPSSPPHeadless; fixed in `ce67819`), then all 5 jobs went green. `tools/ci/run_local.sh` runs the same jobs locally. Reflection codegen needs Python 3 on the host (cross images have none; `tools/reflect/prescan.sh` writes the tables).
- **Editor exit code** (fixed in MVP-53): `rensga-editor --play` now exits 1 when the pipeline fails or cannot start.
- **Bugs found in the user's play-testing of the MVP** (all fixed; none was caught by acceptance):
  - PSP imports got their NIDs from link order, so the game booted to a black screen in PPSSPP.
  - The 2-triangle ground plane was dropped by the GU; the plane primitive now takes `cellSize` and the cooker subdivides it.
  - Strafe and camera orbit were mirrored; the template now takes the right axis from the camera.
- **Open gap: acceptance never ran PPSSPP's GPU renderer.** MVP-43 used PPSSPPHeadless (software renderer), which hid the three bugs above. A GPU-renderer run (windowed or a GL-capable headless) must join the acceptance checks in F5. (F4 closed part of it: `tools/ci/psp_gpu_check.sh` and `psp_2d.sh gles` run the 3D template and the 2D slice under `PPSSPPHeadless --graphics=gles` and check the log for unknown syscalls and GPU errors. The pixels of the GLES path are still a manual check, because PPSSPP does not read GLES framebuffers back into VRAM and every frame dump from a GLES run is stale.)

---

## Step 2 · Full suite: 0 → 100%

Milestones follow the design doc's build order: shared core → editor and TargetModule → PSP → PS2 → PSX → ship. Milestones can overlap once their inputs exist. The percentages measure how much is done, not when.

### F1 · Foundations (harden the MVP) — 4.5% (cumulative 4.5%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [x] **F1.01** Engine 0 · Toolchain and build matrix — one CMake repo, four targets, CI green · `0.79%` 🔶
- [x] **F1.02** API 0 · API design rules — naming, handles, ownership, profile gating · `0.59%` 🔶
  - Rules: `docs/rensga-engine/api-rules.md`; `tools/check_api_rules.sh` in ctest. Audit notes: F2.08 makes `Time::*` getters; F2.07 adds `Transform` local getters/setters and the `RENSGA_REQUIRES_FEATURE` / `RENSGA_API_VERSION_*` macros.
- [x] **F1.03** Editor 0 · Editor shell and projects — docking UI, project folders, console · `0.79%` 🔶
- [x] **F1.04** Editor 0.5 · TargetModule interface — console plug-in boundary · `0.79%` 🔶
  - Reviewed: preview and inspector rules are now data from ConsoleTarget.json; `docs/rensga-engine/target-module.md`; test-only `editor/targets/dummy`. Left for F5.04: the cooker CLI default `--target psp` and its PSP plugin registration.
- [x] **F1.05** Editor 1 · Reflection, serialization and undo — the backbone everything else needs · `1.59%` 🔶
  - Codegen: `tools/reflect/reflect_scan.py` (Python 3; the cross images have none, so cross builds read tables written by `tools/reflect/prescan.sh` on the host, run by the PSP target module and `tools/ci/*.sh`). Macros: `REFLECT`, `SERIALIZE_FIELD`, `RANGE`, `HEADER`, `TOOLTIP`, `REQUIRE_COMPONENT`; `RENSGA_STRIP_EDITOR_METADATA` (on for cross/runtime builds) strips headers, tooltips, ranges and required components. For a scanned `.cpp` the generated file includes it and replaces it in the source list.
  - One description feeds the inspector, JSON (`fieldsToOrderedJson`), the FDAT blob, field paths (`parseFieldPath`: `speed`, `offset.x`) and hot-reload state transfer (`saveState` / `loadState`, name-keyed). Undo is command-based (`editor/core/undo.h`: merge by key, groups, 256 steps, dirty tracking); `SceneDocument` runs on it.

**Gates**

- [x] **F1.G1** CI builds every enabled preset on every push. *Green on GitHub Actions: linux-host, psp (+ headless boot + GPU-renderer syscall check), windows, android, ps2 + ISO.*
- [x] **F1.G2** `tools/check_editor_core.sh` runs in CI.
- [x] **F1.G3** The API design rules are written up as a page under `docs/` before API 1 begins.
- [x] **F1.G4** Every MVP hook in the register has an owner task in this list.

### F2 · Shared core — 8.7% (cumulative 13.3%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [API](docs/rensga-engine/roadmaps/api.html)

- ◐ **F2.01** Engine 1 · Core and platform layer — loop, memory, input, files, logging · `1.19%` 🔶 🟡 *Attention: everything is done (touch controls and the Android player are built) except the Android on-device run (`tools/ci/android_device.sh`), deferred by the user to a later phase.*
  - Acceptance 2: done and tested: fixed-timestep loop, arena/pool/frame allocators with budgets, `data:/` and `save:/` file systems (PC, Android assets, PS2 cdrom0/host/mass/mc0, PSP ms0:/host0:), logging, alloc-check, multi-touch virtual gamepad (`engine/backends/platform/sdl/touch.cpp`, tests `touch_pad_logic`, `touch_sdl_events`), and the Android player APK (`libmain.so` is the engine player, `game.rpak` and `music.wav` in the assets, `build/ci/android/rensga-player.apk`). Waiting for the phone run (`tools/ci/android_device.sh`): touch zones and the player on a real device.
- ◐ **F2.02** Engine 2 · 2D renderer — gsKit backend + GL/GLES backend · `1.59%` 🟡 *Attention: everything is done (the GLES path is built) except the Android on-device run (`tools/ci/android_device.sh`), deferred by the user to a later phase.*
  - Acceptance 2: GL desktop renderer and gsKit renderer done. The PS2 VRAM manager is done (32 KB CLUT area, 8 KB-page first-fit texture area with LRU eviction, queue kick before eviction), verified in PCSX2 with `RENSGA_PS2_VRAM_STRESS`. The only gap left is the Android GLES run on a device; waiting for the phone run (`tools/ci/android_device.sh`). Font and tilemap draw paths are used by the sample on both backends.
  - Evidence: PCSX2 `rensga: vram fb=2240KB tex=1608KB used/1824KB area clut=2KB free=216KB evictions=5833 uploads=5841`, `alloc-check frames=300 allocs=0`, screenshot of 24 correct textured cells (PCSX2 hardware and software renderers identical; CLUT8 uses CSM1 = TEX0.CSM 0, gsKit puts ClutStorageMode into CSM); desktop 1000 sprites `alloc-check frames=299 allocs=0`.
- [x] **F2.03** Engine 3 · Asset pipeline — c++ cooker, per-target formats, pack files · `1.19%` 🔶
  - Evidence: cooker with per-target plugins (PS2 CLUT4/CLUT8 or RGBA, PC/Android RGBA), pack v2 (TEXR, AUDI, TMAP, ANIM, FONT, SCNE, PH2D, CTGT), whole-project skip plus a per-asset cache in `Library/Cache/<target>/` (PNG and WAV; JSON assets always re-cook). Touching one WAV in the sample: `cook: cooked 1, cached 8`; unchanged project: `cooked 0, cached 0 (whole project up to date)`; test `assets_cook_cache_reuses_unchanged_assets`. Desktop hot reload: `rensga_desktop` reloads the pack between frames and logs `rensga: hot-reload <pack> (scene TestLevel, objects=21)` (test `hot_reload_desktop`); the PS2 build has no watcher. CSM1 CLUT8 swizzle is in `gs_renderer.cpp` and the PS2 screenshots show correct palette colors. Exercise (cook a tileset and sprite sheet for PS2 and PC, load, draw): `samples/platformer2d` room on PS2 (PCSX2) and desktop.
- [x] **F2.04** Engine 4 · Audio — own software mixer on SDL audio · `0.79%`
  - Evidence: one mixer (16 voices, volume/pan, loop, streamed music with 4096-frame double buffering) wired into the desktop, Android and PS2 mains (`runtime_audio.cpp`; sample rate and voice cap from the ConsoleTarget, voice count is compile-time `RENSGA_AUDIO_MAX_VOICES`); SDL backend on PC and Android, audsrv on PS2 (needs `sbv_patch_enable_lmb` on the 1.60 BIOS); music is `Streaming/music.wav`, copied next to the pack and put on the disc as `MUSIC.WAV`. Exercise (looping music plus 16 overlapping effects, CPU cost): PCSX2 `audio-stress voices=16 mix_us=3119/4310 ... cpu_permille=92 music=1 underruns=0`, `alloc-check frames=200 allocs=0` (emulated timing, about 9% of a core); desktop `audio-stress voices=16 mix_us=177/221 ... cpu_permille=3 underruns=0` with a non-silent device output (test `audio_stress_desktop`). SPU2 comparison is a design topic; software mixing stays (real-PS2 timing is F4.01).
- ◐ **F2.05** Engine 5 · Gameplay framework — entities, collision, animation, saves, UI · `1.19%` 🟡 *Attention: everything is done (the room exercise runs on Linux, Windows and PS2; Android is the fourth target) except the Android on-device run (`tools/ci/android_device.sh`), deferred by the user to a later phase.*
  - Acceptance 2: done: SpriteRenderer, SpriteAnimator, Tilemap with solid / one-way / slope tile flags, BoxCollider2D sweep (slopes, one-way, DropThrough, Down+South auto-drop), Physics2D layer matrix (`Physics2D.json`, PH2D section), Camera2D (dead zone, bounds, pixel snap), DontDestroyOnLoad, UI (text, image, button), SceneManager, Save store (files; PS2 memory card `mc0:/RENSGA`). Exercise on `samples/platformer2d`: 40x14 room, slope run, one-way planks, 7 coins, Walker and Hopper enemies (stomp or lose 1 HP), save point; bot `RoomBot` clears it on host (`tests/sample`, 20 tests), desktop and PS2.
  - Evidence: desktop `room cleared coins=7 score=1836`, `alloc-check frames=8999 allocs=0`; PCSX2 `room cleared coins=7 score=1832`, `saved best=700 coins=5`, `save readback best=1832 coins=7`, `alloc-check frames=1100 allocs=0`.
  - Not ticked: the exercise says "all four targets" and Android has not run the room (waiting for the phone run, `tools/ci/android_device.sh`; the player starts on Title, tap the South zone). Also open: no `icon.sys` is written on the PS2 card (saves work but show no icon in the BIOS browser; real-card check is F4.01), no real-pad PS2 play-through (PCSX2 sandbox has no controller), 2D editor gizmos (F3.01), PSP and PSX 2D (F4, F9).
- [x] **F2.06** API 1 · Math and core types — vector3, Quaternion, Mathf, Random · `0.79%` 🔶
- [x] **F2.07** API 2 · Object model — gameObject, Component, Transform · `1.19%` 🔶
- [x] **F2.08** API 3 · Lifecycle, Time, Debug, Application — execution order and global services · `0.79%` 🔶

**Gates**

- ◐ **F2.G1** The no-per-frame-allocation check passes on every target. 🟡 *Attention: allocs=0 on Linux, Windows, PSP and PS2 except the Android on-device run (`tools/ci/android_device.sh`), deferred by the user to a later phase.*
  - Evidence (`alloc-check ... allocs=0`): Linux headless 400 frames, Linux desktop 299 (sprite stress) and 8999 (sample room), Windows (wine + Xvfb) 199, PSP (PPSSPPHeadless) 239, PS2 (PCSX2, local) 300 (stress), 1100 (sample room), 300 (VRAM stress), 200 (audio stress). Android: built, not run (no device); waiting for the phone run (`tools/ci/android_device.sh`).
- [x] **F2.G2** Determinism test: the same scene and inputs give the same state hash on Linux and PSP.
  - Evidence: `tests/determinism/walk.json`, 400 frames, Linux hash == PSP hash `05d7d1bf`, all 400 per-frame trace hashes equal (`tools/ci/determinism.sh`).

### F3 · Editor core — 9.9% (cumulative 23.2%)

Roadmaps: [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [x] **F3.01** Editor 2 · Scene, hierarchy and inspector — gameObjects, Transforms, gizmos · `1.59%`
  - Evidence: `rensga_sceneedit_tests`, `editor_scene_ui`, `editor_callbacks_ui`, `exercise_patrol`. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.02** Editor 3 · Asset database and project window — .meta files, importers, VRAM map · `1.59%`
  - Evidence: `rensga_asset_tests`, `editor_assets_ui` (dropped PNG, ps2 override clut8 to clut4). Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.03** Editor 4 · C++ scripting workflow — behaviours, hot reload, compile errors · `1.98%`
  - Evidence: `editor_script_build_ui` (frames keep advancing during a build, error file:line), `exercise_hot_reload`, `editor_play_hot_reload`, `rensga_editor_tests` game module tests. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.04** Editor 5 · Play mode and Game view — play, pause, step, revert, profile previews · `1.19%`
  - Evidence: `editor_play_psp`, `editor_play_platformer`, `exercise_play_revert`. Known gap: no audio in editor play. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.05** API 4 · Coroutines, Invoke and events — waiting over time, C# events, UnityEvent · `0.99%`
  - Evidence: `rensga_core_tests` coroutine tests (allocs=0), `rensga_cooker_tests` door/UnityEvent, `exercise_door`. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.06** API 5 · Input — axes, buttons, actions, rumble · `0.59%`
  - Evidence: `rensga_core_tests` input tests, `pad_psp_map`, `pad_sdl_events`, `input_cook`, `exercise_input_desktop`. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- [x] **F3.07** API 11 · Editor-facing attributes — serializeField, Range, RequireComponent · `0.79%`
  - Evidence: `rensga_inspector_tests`, `rensga_strip_tests`, `reflect_scan_codegen`. Console and CI checks passed natively (no Docker, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`): psp booted to frame 240 with allocs=0, determinism MATCH, psp_gpu_check 0 unknown syscalls, editor_tour, windows build + wine alloc-check 0, ps2 build + PCSX2 boot (allocs=0).
- ◐ **F3.08** Editor 11.5 · Build support modules — native toolchains and emulators from the Hub, no Docker · `1.19%` 🟡 *Attention: Windows-hosted PSP toolchain is built by .github/workflows/toolchains.yml, not published yet; android-build was never installed or run (installing it means accepting Google's Android SDK license, which only the user may do: `rensga modules install android-build --accept-licenses`, then `RENSGA_TOOLCHAIN=native tools/ci/android.sh`); the PPSSPP Linux entry is `manual`; Windows-host installs and the Windows postInstall (`sh -c`) are untested.*
  - Evidence: psp-build, ps2-build, windows-build, build-tools installed from the catalog; `run_local.sh` natively passed linux_host, psp, editor_tour, determinism, psp_gpu_check, windows, windows_run, ps2, ps2_check, android_device ("built, not run"); android SKIPPED with the clear "android-build is not installed, it needs Google's Android SDK license" message (exit 77; in CI it fails instead); 57/57 ctest incl. `editor_modules_ui` (Hub screenshots).
  - Done when: every console and cross build runs from pinned, sha256-verified native toolchain archives installed per user (Hub > Modules or `rensga modules install <id>`; catalog `tools/modules/manifest.d/`), the pipeline, the editor TargetModules and `tools/ci/*.sh` resolve toolchains through one runner (`RENSGA_TOOLCHAIN=auto|native|docker`), CI installs the same modules, and Docker is only an optional contributor backend. Windows archives we build ourselves stay "unpublished" until `.github/workflows/toolchains.yml` releases them.

**Gates**

- [x] **F3.G1** A scene saved, reloaded and saved again produces no diff.
  - Evidence: `exercise_scene_roundtrip` (save, reopen, save: no diff), `editor_scene_ui`, every shipped scene saves unchanged (`rensga_sceneedit_tests`).
- [x] **F3.G2** Every inspector edit can be undone and redone.
  - Evidence: `rensga_inspector_tests` (`test_inspector_model.cpp`): every edit path undone and redone, JSON identical; `editor_inspector_ui`.
- [x] **F3.G3** The Play dropdown's "Play in editor" works and Stop restores the scene.
  - Evidence: `editor_play_psp`, `editor_play_platformer`, `exercise_play_revert` (Stop restores the scene), `rensga_editor_tests` session tests.
- [x] **F3.G4** A machine without Docker installs PSP build support from the Hub or CLI, builds the PSP template and plays it in PPSSPP.
  - Evidence: fresh `RENSGA_HOME`, Docker permission denied: `rensga modules install psp-build build-tools`, `rensga build --target psp templates/psp-3d` (EBOOT.PBP 597962 bytes), `rensga play --target psp` with `RENSGA_TOOLCHAIN=auto` ran to quit-after with heartbeats. Hub screenshots: `build/nm-accept0/tests/editor/modules_ui/` (`ctest editor_modules_ui`).

### F4 · 2D slice everywhere + PSP base — 8.3% (cumulative 31.5%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [Editor](docs/rensga-engine/roadmaps/editor.html) · [PSP Engine](docs/rensga-engine/roadmaps/psp-engine.html) · [PSP Template](docs/rensga-engine/roadmaps/psp-template.html) · [PS2 Template](docs/rensga-engine/roadmaps/ps2-template.html)

- ◐ **F4.01** Engine 6 · 2D vertical slice on all targets — a small real game, shipped four ways · `1.59%` 🟡 *Attention: Android phone run deferred (user decision). The APK was not built on the development machine either (android-build module not installed, Google's license is the user's to accept; `tools/ci/android.sh` exits 77 there); CI builds it.*
  - Note: real-PS2 hardware testing moved to F8.02 (Engine 8) and F8.G3; F4 uses PCSX2. The Android phone run stays deferred.
  - Evidence (acceptance run, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`, `RENSGA_CI_OUT=build/f4-accept/ci`): the game is `samples/platformer2d` (Title, three rooms joined by exits, lives, game over and continue, pause on Start, results, HUD, per-scene music, saves, anchored UI, PS2 card `icon.sys`); `rensga_platformer_tests` 42 tests incl. the full-game bot at uneven frame rates and a pad replay. PS2 in PCSX2 (`ps2_check`): `game complete coins=19 time=2020`, `rensga: prof frames=60 ...`, `memory card icon: icon.sys ok, rensga.ico ok`, `save selftest ... readback=1020`, `alloc-check frames=3000 allocs=0`, screenshot `build/f4-accept/ci/ps2-check/ps2_slice.png`. Linux desktop (`linux_host`, softpipe): `game complete coins=19 time=1890`, `alloc-check frames=1499 allocs=0`, `build/f4-accept/ci/linux-slice-run/linux_{title,results}.png`. Windows under wine (`windows_run`): `game complete coins=19 time=1903`, `allocs=0`, `build/f4-accept/ci/windows/windows_slice.png`. Android: not built locally (SKIP 77), phone run deferred. Cross-target QA checklist: "Cross-target QA checklist" in `samples/platformer2d/README.md` (the roadmap exercise).
  - Done when still open: the Android run on a phone (`tools/android/run_device_check.sh`, the player starts on Title, tap the South zone).
- [x] **F4.02** PSP Engine 0 · pspdev toolchain and PSP target — preset, EBOOT.PBP, bundled PPSSPP, CI · `0.59%` 🔶
  - Evidence: PPSSPP is pinned to 1.20.4 (`tools/modules/manifest.d/ppsspp.json` and `tools/ci/build_ppsspp_headless.sh`, which fails if they disagree). `tools/ci/psp.sh`: dev ELF boots to frame 240, `alloc-check frames=239 allocs=0`. `tools/ci/psp_eboot.sh`: the release EBOOT.PBP from `rensga build --target psp` (PRX, mksfoex, pack-pbp) boots in PPSSPPHeadless, `PPSSPPHeadless version: 1.20.4 (pinned 1.20.4)`, `scene loaded objects=21 pickups=5`, `quit after 240 frames`. Exercise `tools/ci/psp_hello.sh` (one script: build the shared hello world as an EBOOT and launch it in PPSSPP): `rensga hello: done frames=120`, `allocs=0`. `editor_tour` played in PPSSPP and built an EBOOT.PBP. `validate()` refuses `ramBytes` above the PSP-1000 limit (32 MiB) and a non-16-bit framebuffer.
  - Not run here: the GitHub runner's source build of PPSSPP 1.20.4 (`build_ppsspp_headless.sh`); if it fails there, `PPSSPP_TAG=v1.17.1` in the workflow reverts.
- ◐ **F4.03** PSP Engine 1 · PSP platform layer — files, input, home button, memory, clock · `0.79%` 🔶 🟡 *Attention: the exercise says the input test "quits cleanly with Home". PPSSPPHeadless takes no input, so the Home button is a manual check in windowed PPSSPP (`rensga play --target psp` with the InputTest scene, then Home); everything else is verified. Tick this task after that check.*
  - Evidence: SDL2 on the PSP for game controller, timer and audio (never video); `tools/ci/psp_2d.sh input` (InputTest scene, `build/f4-accept/ci/psp2d/shots/psp_input_100.png`): `rensga: input sdl controller=PSP builtin joypad`, `mem budget=25165824 used=350800 arena=20643840 peak=351024`, `alloc-check frames=129 allocs=0`; the boot log also prints the data root probe (argv[0] folder, ms0, umd0, host0), `psp clock cpu=333 bus=166` and `rensga: prof` every 60 frames. Tests `pad_psp_map` (sceCtrl) and `pad_psp_sdl_map` (SDL controller), `-DRENSGA_PSP_NATIVE_PAD=ON` fallback. The exit and suspend callbacks only set flags; the suspend/resume counts are logged (not exercised headless).
- [x] **F4.04** PSP Engine 2 · GU 2D backend — display lists, 2 MB VRAM, 480x272 · `1.19%` 🔶
  - Evidence: `rensga_gu_tests` 12/12 (radix sort against a stable reference, clipping, flips, scaling, queue full, vertex layout, VRAM allocator); 3D template `meshes compact=14 float=0`, 5650, `alloc-check frames=239 allocs=0`; 2D slice `[gu] vram tex=19328/1261568 ram_tex=0`, `allocs=0` over 3499 frames; on-target self test of all five formats. Exercise `tools/ci/psp_2d.sh stress` (1000 sprites, last 60 frames avg/max us): PSP 480x272 in PPSSPPHeadless total 17508/66908, render 850/904, `allocs=0`; PS2 640x448 in PCSX2 total 16671/17376, render 451/463, `allocs=0`. Both are vsync-bound at about 16.7 ms and the times are emulator virtual time, not hardware. Screenshot `build/f4-accept/ci/psp2d/shots/psp_stress_250.png`.
  - Manual check: GLES pixels (`psp_2d.sh gles` and `psp_gpu_check.sh` check the log only: `Unknown syscall x0`, 0 GPU error lines; the frame dump under GLES is stale).
- [x] **F4.05** PSP Engine 3 · PSP cooker plugin — swizzled CLUT textures, GU vertex formats · `0.59%` 🔶
  - Evidence: `rensga_cooker_tests psp_` 11/11 (swizzle round trip, padding, minimum sizes, nibble order, 5551/4444 bit layout, size error naming the asset, compact mesh precision under 4 mm, compact meshes only for the psp target, RAM ceiling, determinism, cache reuse) and `cooker_cli_assets2d_psp`; the pack format is documented in `docs/rensga-engine/pack-format.md`. Exercise (same slice sources cooked for PSP and PS2): PSP pack 240,384 B (texture data 19,328 B) against PS2 268,288 B (17,280 B); both pick CLUT4 for all 6 textures; PSP texture data is 2,048 B larger from power-of-two padding.
- [x] **F4.06** PSP Engine 4 · Audio on PSP — shared mixer, streaming from memory stick · `0.40%`
  - Evidence: the shared `SdlAudio` backend on the PSP (`audio device want.freq=22050 have.freq=22050 ... driver=psp`), music streamed through read-ahead rings filled by a worker thread. Exercise `tools/ci/psp_2d.sh audio` (16 looping effects + streamed music, 600 frames, 1024-frame blocks, PPSSPP virtual time): 222 MHz `mix_us=4738/4740 cpu_permille=102 underruns=0`, 333 MHz `mix_us=3159/3160 cpu_permille=68 underruns=0`, `allocs=0` at both.
- [x] **F4.07** PSP Engine 5 · 2D slice on PSP — same game, new target · `0.79%`
  - Note: the real-PSP pass moved to F5.02 (PSP Engine 7) and F5.G1; F4 uses PPSSPP.
  - Evidence: `samples/platformer2d_psp` is generated by `make_sample.py --profile psp`; `rensga_platformer_tests` regenerates both profiles and diffs them byte for byte (only `ProjectSettings/Project.json` and `ConsoleTarget.json` differ), so no game code changed. `tools/ci/psp_2d.sh slice` in PPSSPPHeadless 1.20.4: `game complete coins=19 time=2004`, `alloc-check frames=3499 allocs=0`, six save files, one PNG per title, room and results (`build/f4-accept/ci/psp2d/shots/psp2d_sw_{20,400,1000,1800,end}.png`). `psp_2d.sh gles`: the game completes, 0 `Unknown syscall`, 0 GPU error lines. PSP vs PS2 vs desktop QA: the checklist in `samples/platformer2d/README.md`.
  - Manual check: GLES pixels (see F4.04).
- [x] **F4.08** Editor 7 · 2D toolset — sprites, tilemaps, 2D physics · `1.59%`
  - Evidence: Rigidbody2D (dynamic, kinematic, static; `rensga_core_tests rb2d` 11 tests; a scene cooked for ps2 runs 180 frames with 0 heap allocations), `TilemapEdit` and sprite slices (`rensga_tilemap_tests` 23), Tile Palette with paint, erase, rect, fill and rule brushes and per-tile flags, 2D painting with one undo step per stroke on the scene's own undo stack, Sprite Editor (grid and manual slices, pivots, names, Apply/Revert, undo), the SpriteRenderer sprite picker. Tests `editor_tile_palette_ui`, `editor_sprite_editor_ui`. Exercise `exercise_paint_level` (ctest label `f4_exercises`): paint a level, set flags, add TilemapCollider2D, build a player with Rigidbody2D and a script, Play in editor: `player rest: (64.0, 192.0, 0.0) after walking: (276.0, 192.0, 0.0)` (192 is the top of the painted ground), Stop `identical=1 dirty=0`. Screenshots in `build/f4-s2int/tests/editor/{tile_palette_ui,sprite_editor_ui,exercise_paint_level}/` (`palette.png`, `palette_slope.png`, `inspector_tilemap.png`, `sprite_editor.png`, `sprite_picker.png`, `painted.png`, `play.png`).
  - Not built (hook register): tilemap resize UI, rule-tile authoring UI, multi-layer palette.
- [x] **F4.09** PSP Template 5 · PSP 2D template and Hub picker — 480x272 tilemap room, a 2D and a 3D card per target API · `0.40%`
  - Done when: `templates/psp-2d` is one 480x272 tilemap room with a sprite player driven by the shared 2D platformer controller (written against `Scalar`, so PSX reuses it), a coin, a HUD and Camera2D; the Hub's Create Project shows a 2D and a 3D template card for every target API, and unbuilt ones are greyed out and name the phase that brings them; CI cooks the template and boots it in headless PPSSPP.
  - Evidence: `tools/ci/templates.sh`: both 2D templates cook for linux and run in `rensga_headless` (`alloc-check frames=400 allocs=0`, `hash c5a91ad3`) and in the desktop player; psp-2d boots in PPSSPPHeadless (`vram tex=13056/1261568 ram_tex=0`, `alloc-check frames=399 allocs=0`). `rensga_hub_tests` 5/5 and `templates_2d_generator`. Hub: `hub.json` lists six cards, psp-2d, psp-3d and ps2-2d selectable, psx-2d ("Coming in PSX Template 5"), psx-3d ("Coming in PSX Template 4") and ps2-3d ("Coming in PS2 Template 4") greyed out. Exercise (2D template to Play) `editor_hub_templates_ui`: creates a PSP 2D project, builds its scripts, Play in editor draws tiles and sprites (the player moves, Stop gives `identical=1 dirty=0 undo=0`); it fails on a refused texture upload. Screenshots `build/f4-s2int/editor_hub_templates_ui/{dialog_psp,dialog_ps2,dialog_psp_2d,play_psp,play_ps2}.png`, `build/f4-accept/ci/templates/psp-2d.ppsspp.png`.
  - Note: the Play step of the exercise runs in the editor (the Play dropdown default); the same room boots in PPSSPPHeadless through `templates.sh`, not through the editor's "Play on console".
- [x] **F4.10** PS2 Template 5 · PS2 2D template — 640x448 tilemap room, boot test in PCSX2 · `0.40%`
  - Done when: `templates/ps2-2d` is the 640x448 version of the PSP 2D template with the same shared controller; it cooks in CI and boots in PCSX2 locally (needs the user's own BIOS, like every PS2 run).
  - Evidence: `tools/ci/templates.sh`: ps2-2d cooked for ps2, ISO 2,994,176 B, PCSX2 `alloc-check frames=300 allocs=0` and heartbeat frame 360, screenshot `build/f4-accept/ci/templates/ps2-2d.pcsx2.png`; linux headless `hash c5a91ad3` (same as psp-2d: one shared controller); `editor_hub_templates_ui` creates a PS2 2D project and plays it (`play_ps2.png`). The PS2 card says console build and run wait for the PS2 editor module (F8).

**Gates**

- [x] **F4.G1** CI boots the PSP build in headless PPSSPP.
  - Evidence: `ci.yml` psp job runs `psp.sh`, `psp_eboot.sh`, `psp_hello.sh`, `psp_2d.sh slice`, `templates.sh`, `editor_tour.sh`, `determinism.sh` (3d and 2d) and `psp_gpu_check.sh` with no `|| [ $? = 77 ]` tolerance; the same scripts passed locally through `run_local.sh` on the pinned PPSSPPHeadless 1.20.4 (13 PASS, `allocs=0`, determinism `MATCH hash=968101ef`, 2D `MATCH hash=e634f035` with the per-frame trace equal over 2700 frames). The first GitHub run (which builds PPSSPP 1.20.4 from source) has not happened: the branch is not pushed.
- ◐ **F4.G2** The 2D slice runs on Windows, Linux, Android, PS2 and PSP from one codebase. 🟡 *Attention: Android phone run deferred (user decision); the APK is built in CI only.*
  - Evidence: one pack and one set of scripts: Windows (wine) `coins=19`, Linux desktop `coins=19`, PS2 (PCSX2) `coins=19`, PSP (PPSSPPHeadless) `coins=19`, all `allocs=0`; the PSP project differs from the PS2 one only in `ProjectSettings` (tested). Linux and PSP agree: `tools/ci/determinism.sh 2d` `per-frame trace equal over 2700 frames`, `MATCH hash=e634f035`, `allocs=0` on both sides (engine fix: equal `EXECUTION_ORDER` ties break by type name in `World::ensurePool`); the 3D walk still matches (`968101ef`). Android phone run: deferred.

### F5 · PSP track — 15.4% (cumulative 47.0%)

Roadmaps: [PSP Engine](docs/rensga-engine/roadmaps/psp-engine.html) · [PSP Editor](docs/rensga-engine/roadmaps/psp-editor.html) · [PSP API](docs/rensga-engine/roadmaps/psp-api.html) · [Emulator Play](docs/rensga-engine/roadmaps/emulator-play.html)

- [x] **F5.01** PSP Engine 6 · GU 3D backend — hardware T&L, lights, skinning, clipping · `1.98%` 🔶
  - Evidence: GU renderer: up to 4 hardware lights per draw (top 4 chosen per object by `engine/core` `selectLights`), materials, nearest repeat textures, linear fog, back-face culling from `kMeshWindingCCW`, CPU near-plane clipping for `kMeshNeedsNearClip` meshes, 8-weight hardware skinning per bone batch (`sceGuBoneMatrix`), C16 vertices with the decode folded into the model or bone matrices, VFPU only for render-side matrix products (`-DRENSGA_PSP_VFPU_RENDER`, output identical ON and OFF). Exercise `tools/ci/psp_3d.sh room lights12` on `samples/psp3d_walk` in PPSSPPHeadless (software GE): `rensga: gu draws=2 lights=4 batches=2 clipped=0 tris=1156`, `alloc-check allocs=0`, pixel probes (lit vs shadow, the walking character changes 57-78% of the middle band between dumps, fog darkens the far band by 16%); Lights12 `gu draws=13 lights=12 hwlights=51`. Screenshots `build/f5-integ/ci/psp3d/shots/{room_120,room_side_by_side,lights12_150}.png`. `psp.sh` (baked 3D template) unchanged.
  - Not checked: the look on real hardware (normal scaling of C16 meshes, guard band); listed in `docs/rensga-engine/psp-hardware-checklist.md` for F5.G1.
- ◐ **F5.02** PSP Engine 7 · PSP performance — budgets, VFPU, profiling · `1.19%` 🟡 *Attention: real PSP deferred (user decision 2026-10-31); everything else is done in PPSSPP.*
  - Evidence: per-scene budget lines from the FrameProfiler (`rensga: budget scene=RoomBot draws=2/300 tris=1156/20000 textures=3/64 vram=843968/2097152 status=ok`), VFPU in the render path only, runtime clock (`Application::SetCpuClock`). `psp_3d.sh perf` on the room in PPSSPPHeadless (virtual time, not hardware): 222 MHz busy 439 us (update 292, render submit 35), 333 MHz busy 349 us (update 251, render 23); both vsync-bound at 16.7 ms.
  - Done when still open: the frame-time and battery checks on a PSP-1000 and a slim or Go (`docs/rensga-engine/psp-hardware-checklist.md`).
- ◐ **F5.03** PSP Engine 8 · PSP packaging and release — eBOOT, saves, icons, open-source build · `0.79%` 🟡 *Attention: real PSP deferred; the release package is done and checked in PPSSPP.*
  - Evidence: `rensga build --target psp` writes PARAM.SFO with the title, disc ID and version from `ProjectSettings/Package/Package.json`, ICON0 (144x80) and PIC1 (480x272) from the project or generated from the brand tokens (`tools/psp/make_package_art.py`), optional SND0, every `music*.wav` and `LICENSES/`. Tests `psp_package_art`, `psp_package_eboot` (PBP read back with `tools/psp/pbp_info.py`), `tools/ci/psp_eboot.sh` (EBOOT boots to frame 240, `allocs=0`). Saves: the system save dialog (F5.15). Docs `docs/rensga-engine/psp-package.md`.
  - Done when still open: deploy and boot on a real PSP (`tools/psp/psplink_deploy.sh`, prepared, never run).
- [x] **F5.04** PSP Editor 0 · PSP target module — register PSP with the editor · `0.40%` 🔶
  - Evidence: `PspTargetModule` implements `toolchain()`, `emulator()`, `importRules()` and packaging; Window > Emulators shows PPSSPP status against the pin 1.20.4 and offers the install when Play needs it, driven only by `TargetModule` and the modules catalog. Test `editor_emulator_manager_ui` (missing, offer, cancel installs nothing, install, found at 1.20.4); screenshot `build/f5-integ/tests/psp_package/emulator_ui/offer.png`.
- [x] **F5.05** PSP Editor 1 · PSP project template — consoleTarget PSP, default scene, input map · `0.40%` 🔶
  - Evidence: `templates/psp-3d` and `templates/psp-2d` carry `ProjectSettings/Package/` and allow the shared 3D components (Light, RenderSettings, SkinnedMeshRenderer, Animation); the 2D generators emit them (`templates_2d_generator`); `tools/ci/templates.sh` cooks and boots both templates (psp-2d in PPSSPPHeadless, ps2-2d in PCSX2) with `allocs=0`. The one-nub input map is `samples/psp_services/ProjectSettings/InputActions.json`.
- [x] **F5.06** PSP Editor 2 · PSP import settings — texture, mesh and audio rules · `0.59%`
  - Evidence: the asset inspector's psp tab shows the target limits and the cooked result from the shared cooker code: textures (CLUT depth, 512 px limit with the downscale, power-of-two note, VRAM and RAM cost), meshes (float or 16-bit vertices, bone batches, RAM cost), audio (target rates 22050/11025 Hz, cooked rate and size). The cooker downscales oversized textures instead of failing and resamples console audio (windowed sinc). Tests `rensga_preview_rules_tests`, `cook3d`, `profile_resample_*`. Screenshots `build/f5-integ/tests/editor/preview_ui/{import_texture,import_model}.png`, `build/f5-accept/shots/audio_psp.png`.
- [x] **F5.07** PSP Editor 3 · PSP preview mode — 480x272, 16-bit color, live budgets · `0.79%`
  - Evidence: the Game view applies `PreviewRules`: 5-6-5 output with ordered dither, at most 4 lights per object, cooked PSP textures decoded through `decodeCookedTexture`, RAM/VRAM/polygon bars from a console-form cook of the open scene, device frame and safe area. Test `editor_preview_ui`; screenshot `build/f5-integ/tests/editor/preview_ui/gameplay.png`.
- [x] **F5.08** PSP Editor 4 · PSP inspector rules — profile gating and validation · `0.59%`
  - Evidence: inspector fields are greyed with a reason from `InspectorRules.fieldRules` and the profile features (the hint text matches `RENSGA_REQUIRES_FEATURE`); a scene validation list built from the cooker's checks (lights per object, bones per batch, joints, texture budget), click selects the object. Lights12: 12 warnings "is reached by N lights, the psp profile draws at most 4 per object". Tests `editor_preview_ui`, `rensga_preview_rules_tests`; screenshots `validation.png`, `gating_skin.png` in `build/f5-integ/tests/editor/preview_ui/`.
- [x] **F5.09** PSP Editor 5 · Build and Run in PPSSPP — one click to a running EBOOT · `0.79%` 🔶
  - Evidence: File > Build and Run (Ctrl+B) builds the release EBOOT and runs it in PPSSPPSDL: cold about 29 s, warm 0.8 to 2 s on `templates/psp-3d`; the game boots from the EBOOT folder (`data root=umd0:/`). Test `editor_build_run`.
- [x] **F5.10** PSP Editor 6 · PSP debugging and profiling — logs, profiler feed, PPSSPP debugger · `0.79%`
  - Evidence: the Profiler panel reads the game's `rensga: prof`, `budget`, `gu`, `mem` and `clock` lines during console Play and Play in editor (frame-time graph, draws and triangles against the budget, CPU MHz); "Open PPSSPP debugger" connects to the WebSocket debugger (`cpu.status` answered) after "Relaunch with debugger". Tests `editor_dock_emulator_ui`, `editor_profiler_feed_ui`; screenshot `build/f5-integ/tests/editor/dock_emulator/profiler.png`.
- [x] **F5.11** PSP Editor 7 · PSP samples and docs — sample project and getting-started page · `0.79%`
  - Evidence: `samples/psp3d_room` (lit room, Lights12) and `samples/psp3d_walk` (scripts: nub-driven third-person camera, RoomBot walk), both generated by `tools/samples/gen_blocky.py` (own assets, `--check` OK); `samples/psp_services`; `docs/rensga-engine/getting-started-psp.md`. `psp_3d.sh` runs the sample in PPSSPPHeadless.
- [x] **F5.12** PSP API 0 · PSP profile definition — what the PSP profile allows · `0.40%` 🔶
  - Evidence: profile features and limits are data (`tools/cooker/data/profile_defaults.json`, ConsoleTarget `features` and `limits`, CTG2 in the pack, generated header); gated calls fail to compile with a hint (`RENSGA_REQUIRES_FEATURE`); the cooker refuses components whose feature is missing, naming the scene. Exercise `tools/ci/api_profile.sh`: every gated call fails with its hint on ps2, psx and a featureless psp project, e.g. "No on-screen keyboard on this profile; read the text from UIText / a UI field instead." Generated table `docs/rensga-engine/api-availability.md` (ctest `api_availability_doc`).
- ◐ **F5.13** PSP API 1 · PSP input and app events — one nub, Home button, suspend · `0.40%` 🔶 🟡 *Attention: PPSSPP cannot raise the Home popup or a suspend, so those reach `OnApplicationPause` only through injection here; the real events wait for F5.G1.*
  - Evidence: `OnApplicationPause(true/false)` from the power callback, the Home popup poll and `injectApplicationPause` (dev control `apppause`); the frame clock resets on resume. PPSSPPHeadless: injected pause gives `OnApplicationPause(true)` then `(false)`. PPSSPPSDL: `ButtonPreference` 0/1 gives `confirm=east|south` and the system dialogs follow it; InputCam on the nub and shoulders moves the camera (yaw 0 -> 150 -> -151) and the player. Default one-nub actions: `samples/psp_services/ProjectSettings/InputActions.json`. `tools/ci/psp_services.sh` (headless, sdl, input).
  - Done when still open: Home and suspend/resume on a real PSP (F5.G1 checklist). F4.03's Home quit stays open for the same reason.
- [x] **F5.14** PSP API 2 · Rendering API on PSP — light limits, materials, skinning · `0.79%`
  - Evidence: shared 3D built ahead of F6 (user decision 2026-10-31): Light (directional, point), Material (`.mat.json`, glTF materials), SkinnedMeshRenderer, legacy Animation (Play, Stop, wrap modes), glTF skins and clips resampled at cook, skins split into batches of at most 8 bones; per-object top-4 lights, integer-tick clip sampling, frustum culling; implemented in GL (desktop, editor) and GU. Tests `cook3d` (12), core `lighting_*`, `anim_*`, `cull_*`, `gl_lit_*`. Determinism `tools/ci/determinism.sh 3dlit`: `MATCH hash=20a3e8ce`, `skinhash MATCH 283fc98d`, per-step skin trace equal over 240 steps; on the cooked room (`psp_3d.sh det`) `MATCH hash=964f6e68`, `skinhash ce188aa3`. Exercise: the room and Lights12 on PSP and desktop (`room_side_by_side.png`). PS2 GS lighting waits for F8.
- [x] **F5.15** PSP API 3 · PSP system services — save dialog, keyboard, language, battery · `0.59%`
  - Evidence: `TouchScreenKeyboard` (system OSK), `Save::setMode(SystemDialog)` (LISTSAVE/LISTLOAD dialogs, AUTOSAVE/AUTOLOAD in Auto), `Application::systemLanguage`, `SystemInfo` battery. PPSSPPSDL: `GameLanguage` 2/4 gives `language=2 (French)` / `4 (German)`, OSK name entry `name=Ab1...`, the save dialog writes `SAVEDATA/RENSGASLOT0/DATA.BIN` and loads it back; PPSSPPHeadless AUTOSAVE `save ok=1`, `load bytes=7 name=Rensga`, `battery=100%`. `tools/ci/psp_services.sh`; screenshots `build/f5-psp-sys/ci/psp-services/shots/{osk_open,osk_typed,save_dialog}.png`.
- [x] **F5.16** PSP API 4 · Performance API on PSP — cPU clock, memory stats, VFPU · `0.40%`
  - Evidence: `Application::SetCpuClock` (`clock set=333 cpu=333 bus=166 ok`, read back), `SystemInfo` memory and VRAM (`memory system=19MB used=107888 graphics=2MB graphics_used=839744`), VFPU in the GU render path only. `allocs=0`. 222 vs 333 MHz numbers: see F5.02.
- [x] **F5.17** PSP API 5 · Audio API on PSP — same API, PSP-safe defaults · `0.40%`
  - Evidence: `AudioSource.priority` (0 most important): the mixer steals the least important voice, then the oldest, never a more important one; the voice limit comes from the ConsoleTarget; the cooker warns when a scene starts more AudioSources than voices, naming the scene; console packs resample `.wav` to the profile rate (22050 Hz on PSP). Tests `audio_priority_*`, `profile_audio_voice_warning_names_the_scene`, `profile_resample_*`.
- [x] **F5.18** PSP API 7 · PSP docs and API tests — profile badges, tests in PPSSPP · `0.59%`
  - Evidence: `tools/ci/psp_api.sh all`: the API test game (`tests/psp_api`) passes 31 checks on the host and 32 in PPSSPPHeadless (`psp_api: DONE pass=32 fail=0 platform=PSP`); the fail demo (`sizeof(long) == 8`) passes on the host and is caught on the PSP. API pages with PSX/PSP/PS2 badges in `docs/rensga-engine/api/` (generated, ctest `api_pages_doc`).
- [x] **F5.19** Emulator Play 0 · Emulator launch contract — cLI args, sandboxed configs, BIOS · `0.40%` 🔶
  - Evidence: `LaunchSpec` headless, env, sandboxed memory stick (`<project>/Library/Emulators/ppsspp`), debugger flag (`RemoteDebuggerOnStartup` patched into the sandbox ini), version check against the pin at every launch; the per-emulator contract is `docs/rensga-engine/emulator-launch.md` (PCSX2 and PCSX-Redux rows describe F8/F9). Tests `pipeline_emu_play_launcher`, `editor_emu_play`.
- [x] **F5.20** Emulator Play 1 · Play, Pause, Stop — toolbar buttons driving the emulator · `0.59%` 🔶
  - Evidence: the Stop bug is fixed (the session stays Running until PPSSPP exits); Pause, Step and Resume through `host0:/rensga.ctl` (SIGSTOP fallback): paused at frame 120, step 5 -> 125, step 1 -> 126, resumed at 126; Play/Stop x10 leaves no PPSSPP process. Tests `pipeline_devctl`, `pipeline_emu_play_cli`, `editor_emu_play` (screenshot `paused.png`).
- [x] **F5.21** Emulator Play 2 · Fast iteration path — skip disc images, load assets from host · `0.59%` 🔶
  - Evidence: dev Play boots the ELF with the cooked pack linked in place; the compile and the configure are skipped when nothing changed. Second Play with no change: pipeline ready in 40 ms, game booted in 3.8 s; after one asset change 47 ms / 3.8 s; after one script change 3.7 s. Logged as `pipeline: stage <name> <ms>`.
- [x] **F5.22** Emulator Play 3 · Console log and crash handling — game output in the editor console · `0.40%` 🔶
  - Evidence: Console filters (level, source, text search), deduplicated game lines, exit state Exited, Crashed (signal, exit code, guest CPU exception) or Hung (no `rensga: frame` heartbeat), a banner with "Relaunch with debugger", `Library/Logs/last-play.log`; CLI exit 3 on a crash and 4 on a hang. Tests `pipeline_emu_play_launcher`, `editor_emu_play` (`crashed.png`, `hung.png`).
- ◐ **F5.23** Emulator Play 4 · Docked Game view — emulator window inside the editor · `0.79%` 🟡 *Attention: the native-Wayland fallback was run only through `RENSGA_EMBED=wayland`, and the Win32 `SetParent` path is compiled, not run.*
  - Evidence: on X11 PPSSPPSDL is reparented into the Emulator panel (`rensga-xtool tree` shows it as a child of the editor window), focus follows the pointer, Undock/Dock work; floating fallback. Test `editor_dock_emulator_ui`; screenshot `build/f5-integ/tests/editor/dock_emulator/docked.png`.

**Gates**

- [ ] **F5.G1** Real-hardware pass on a PSP-1000 plus slim or Go. *(deferred, user decision 2026-10-31; prepared: `tools/psp/psplink_deploy.sh`, `docs/rensga-engine/psp-hardware-checklist.md`)*
- [x] **F5.G2** Review: no PSP-specific code leaked into `editor/core`. The TargetModule interface grew instead.
  - Evidence: `tools/check_editor_core.sh` ok and ctest `editor_core_console_agnostic` pass; the TargetModule interface grew (`importRules`, `toolchain`, `emulator`, `PreviewRules` colorBits/dither/lights/device frame, `InspectorRules.fieldRules`, `LaunchSpec` and `EmulatorLauncher` extras); preview, import and inspector rules are read from ConsoleTarget data, PSP code lives in `editor/targets/psp`.

### F6 · Full editor and API — 15.2% (cumulative 62.2%)

Roadmaps: [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F6.01** Editor 6 · Prefabs — instances, overrides, nesting, variants · `1.58%`
- [ ] **F6.02** Editor 8 · 3D toolset — meshes, materials, lights, cameras, audio · `1.58%`
- [ ] **F6.03** Editor 9 · Animation — clips, Animation window, Animator graph · `1.58%`
- [ ] **F6.04** Editor 10 · UI, input and project settings — canvas UI, input actions, tags and layers · `1.19%`
- [ ] **F6.05** Editor 11 · Build pipeline — build Settings, packaging, Build and Run · `1.19%` 🔶
- [ ] **F6.06** Editor 12 · Profiler and debugging — live stats from the running game · `1.19%`
- [ ] **F6.07** Editor 13 · Extensibility and polish — custom inspectors, data assets, samples · `1.19%`
- [ ] **F6.08** API 6 · Physics — rigidbody, callbacks, queries · `1.19%` 🔶
- [ ] **F6.09** API 7 · Rendering and camera — renderers, materials, particles, Gizmos · `1.19%`
- [ ] **F6.10** API 8 · Animation and audio — animator, AudioSource, mixer · `0.79%`
- [ ] **F6.11** API 9 · Scenes, assets and saves — sceneManager, Resources, save data · `1.19%`
- [ ] **F6.12** API 10 · UI API — buttons, text, sliders, navigation · `0.79%`
- [ ] **F6.13** API 12 · Platform and performance helpers — defines, profiling, pooling, budgets · `0.59%`

**Gates**

- [ ] **F6.G1** Every component type is tested through reflection, inspector, prefab override and the cooker.
- [ ] **F6.G2** Build and Run works from Build Settings for every enabled target.

### F7 · PSP template — 2.2% (cumulative 64.4%)

Roadmaps: [PSP Template](docs/rensga-engine/roadmaps/psp-template.html)

- [ ] **F7.01** PSP Template 0 · Primitive generator (shared) — gameObject > 3D Object menu · `0.59%` 🔶
- [ ] **F7.02** PSP Template 1 · Primitive character — hierarchy prefab, lit and textured · `0.40%` 🔶
- [ ] **F7.03** PSP Template 2 · Primitive test level — textured blocks, 4 lights, fog · `0.40%` 🔶
- [ ] **F7.04** PSP Template 3 · Character controller (shared base) — move, jump, one-nub camera, pickups · `0.59%` 🔶
- [ ] **F7.05** PSP Template 4 · Package the PSP template — 3D entry, CI boot test · `0.20%`

**Gates**

- [ ] **F7.G1** A first-time user goes from New Project (PSP) to Play without touching an SDK.

### F8 · PS2 track — 6.9% (cumulative 71.3%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [PS2 Template](docs/rensga-engine/roadmaps/ps2-template.html)

- [ ] **F8.01** Engine 7 · 3D renderer — ps2gl backend + matching GL look · `3.17%`
- [ ] **F8.02** Engine 8 · 3D gameplay and performance — characters, physics, animation, budgets · `1.98%`
- [ ] **F8.03** PS2 Template 0 · PS2 primitive settings — smoother tessellation, VU1 path · `0.20%`
- [ ] **F8.04** PS2 Template 1 · Primitive character and blob shadow — shared prefab, smoother look · `0.40%`
- [ ] **F8.05** PS2 Template 2 · Primitive test level — bigger layout, textures, fog · `0.59%`
- [ ] **F8.06** PS2 Template 3 · Dual-analog controller — right-stick camera, rumble · `0.40%`
- [ ] **F8.07** PS2 Template 4 · Package the PS2 template — 3D entry, boot test without public BIOS · `0.20%`

**Gates**

- [ ] **F8.G1** The PCSX2 launcher joins the emulator launch contract, with the BIOS prompt (no BIOS is ever bundled).
- [ ] **F8.G2** A self-hosted runner with a maintainer's private BIOS boots the PS2 template on every engine change.
- [ ] **F8.G3** Real-hardware pass on a PS2.

### F9 · PSX track — 23.1% (cumulative 94.5%)

Roadmaps: [PSX Engine](docs/rensga-engine/roadmaps/psx-engine.html) · [PSX Editor](docs/rensga-engine/roadmaps/psx-editor.html) · [PSX API](docs/rensga-engine/roadmaps/psx-api.html) · [PSX Template](docs/rensga-engine/roadmaps/psx-template.html)

- [ ] **F9.01** PSX Engine 0 · PSn00bSDK toolchain and PSX target — pS-EXE, BIN/CUE, bundled PCSX-Redux · `0.79%`
- [ ] **F9.02** PSX Engine 1 · Fixed-point core — same core systems, integer math · `1.19%`
- [ ] **F9.03** PSX Engine 2 · PSX platform layer — no SDL: pad, CD, memory card, vsync · `0.79%`
- [ ] **F9.04** PSX Engine 3 · GPU 2D backend — ordering tables, VRAM, 320x240 · `1.19%`
- [ ] **F9.05** PSX Engine 4 · PSX cooker plugin and VRAM packer — tIM-style textures, CLUTs, VAG, sectors · `1.19%`
- [ ] **F9.06** PSX Engine 5 · SPU audio and CD streaming — hardware voices, XA music · `0.99%`
- [ ] **F9.07** PSX Engine 6 · 2D slice on PSX — same game code, smaller assets · `0.99%`
- [ ] **F9.08** PSX Engine 7 · GTE 3D backend — transforms, sorting, subdivision, lights · `1.98%`
- [ ] **F9.09** PSX Engine 8 · PSX performance and streaming — scratchpad, area streaming, budgets · `1.19%`
- [ ] **F9.10** PSX Engine 9 · PSX packaging and release — bIN/CUE, memory card icon, licenses · `0.59%`
- [ ] **F9.11** PSX Editor 0 · PSX target module — register PSX with the editor · `0.40%`
- [ ] **F9.12** PSX Editor 1 · PSX project template — consoleTarget PSX, 320x240, starter scene · `0.40%`
- [ ] **F9.13** PSX Editor 2 · PSX import settings and VRAM map — bit depth, CLUT sharing, live VRAM view · `0.99%`
- [ ] **F9.14** PSX Editor 3 · PSX preview mode — wobble, affine warp, sorting, dither · `0.99%`
- [ ] **F9.15** PSX Editor 4 · PSX inspector rules — gating and validation · `0.59%`
- [ ] **F9.16** PSX Editor 5 · Build and Run in PCSX-Redux — one click to a booting disc image · `0.79%`
- [ ] **F9.17** PSX Editor 6 · PSX debugging and profiling — pCSX-Redux debugger, budget reports · `0.79%`
- [ ] **F9.18** PSX Editor 7 · PSX samples and docs — sample project, getting started, credits · `0.59%`
- [ ] **F9.19** PSX API 0 · PSX profile and fixed-point rules — what scripts can and can't do · `0.59%`
- [ ] **F9.20** PSX API 1 · Fixed-point math API — vector3, Quaternion, Mathf, same names · `0.79%`
- [ ] **F9.21** PSX API 2 · Rendering API on PSX — 3 lights, blend modes, segmented meshes · `0.79%`
- [ ] **F9.22** PSX API 3 · Physics on PSX — kinematic, simple shapes, queries · `0.59%`
- [ ] **F9.23** PSX API 4 · Audio and music API on PSX — sPU voices, XA tracks, residency · `0.59%`
- [ ] **F9.24** PSX API 5 · Scenes and saves on PSX — area streaming, memory card blocks · `0.59%`
- [ ] **F9.25** PSX API 7 · PSX docs and API tests — fixed-point guide, tests in PCSX-Redux · `0.59%`
- [ ] **F9.26** PSX Template 0 · PSX primitive settings — low tessellation, vertex color, fixed-point · `0.20%`
- [ ] **F9.27** PSX Template 1 · Segmented primitive character — box body, sphere head, cylinder limbs · `0.40%`
- [ ] **F9.28** PSX Template 2 · Primitive test level — grid ground, platforms, ramps, pickups · `0.59%`
- [ ] **F9.29** PSX Template 3 · Fixed-point character controller — move, jump, camera, pickups · `0.40%`
- [ ] **F9.30** PSX Template 4 · Package the PSX template — 3D entry, CI boot test · `0.20%`
- [ ] **F9.31** PSX Template 5 · PSX 2D template — 320x240 tilemap room, CLUT4 tiles, fixed-point · `0.40%`
  - Done when: `templates/psx-2d` is the 320x240 version of the 2D template (CLUT4 tiles, the shared controller in 20.12 fixed-point) after PSX Engine 6 (F9.07); CI cooks it and boots it in PCSX-Redux with OpenBIOS.

**Gates**

- [ ] **F9.G1** Decide on SplashEdit collaboration before PSX Engine 0.
- [ ] **F9.G2** PCSX-Redux with OpenBIOS boots the PSX template in public CI.
- [ ] **F9.G3** PC and Android builds of a PSX game use fixed-point and match the PSX state hash.
- [ ] **F9.G4** Using `float` in PSX game code is a compile error with a hint.
- [ ] **F9.G5** Real-hardware pass on a PS1.

### F10 · Ship 1.0 — 2.0% (cumulative 96.4%)

Roadmaps: [Engine](docs/rensga-engine/roadmaps/engine.html) · [API](docs/rensga-engine/roadmaps/api.html)

- [ ] **F10.01** Engine 9 · Ship — packaging, open-source release, legal notes · `0.79%`
- [ ] **F10.02** API 13 · Docs, samples and stability — reference, Unity mapping, versioning · `1.19%`

**Gates**

- [ ] **F10.G1** Name and pequi icon cleared against existing trademarks.
- [ ] **F10.G2** Each console SDK's license checked; notices included in every package.
- [ ] **F10.G3** Bundled emulators ship unmodified, each with its license and source link.
- [ ] **F10.G4** Real-hardware pass on every console for the release build.
- [ ] **F10.G5** Docs site with the API reference and Unity mapping is live.
- [ ] **F10.G6** GitHub release of the editor for Windows and Linux, plus pages for the sample games.

### F11 · Visual scripting (Blueprint) — 3.6% (cumulative 100.0%)

Roadmaps: [Editor](docs/rensga-engine/roadmaps/editor.html) · [API](docs/rensga-engine/roadmaps/api.html)

Blueprint is a second scripting language, not a replacement for C++: every Blueprint compiles to C++ and goes through the same pipeline, so it runs on PSX, PSP and PS2 and on the Windows, Linux and Android builds. The user picks C++ or Blueprint when creating a project in the Hub.

- [ ] **F11.01** Editor 14 · Visual scripting — blueprint graphs, project language choice, debugging · `1.98%`
- [ ] **F11.02** API 14 · Blueprint compiler — graphs compiled to C++ for every profile · `1.58%`

**Gates**

- [ ] **F11.G1** A Blueprint project and the same game written in C++ give the same state hash on Linux and in the PSX, PSP and PS2 emulators.
- [ ] **F11.G2** Every public API function is a node, or is listed as excluded with a reason.
- [ ] **F11.G3** The Hub's Create Project offers C++ and Blueprint for every template, and C++ projects behave exactly as before.

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
