# Rensga Engine — design and roadmaps

Open-source (MIT) engine, editor and Unity-style C++ API for making games that ship as PSX, PSP and PS2 disc images plus native Windows, Linux and Android builds. The icon is a pequi fruit from the cerrado of Goiás, Brazil (`rensga-icon.svg`).

Open `index.html` in a browser to start.

## Contents

| Path | What it is |
| --- | --- |
| `index.html` | Home page linking the design doc and every roadmap |
| `design-doc/rensga-engine-design-doc.html` | Design document, styled for reading |
| `design-doc/rensga-engine-design-doc.md` | Same document as Markdown, for a repo or wiki |
| `design-doc/diagrams/` | Architecture and asset-pipeline diagrams as SVG |
| `brand/visual-identity.html` | Visual identity: pequi palette, design tokens, logo rules, editor theme |
| `brand/visual-identity.md` | Same guide as Markdown |
| `brand/rensga-tokens.css`, `brand/palette.json` | Design tokens as CSS custom properties and as data |
| `brand/rensga-icon-mono-*.svg` | One-color icons, dark and light |
| `api-rules.md` | Scripting API design rules (API 0), checked by `tools/check_api_rules.sh` |
| `target-module.md` | The TargetModule interface and how to add a console (Editor 0.5) |
| `roadmaps/*.html` | The 13 roadmaps: expandable phases, estimates, prompt pills |
| `roadmaps/data/roadmaps.json` | All roadmap content as data |
| `roadmaps/src/` | The data and the scripts that generate the pages |

## Roadmaps

| Group | Roadmaps |
| --- | --- |
| Shared | Engine (shared core + PS2), Editor, Scripting API |
| PSP | PSP Engine, PSP Editor, PSP API |
| PSX | PSX Engine, PSX Editor, PSX API |
| Minor features | Emulator Play button, PSP template, PSX template, PS2 template |

In a roadmap page, click a phase to expand it. Each pill copies a ready-made prompt to the clipboard so you can paste it into Claude; dashed pills are exercises.

## Regenerating the pages

The roadmaps follow the engine's own data-driven rule: content lives in data, pages are generated from it. To change a roadmap, edit `roadmaps/src/data_*.py`, then run from `roadmaps/src/`:

```
python build.py ../..        # roadmap pages, index and roadmaps.json
python build_doc.py ../..    # design doc HTML from the Markdown (needs: pip install markdown)
python build_brand.py ../..  # brand/ from data_brand.py; stops if a contrast check fails
```
