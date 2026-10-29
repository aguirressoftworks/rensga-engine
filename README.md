# Rensga Engine

<img src="docs/rensga-engine/rensga-icon.svg" alt="Pequi icon" width="64" align="right">

Open-source (MIT) engine, editor and Unity-style C++ API for making new games that ship as **PSX, PSP and PS2** disc images, plus native **Windows, Linux and Android** builds of the same game.

The icon is a pequi fruit, from the cerrado of Goiás, Brazil.

> **Status: design stage.** This repository has the design document and the 13 roadmaps. There is no engine, editor or API code yet.

## What it will be

Someone who knows Unity creates a project from a template, builds a scene, writes a C++ script and presses Play to see the game running in the right emulator, without touching a console SDK.

- **Runtime engine.** C++17, one core compiled per console profile, with console differences kept in backends (platform, renderer, audio, file system).
- **Editor.** Unity-style (hierarchy, inspector, prefabs, play mode, build settings), built on Dear ImGui, SDL2 and OpenGL, for Windows and Linux.
- **Scripting API.** Unity names in C++17 (`GetComponent`, `Instantiate`, `StartCoroutine`), with features gated by the project's profile.
- **Bundled emulators.** PCSX-Redux, PPSSPP and PCSX2, so Play can run the game on the emulated console.

The main idea is that **a game is data.** Scenes, prefabs, materials, input maps, budgets and console profiles are assets. Code only defines behaviour and the systems that read that data. See [RULES.md](RULES.md).

| | PSX | PSP | PS2 |
| --- | --- | --- | --- |
| Number type | 20.12 fixed-point | float | float |
| RAM / VRAM | 2 MB / 1 MB | 32 MB / 2 MB | 32 MB / 4 MB |
| Resolution | 320x240 | 480x272 | 640x448 |
| SDK | PSn00bSDK | pspdev + SDL2 | ps2dev + SDL2 |
| Output | BIN/CUE | EBOOT.PBP | ISO |
| Emulator | PCSX-Redux (OpenBIOS) | PPSSPP | PCSX2 (your own BIOS) |

## Repository layout

```
README.md            this file
TASKLIST.md          every task to 1.0: the MVP, then milestones F1–F10 weighted to 100%
RULES.md             project rules (design principles, architecture, licensing, docs)
AGENTS.md            working guide for AI coding agents
CLAUDE.md            Claude Code entry point (imports AGENTS.md and RULES.md)
docs/rensga-engine/
  index.html         home page: design doc + every roadmap
  README.md          guide to the docs folder
  rensga-icon.svg    the pequi icon (generated)
  design-doc/        design document (.md is the source, .html is generated) and SVG diagrams
  brand/             visual identity guide, palette.json, rensga-tokens.css, one-color icons (all generated)
  roadmaps/
    *.html           13 roadmap pages (generated)
    data/roadmaps.json   all roadmap content as JSON (generated)
    src/             roadmap and brand data (data_*.py) and the build scripts
```

## Reading the docs

Open `docs/rensga-engine/index.html` in a browser. Pages work offline with no server.

- **Design document:** `docs/rensga-engine/design-doc/rensga-engine-design-doc.md` (or its `.html`).
- **Visual identity:** `docs/rensga-engine/brand/visual-identity.html` (or `.md`). Covers the pequi palette, design tokens, logo rules, console target colors and the editor theme. CSS tokens are in `brand/rensga-tokens.css`.
- **Roadmaps:** click a phase to expand it. Each pill copies a ready-made prompt to paste into Claude. Dashed pills are exercises.

| Group | Roadmaps |
| --- | --- |
| Shared | Engine (shared core + PS2), Editor, Scripting API |
| PSP | PSP Engine, PSP Editor, PSP API |
| PSX | PSX Engine, PSX Editor, PSX API |
| Minor features | Emulator Play button, PSP / PSX / PS2 templates |

Planned build order: shared core, then the editor core with the TargetModule interface, then PSP (the cheapest full test of the pipeline), then PS2, then PSX.

[TASKLIST.md](TASKLIST.md) turns this into a checklist. The first step is an MVP where a 3D PSP prototype boots in PPSSPP from the editor's Play button and the `rensga play` command. After that, every roadmap phase is a task with its share of the 100%.

## Editing the docs

The HTML pages are generated. Edit the sources, then rebuild:

| To change | Edit | Rebuild with |
| --- | --- | --- |
| A roadmap | `docs/rensga-engine/roadmaps/src/data_*.py` | `python build.py ../..` |
| The design doc | `docs/rensga-engine/design-doc/rensga-engine-design-doc.md` | `python build_doc.py ../..` |
| The icon or page styling | `docs/rensga-engine/roadmaps/src/build.py` | `python build.py ../..`, then `python build_brand.py ../..` |
| Colors, tokens, logo rules | `docs/rensga-engine/roadmaps/src/data_brand.py` | `python build_brand.py ../..` |

Run the commands from `docs/rensga-engine/roadmaps/src/`. You need Python 3. `build_doc.py` also needs `pip install markdown`. `build_brand.py` checks contrast and that the palette matches the icon. If a check fails, it stops and writes nothing.

## License

MIT for the engine, editor, API, tools and templates, so games made with it can use any license. The bundled emulators are GPL and ship unmodified as separate programs. No Sony BIOS is ever distributed. A `LICENSE` file is still to be added.

Author: Breno Freitas Aguirres.
