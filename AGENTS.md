# AGENTS.md

Guide for AI coding agents working in this repository. Read [RULES.md](RULES.md) too: it holds the project rules and they are not optional.

## What this repository is

Rensga Engine is a planned open-source (MIT) engine, editor and Unity-style C++17 API for making games that ship as PSX, PSP and PS2 disc images plus Windows, Linux and Android builds.

**The repository holds the docs plus the Step 1 MVP** (engine/core, engine/api, PSP and GU backends, cooker, pipeline, CLI, minimal editor, `templates/psp-3d`, tests). Do not invent build commands, source files or features that don't exist; see TASKLIST.md for what is done.

- `docs/rensga-engine/design-doc/rensga-engine-design-doc.md` is the main design reference. Read it before any design or architecture work.
- `docs/rensga-engine/roadmaps/` has 13 roadmaps (phases, estimates, Claude prompts), generated from Python data.
- `docs/rensga-engine/brand/` has the visual identity: pequi palette, design tokens, logo rules and the editor theme, all generated from `roadmaps/src/data_brand.py`. Use its tokens for any UI or page color.

## Source files vs generated files

| Kind | Files |
| --- | --- |
| **Edit these** | `docs/rensga-engine/roadmaps/src/data_core.py`, `data_psp.py`, `data_psx.py`, `data_minor.py`, `data_brand.py`; `roadmaps/src/build.py`, `build_doc.py`, `build_brand.py`; `design-doc/rensga-engine-design-doc.md`; `design-doc/diagrams/*.svg`; `docs/rensga-engine/README.md` |
| **Never edit by hand** | `docs/rensga-engine/index.html`, `rensga-icon.svg`, `roadmaps/*.html`, `roadmaps/data/roadmaps.json`, `design-doc/rensga-engine-design-doc.html`, everything in `brand/` |

The icon is the `ICON` string in `build.py`. Page CSS and JS are the `CSS` and `JS` strings there too.

## Commands

Code (MVP; toolchains via build support modules, `rensga modules install psp-build` (Docker via `tools/docker/psp.sh` is the optional contributor backend), PPSSPP via `tools/emulators/fetch_ppsspp.sh`):

```bash
# Build support modules (F3.08; catalog in tools/modules/manifest.d/<id>.json, installed per user in $RENSGA_HOME/modules or ~/.local/share/rensga/modules)
build/host/tools/cli/rensga modules list [--json]                         # modules, versions, installed state, host availability
build/host/tools/cli/rensga modules install psp-build [--accept-licenses] # download, verify sha256, extract, check; also remove|verify|path <id>
build/host/tools/cli/rensga modules env psp-build --shell sh              # env vars + PATH for a module (sh|cmd|json); tools/modules/toolchain_env.sh <id> evals it
build/host/tools/cli/rensga modules doctor [--json]                       # what is installed, what is missing, whether Docker is usable
# Hub modules --ui-event verbs (editor screenshots, tests/editor/modules_ui_check.sh): modules | modhub[:off] | modselect:<id> | modinstall:<id> | modaccept:<module or license id> | modremove:<id> | modverify:<id> | modlocate:<id>|<dir> | modstate[:<id>] | modwait | playconsole | modgate:<target> | moddialoginstall | moddialogcancel
# test env: RENSGA_MODULE_CATALOG=<dir> (fixture catalog), RENSGA_EDITOR_TEST_MODULE_DELAY_MS=<ms> (slows install progress for screenshots)
RENSGA_TOOLCHAIN=auto|native|docker                                       # auto (default) = native module, else Docker if usable, else a clear "install it" error
export RENSGA_HOME=$PWD/build/rensga-home                                 # dev: shared module install inside the repo (gitignored); tests use their own temp RENSGA_HOME and file:// fixtures, never the network
cmake --preset linux-host -B build/host && cmake --build build/host -j4   # tests, cooker, pipeline, CLI, editor, headless sim
ctest --test-dir build/host                                               # all tests
build/host/tools/cli/rensga cook templates/psp-3d                            # cook + budget check
build/host/engine/runtime/rensga_headless --pack templates/psp-3d/Library/Cooked/psp/game.rpak --frames 400 --hash
xvfb-run -a build/host/tools/cli/rensga play --target psp templates/psp-3d --json --quit-after 25   # dev build with the native psp-build module, boots in PPSSPP
build/host/tools/cli/rensga build --target psp templates/psp-3d              # release: EBOOT.PBP in Library/Build/psp-release/
build/host/editor/app/rensga-editor --project templates/psp-3d [--play --quit-after 30]   # --play runs the emulator pipeline (exits 1 if it fails); project scripts build into <project>/Library/ScriptAssemblies and hot-reload in the background on change (--no-auto-rebuild turns it off)
xvfb-run -a -s "-screen 0 1600x900x24" build/host/editor/app/rensga-editor --project templates/psp-3d --screenshot out.png [--screenshot-frames N] [--select <object>] [--ui-event click|rclick|move:x,y|script:Name|focus|create:cube|select:Name|addscript:Name|save ...]   # editor PNG, never opens a window on the desktop
# F3.02 asset verbs for --ui-event (Project window, asset inspector, VRAM map): asset:<Assets/..path> | assettab:<default|build target id> | assetset:<key>=<value> (e.g. format=clut4) | assetapply | assetrevert | assetundo | assetredo | assetrename:<path>|<new name> | assetmove:<path>|<folder> | assetdelete:<path> | assetdrop:<path>[|<object>] | search:<text> | folder:<Assets/..> | vram   # open Window > VRAM Map; the Inspector shows the asset until an object is picked
# F3.07 edit-mode callbacks: --ui-event editworld rebuilds the edit-mode World now and prints "scene: editworld valid= rebuilds= objects= error="; tests/editor/callbacks_ui_check.sh (ctest editor_callbacks_ui) shows the Patrol waypoint gizmos
# F3.01 inspector verbs for --ui-event: multiselect:A|B (object names) | addcomp:<Type> | rmcomp:<i> | moveup:<i> | movedown:<i> | resetcomp:<i> | copycomp:<i> | pastecomp:<i> | ctxmenu:<i>|<method> | setfield:<Type>.<field>=<json> | setobj:<name|active|tag|layer>=<json> | undo | redo   # act on the whole selection as one undo step; combine with shot:<png>
ctest --test-dir build/host -R "inspector"                                 # inspector model + gate F3.G2 (every edit undoes/redoes exactly), xvfb inspector screenshots (77 = skipped)
xvfb-run -a build/host/engine/runtime/rensga_desktop --stress-sprites 1000 --frames 300 --screenshot out.png   # desktop GL player (or --pack <game.rpak>); exits 4 if alloc-check allocs != 0
# F3.04 Play in editor (Play dropdown default; --play still runs the emulator pipeline) --ui-event verbs: openscene:<scene name> | showgame | playedit | pause | resume | pausetoggle | step[:N] (exactly N fixed frames, pauses first) | playwait:N (hold the queue until N game frames ran) | playkeys:<SDL key names,comma> (Game view input, `none` clears) | playset:<object>|<component index>|<field>|<json> | playmove:<object>|x,y,z | playdump:<object>[|<component>|<field>] (prints runtime values) | stopedit (prints `play-check: ... identical=1 dirty=0 undo=N`)
#   tests/editor/play_ui_check.sh <editor> <project> <scene> <object> <keys> <component|field|json> <workdir>   # Play, pause, step, edit, Stop, scene file byte-identical (ctest editor_play_psp, editor_play_platformer)
tools/emulators/pcsx2_boot.sh build/ci/ps2/rensga.iso --frames 120 --log /abs/log.txt [--shot out.png]   # PS2 boot in PCSX2 (local only; PCSX2 from `rensga modules install pcsx2` or RENSGA_PCSX2, BIOS referenced in place via ~/.config/rensga/emulators.json)
# F3.01 scene verbs for --ui-event (hierarchy edits, 2D mode, ID-buffer picking; output lines start with "scene:"): new:<name>[|<parent>] | selectmany:A,B | selectadd:A | reparent:A,B|<parent or empty>[|<before>] | dup | del | copy | paste | undo | redo | setpos:<name>|x,y,z | reload | savecopy:<file> | mode:2d|3d | framesel | pickobj:<name> | boxsel:x0,y0,x1,y1[,add] | dumpsel | dumptree | gizmos
# 2D gameplay sample (samples/platformer2d, PS2 profile 640x448): its scripts are compiled into a project build tree.
#   cmake --preset linux-host -B build/sample -DRENSGA_PROJECT_DIR=$PWD/samples/platformer2d && cmake --build build/sample -j4 --target rensga_desktop rensga-cooker
#   build/sample/tools/cooker/rensga-cooker cook --project samples/platformer2d --target ps2 --force
#   xvfb-run -a build/sample/engine/runtime/rensga_desktop --pack samples/platformer2d/Library/Cooked/ps2/game.rpak --scene Room --frames 120 --screenshot room.png
#   RENSGA_PS2_PROJECT=samples/platformer2d RENSGA_PS2_PACK=samples/platformer2d/Library/Cooked/ps2/game.rpak tools/ci/ps2.sh   # ISO; then pcsx2_boot.sh
# F3 exercises (samples/editor_tour, see its README): one ctest each, driven by --ui-event under xvfb, screenshots in the build dir
#   ctest --test-dir build/host -L f3_exercises --output-on-failure      # scene_roundtrip hot_reload play_revert door patrol input_desktop + editor_assets_ui
#   tools/ci/editor_tour.sh                                                # console side: rensga play/build --target psp on samples/editor_tour (native psp-build module + PPSSPP, exit 77 without)
build/host/tools/iso/rensga-iso -o out.iso -V VOLID [--order A,B] <dir>   # ISO 9660 writer (no xorriso); --list image.iso reads one back; ctest -R iso
tools/install_git_hooks.sh                                                # pre-commit guard: refuses BIOS/ROM dumps
tools/emulators/screenshot.sh                       # PPSSPP screenshot into docs/rensga-engine/mvp/ (needs a prior cook)
tools/check_editor_core.sh                                                # editor/core must not name a console
tools/check_api_rules.sh                                                  # API design rules (docs/rensga-engine/api-rules.md)
cmake --preset psp|windows|android|ps2 -B build/<x>                       # cross builds; run through the native modules (tools/modules/toolchain_env.sh <id>); tools/docker/{psp,windows,android,ps2}.sh is the optional contributor backend (RENSGA_TOOLCHAIN=docker)
tools/ci/run_local.sh                                                     # every CI job locally, native modules first (RENSGA_TOOLCHAIN=native, shared RENSGA_HOME=build/rensga-home; flags --install installs the modules the jobs need, --docker uses the contributor images); same jobs as .github/workflows/ci.yml, which installs the same modules with the CLI and never uses Docker; modules upstream lacks for Windows are built by .github/workflows/toolchains.yml (manual or tag toolchains-<id>-<version>); RENSGA_CI_OUT overrides build/ci; exit 77 = skipped
ctest --test-dir build/host -R "touch_|hot_reload|audio_stress|audio_sdl"                    # F2 host checks: touch pad + SDL finger events, desktop pack hot reload, audio-stress on SDL disk audio (skip with 77 without xvfb or a cooked pack)
build/host/tests/cooker/rensga_cooker_tests assets_cook_cache                              # per-asset cook cache (Library/Cache/<target>/; prints `cook: cooked <a>, cached <b>`)
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/tmp/a.raw xvfb-run -a build/host/engine/runtime/rensga_desktop --pack <game.rpak> --audio-stress --frames 300   # 16 voices + music, logs mix_us and underruns
cmake --preset ps2 -B build/ps2 -DRENSGA_PS2_VRAM_STRESS=ON                                # PS2 VRAM stress (24 textures); run the ISO with tools/emulators/pcsx2_boot.sh, PS2-local only
cmake --preset linux-host -B build/sample -DRENSGA_PROJECT_DIR=$PWD/samples/platformer2d   # sample build: platformer2d scripts in player/cooker (samples/platformer2d/tools/make_sample.py regenerates its assets)
tools/ci/android.sh                                                        # builds build/ci/android/rensga-player.apk (needs a cooked samples/platformer2d ps2 pack)
tools/android/run_device_check.sh                                          # install + run the APK on a USB phone via adb (LOCAL ONLY, never in CI)
tools/ci/determinism.sh                                                   # same walk.json on Linux and PSP: state hashes must match (DET_TRACE=1 compares every frame)
tools/ci/windows_run.sh                                                   # Windows build under wine + Xvfb, checks the alloc-check line
tools/ci/android_device.sh                                                # install + run on an adb phone; prints "built, not run" without one
tools/ci/ps2_check.sh                                                     # PS2 ISO in PCSX2, alloc-check line (LOCAL ONLY, needs your BIOS in emulators.json)
# every runtime main prints "rensga: alloc-check frames=<N> allocs=<K>" (K must be 0) and "rensga: frame <n>" every 60 frames
# heavy jobs on a small machine: wrap in flock /tmp/rensga-heavy.lock, nice -n 19, RENSGA_JOBS=2
```

Docs:

Run from `docs/rensga-engine/roadmaps/src/`. The argument is the output folder, `docs/rensga-engine`.

```sh
python build.py ../..        # roadmap pages, index.html, rensga-icon.svg, roadmaps.json
python build_doc.py ../..    # design doc HTML from the Markdown (needs: pip install markdown)
python build_brand.py ../..  # brand/: visual identity HTML + Markdown, palette.json, rensga-tokens.css, one-color icons
```

Always pass `../..`. With no argument the scripts write to `./out` instead.

`build_brand.py` first checks every token pair for contrast and that the icon's colors (`LOGO_PARTS`) appear in `build.ICON`. If a check fails, it prints the failing pairs and writes nothing. Fix the colors; don't lower the minimums.

**Check a change without touching the repo:** build into a temporary folder and compare.

```sh
python build.py /tmp/rensga-check && python build_brand.py /tmp/rensga-check \
  && diff -r /tmp/rensga-check ../.. | grep -v '^Only in ../..'
```

No output means the committed pages match the data. The build is deterministic, so any difference is a real change.

## Roadmap data format

Each `data_*.py` module defines roadmap dicts and exports them as `ROADMAPS = [...]`. `build.py` lists the modules in `GROUPS` (group name, blurb, roadmaps). A new module must be imported and added to `GROUPS`.

```python
ROADMAP = {
    "slug": "psp-engine",            # output file name: roadmaps/<slug>.html
    "title": "...",
    "summary": "...",                # also shown on the index card
    "stats": [("total", "~21 weeks"), ("phases", "9"), ...],  # "total" is shown on the index card
    "legend": ["name for group 1", "name for group 2", "name for group 3"],
    "phases": [
        (group, title, subtitle, estimate, note, topics, exercises),
        (group, title, subtitle, estimate, note, topics, exercises, "0.5"),  # optional label
    ],
}
```

- `group` is 1, 2 or 3. It picks the legend entry and the color (CSS classes `g1` to `g3` only).
- Phases are numbered automatically from 0. A phase with an 8th element uses it as its label and does **not** advance the counter. Editor's "0.5 TargetModule interface" works this way.
- `topics` and `exercises` are lists of `(label, prompt)`. Prompts are written to Claude in the first person ("Design ...", "Explain ..."). Exercise prompts start with "Give me an exercise:" or "Guide me through an exercise:". Prompts must stand on their own, because users paste them into a fresh chat.
- `stats` values are typed by hand. Keep the `"phases"` count and `"total"` in step with the phase list and with the design doc's roadmap index table.
- All text is HTML-escaped by the builder, so write plain text, not HTML.

## Brand data format

`data_brand.py` holds plain data. Colors are written as `"<ramp>.<step>"` (for example `"pequi.500"`), as a token name (for example `"primary"`), or as raw hex.

- `RAMPS`: the palette, as named ramps of `(step, hex)`.
- `LOGO_PARTS`: icon part -> palette color. These must match `build.ICON` exactly.
- `TOKENS`: `(name, light, dark, use)`. These become `--rg-<name>` in CSS.
- `CHECKS`: `(fg, bg, minimum)`, checked in both themes. Target tags and the `EDITOR` rows that carry text are checked automatically.
- `TARGETS`, `LOGO`, `TYPE`, `EDITOR` (the Dear ImGui dark theme), `IMAGERY`, `VOICE`: the guide's other sections.

The docs pages' own CSS (`CSS` in `build.py`) predates the brand tokens and doesn't use them yet.

## Cross-references

The design doc cites phases as "<Roadmap> <number>", for example "(Editor 3; PSX Engine 4)". Changing phase numbers, names or estimates means updating:

1. the phase list in `data_*.py`,
2. the roadmap's `stats`,
3. the design doc's text references and its "Roadmap index" table,
4. the generated files (rebuild both scripts).

Search before renumbering: `grep -n "PSX Engine" docs/rensga-engine/design-doc/rensga-engine-design-doc.md`.

## Domain facts to get right

- Profiles: PSX (20.12 fixed-point, 2 MB RAM, 320x240, PSn00bSDK, no SDL), PSP (float, 480x272, pspdev + SDL2, GU), PS2 (float, 640x448, ps2dev + SDL2, gsKit for 2D, ps2gl on VU1 for 3D).
- One project = one profile, always also built for Windows, Linux and Android.
- Emulators: PCSX-Redux with OpenBIOS (PSX), PPSSPP (PSP), PCSX2 with the user's own BIOS (PS2). PS2 cannot boot in public CI.
- The editor runs on Windows and Linux only: Dear ImGui (docking), ImGuizmo, imnodes, SDL2, OpenGL.
- Console and cross builds run in pinned native build support modules (psp-build, ps2-build, windows-build, android-build, ...), installed per user from the Hub or `rensga modules`. Users never need Docker; Docker is an optional contributor backend (`RENSGA_TOOLCHAIN=docker`).
- Non-goals: C#/.NET, shader graphs, per-pixel lighting, URP/HDRP, DOTS, store integration, shipping any Sony BIOS, an editor on Android or consoles.

## Task list

`TASKLIST.md` is the work queue: Step 1 (MVP, tasks `MVP-xx`), then Step 2 (milestones F1–F10, one task per roadmap phase, with gates).

- Start from the lowest unticked task in the current step. Read its roadmap phase before writing code.
- Tick a task only when its "done when" is fully true. Note partial progress under the task.
- Never renumber task IDs. Add new tasks at the end of their block.
- Every MVP shortcut gets a row in the MVP hook register naming the Step 2 task that finishes it. A deviation from the design doc needs its reason there.
- Step 2 weights are `estimate / 243.5 weeks`, rounded so they sum to exactly 100%. If a roadmap phase is added, removed or re-estimated, update TASKLIST.md in the same change and recompute the weights and the Progress table.
- Update the Progress table's Status column when you tick tasks.

## When code is added

Follow the planned layout and dependency rules in RULES.md §2 and the design doc's "Architecture" section. Before writing a system, find its roadmap phase and follow the scope and constraints there. Keep this file's commands section up to date as real build and test commands appear.

## Environment notes

- This is a git repository (branch `f3`, main branch `main`). Don't commit unless asked, and never push without the user's consent.
- Python 3 is required for the docs build. `markdown` may not be installed. If it is missing, ask before installing it, or skip `build_doc.py` and say so.
