# hud_options

F6.04 (Editor 10) and F6.12 (API 10) exercise 2: a **HUD** and an **options menu**, built in the editor.

| Scene | What it has |
| --- | --- |
| `Hud` | A health bar (a `UIImage` with `fillMethod` Horizontal; the script only sets `fillAmount`), `SCORE 000125` and a `00:12` clock made with `UIText::SetFormat` (no heap), `HP 93/100`. On start it reads the saved volumes from `PlayerPrefs` and hands them to the mixer groups. |
| `Options` | Music and effects `Slider`s (dynamic `onValueChanged` -> `OptionsMenu.SetMusic/SetSfx`) and a Back button. A slider move reaches the mixer group at once and is stored in `PlayerPrefs`; Back saves and returns to the HUD. |
| `OptionsVerify` | `Options` with `verifyOnly = 1`: a restart that only reads the saved values back, logs them and quits. |
| `HudBot` | `Hud` plus `HudBot`, a scripted pad that opens Options, moves the sliders with pad navigation and presses Back. |

```sh
python3 samples/hud_options/tools/make_sample.py
python3 samples/hud_options/tools/author_in_editor.py build/host/editor/app/rensga-editor [samples/hud_options] [shot dir]
tools/ci/ui_menus.sh desktop psp      # run 1 (HudBot) then a restart (OptionsVerify) with the same save store
```

Expected: run 1 logs `options: music=80 sfx=30`, `options: saved music=80 sfx=30 ok=1` and `hud: volumes music=80 sfx=30 mixer music=80 sfx=30`; the restart logs `options: loaded music=80 sfx=30 mixer music=80 sfx=30 (hasSaved=1)`; `alloc-check allocs=0` on every run.
