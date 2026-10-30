# AGENTS.md

Guide for AI coding agents working in this repository. Read [RULES.md](RULES.md) too: it holds the project rules and they are not optional.

## What this repository is

Rensga Engine is a planned open-source (MIT) engine, editor and Unity-style C++17 API for making games that ship as PSX, PSP and PS2 disc images plus Windows, Linux and Android builds.

**The repository holds the docs plus the Step 1 MVP** (engine/core, engine/api, PSP and GU backends, cooker, pipeline, CLI, minimal editor, `templates/psp`, tests). Do not invent build commands, source files or features that don't exist; see TASKLIST.md for what is done.

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

Code (MVP; Docker image via `tools/docker/psp.sh`, PPSSPP via `tools/emulators/fetch_ppsspp.sh`):

```bash
cmake --preset linux-host -B build/host && cmake --build build/host -j4   # tests, cooker, pipeline, CLI, editor, headless sim
ctest --test-dir build/host                                               # all tests
build/host/tools/cli/rensga cook templates/psp                            # cook + budget check
build/host/engine/runtime/rensga_headless --pack templates/psp/Library/Cooked/psp/game.rpak --frames 400 --hash
xvfb-run -a build/host/tools/cli/rensga play --target psp templates/psp --json --quit-after 25   # dev build in Docker, boots in PPSSPP
build/host/tools/cli/rensga build --target psp templates/psp              # release: EBOOT.PBP in Library/Build/psp-release/
build/host/editor/app/rensga-editor --project templates/psp [--play --quit-after 30]   # --play exits 1 if the pipeline fails; changed Assets/Scripts rebuild this build tree and restart the editor (--no-auto-rebuild turns it off)
xvfb-run -a -s "-screen 0 1600x900x24" build/host/editor/app/rensga-editor --project templates/psp --screenshot out.png [--screenshot-frames N] [--select <object>] [--ui-event click|rclick|move:x,y|script:Name|focus|create:cube|select:Name|addscript:Name|save ...]   # editor PNG, never opens a window on the desktop
xvfb-run -a build/host/engine/runtime/rensga_desktop --stress-sprites 1000 --frames 300 --screenshot out.png   # desktop GL player (or --pack <game.rpak>); exits 4 if alloc-check allocs != 0
tools/emulators/pcsx2_boot.sh build/ci/ps2/rensga.iso --frames 120 --log /abs/log.txt [--shot out.png]   # PS2 boot in PCSX2 (local only, BIOS referenced in place via ~/.config/rensga/emulators.json)
# 2D gameplay sample (samples/platformer2d, PS2 profile 640x448): its scripts are compiled into a project build tree.
#   cmake --preset linux-host -B build/sample -DRENSGA_PROJECT_DIR=$PWD/samples/platformer2d && cmake --build build/sample -j4 --target rensga_desktop rensga-cooker
#   build/sample/tools/cooker/rensga-cooker cook --project samples/platformer2d --target ps2 --force
#   xvfb-run -a build/sample/engine/runtime/rensga_desktop --pack samples/platformer2d/Library/Cooked/ps2/game.rpak --scene Room --frames 120 --screenshot room.png
#   RENSGA_PS2_PROJECT=samples/platformer2d RENSGA_PS2_PACK=samples/platformer2d/Library/Cooked/ps2/game.rpak tools/ci/ps2.sh   # ISO; then pcsx2_boot.sh
tools/install_git_hooks.sh                                                # pre-commit guard: refuses BIOS/ROM dumps
tools/emulators/screenshot.sh                       # PPSSPP screenshot into docs/rensga-engine/mvp/ (needs a prior cook)
tools/check_editor_core.sh                                                # editor/core must not name a console
tools/check_api_rules.sh                                                  # API design rules (docs/rensga-engine/api-rules.md)
cmake --preset psp|windows|android|ps2 -B build/<x>                       # cross builds; run via tools/docker/{psp,windows,android,ps2}.sh (pinned images, sg docker fallback)
tools/ci/run_local.sh                                                     # every CI job locally (linux_host psp determinism psp_gpu_check windows windows_run android android_device ps2 ps2_check); exit 77 = skipped
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
- Console builds run in pinned Docker toolchains.
- Non-goals: C#/.NET, shader graphs, per-pixel lighting, URP/HDRP, DOTS, store integration, shipping any Sony BIOS, an editor on Android or consoles.

## Task list

`TASKLIST.md` is the work queue: Step 1 (MVP, tasks `MVP-xx`), then Step 2 (milestones F1–F10, one task per roadmap phase, with gates).

- Start from the lowest unticked task in the current step. Read its roadmap phase before writing code.
- Tick a task only when its "done when" is fully true. Note partial progress under the task.
- Never renumber task IDs. Add new tasks at the end of their block.
- Every MVP shortcut gets a row in the MVP hook register naming the Step 2 task that finishes it. A deviation from the design doc needs its reason there.
- Step 2 weights are `estimate / 237.5 weeks`, rounded so they sum to exactly 100%. If a roadmap phase is added, removed or re-estimated, update TASKLIST.md in the same change and recompute the weights and the Progress table.
- Update the Progress table's Status column when you tick tasks.

## When code is added

Follow the planned layout and dependency rules in RULES.md §2 and the design doc's "Architecture" section. Before writing a system, find its roadmap phase and follow the scope and constraints there. Keep this file's commands section up to date as real build and test commands appear.

## Environment notes

- Not a git repository yet. Don't run `git init`, commit or push unless asked.
- Python 3 is required for the docs build. `markdown` may not be installed. If it is missing, ask before installing it, or skip `build_doc.py` and say so.
