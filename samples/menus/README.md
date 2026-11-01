# menus

F6.04 (Editor 10) and F6.12 (API 10) exercise 1: a **title screen** and a **pause menu**, built in the editor and fully navigable with the pad.

| Scene | What it has |
| --- | --- |
| `Title` | Heading, Play / Options / Quit buttons, an Options panel (volume `Slider`, mute `Toggle`) that writes the Master mixer group. |
| `Game` | A running clock (`SetFormat("TIME {0:02}:{1:02}")`) and a pause menu (Resume, Quit to title). Start (the `Menu` action) or the cancel button opens and closes it; game time stops while it is open. |
| `TitleBot` | `Title` plus `MenuBot`, a scripted pad: it pushes the same navigation as a pad press (`UI::pushNav`) and walks Title -> Options -> Play -> pause menu -> Title, then quits. |

How it was made: `tools/make_sample.py` writes the project skeleton and empty scenes; `tools/author_in_editor.py` runs `rensga-editor` under xvfb with `--ui-event` verbs (`uicreate:`, `uiprops:`, `uievent:`, `addscript:`, `save`), so the scene files are what the editor saved: Canvas with the reference resolution 480x272, buttons whose `onClick` UnityEvents call `MenuFlow` methods, a slider whose `onValueChanged` is a *dynamic* listener (`MenuFlow.SetVolume(Scalar)` gets the slider value).

```sh
python3 samples/menus/tools/make_sample.py                      # skeleton (psp profile); --profile ps2 --out <dir> for the 640x448 variant
python3 samples/menus/tools/author_in_editor.py build/host/editor/app/rensga-editor [samples/menus] [shot dir]   # fill the scenes in the editor
tools/ci/ui_menus.sh desktop psp                                # navigation trace on the desktop and PPSSPPHeadless (ps2: local, PCSX2)
```

The navigation trace is the sequence of `rensga: ui focus <object>`, `rensga: ui click <object>`, `rensga: ui slider ...`, `rensga: ui toggle ...` and `rensga: menus: ...` lines; it must be identical on the desktop, PSP and PS2. The same scenes run on the PS2 profile (640x448) because the Canvas scales its 480x272 reference to the view.
