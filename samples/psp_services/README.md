# psp_services

PSP-profile test project for the F5 system services (F5.13 input and pause, F5.15 dialogs, F5.16 runtime numbers). Scenes:

| Scene | What it shows | Script |
| --- | --- | --- |
| `Services` | language, confirm button, battery, clock, memory lines, an OSK name entry, then a save through the system dialog | `ServicesDemo` (`useOsk = true`) |
| `ServicesAuto` | the same without the OSK (fixed name "Rensga"); the savedata dialog runs as AUTOSAVE / AUTOLOAD for PPSSPPHeadless | `ServicesDemo` (`useOsk = false`) |
| `InputCam` | a third-person camera and a player that need only the nub and the shoulder buttons | `CamRig` |

Every result is logged as `rensga: services <key>=<value>` (and `rensga: cam yaw=... player=...`), so a test can grep it.
`tools/ci/psp_services.sh` builds the three variants and checks them in PPSSPPHeadless and PPSSPPSDL.

```sh
tools/ci/psp_services.sh [headless|sdl|input|all]     # needs the psp-build module, PPSSPPHeadless, PPSSPPSDL, xvfb-run, ImageMagick `import`
python3 samples/psp_services/tools/make_sample.py     # regenerates the scenes, .meta files and ProjectSettings (scripts are hand-written)
```

## The default one-nub Input Actions set

`ProjectSettings/InputActions.json` is the set a PSP project can copy. The PSP has one analog nub, two shoulder buttons and four
face buttons, and no right stick, so everything works with those:

| Action | Source | Use |
| --- | --- | --- |
| `Move` (axis2) | `nub` | walk, relative to the camera |
| `CameraLeft` / `CameraRight` | `l` / `r`, alt `west` / `east` | orbit the camera (the face buttons Square / Circle are the alternative for a player who holds the pad differently) |
| `CameraUp` / `CameraDown` | `up` / `down` | camera pitch (D-pad) |
| `ConfirmSouth` / `ConfirmEast` | `south` / `east` | menu confirm, see below |
| `Menu` | `start` | pause menu |

## Confirm and cancel follow the system O/X setting

On a PSP sold in Japan (and for anyone who changed System Settings > Button Assign) Circle confirms and Cross cancels. The system
dialogs (OSK, save) already follow it, because the platform copies `BUTTON_SWAP` into every dialog. For the game's own menus:

1. Declare both buttons as actions, as above: `ConfirmSouth` (`south`, the Cross button) and `ConfirmEast` (`east`, the Circle button).
2. Pick one at run time with `Application::confirmButtonEast()`: `confirm = Application::confirmButtonEast() ? confirmEast : confirmSouth`,
   and treat the other one as cancel. `ServicesDemo::ConfirmPressed()` is the pattern.
3. Show the right glyph in hints from the same call.

`confirmButtonEast()` is false on every platform that does not report it (desktop, PS2, Android), so the default is Cross. A
gameplay action such as `Jump` is not remapped: only menus ask `confirmButtonEast()`.

## Notes for the emulators

- `ProjectSettings/Emulators/ppsspp/controls.ini` is the key map the SDL run uses: `W A S D` = nub, Left / Right arrows = L / R, Up / Down
  arrows = D-pad, Z = Cross, X = Circle, Return = Start, Space = Cross.
- PPSSPP cannot raise a suspend or the Home popup, so the test injects the pause: `host0:/rensga_test.txt` (dev builds only) with
  `pause_at=<frame>` makes `main_psp.cpp` call `injectApplicationPause(true)` at that frame and `(false)` 30 frames later.
  `savedata=auto` in the same file turns the save dialog into AUTOSAVE / AUTOLOAD (no UI), for PPSSPPHeadless.
