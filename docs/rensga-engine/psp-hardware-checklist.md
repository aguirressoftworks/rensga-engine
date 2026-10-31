# PSP hardware checklist (gate F5.G1)

These checks need a real PSP. They have **not been run**: the project has no PSP hardware, and every result so far comes from
PPSSPP 1.20.4. Each row says what to do, what a pass looks like, and what the emulator already showed. Fill in the result column
when you run it, with the date, the model (PSP-1000, 2000/3000 "slim", Go, street) and the firmware.

Related: F5.03 in `TASKLIST.md`, `tools/psp/psplink_deploy.sh`, `tools/psp/pbp_info.py`.

## Before you start

1. A PSP with custom firmware (needed to run homebrew), a memory stick and a USB cable.
2. Build the release package on the PC: `rensga build --target psp <project>` (or File > Build and Run in the editor, which also runs it
   in PPSSPP). The result is in `<project>/Library/Build/psp-release/`:
   `EBOOT.PBP`, `game.rpak`, `music*.wav`, `LICENSES/`.
3. Check the package before copying it: `python3 tools/psp/pbp_info.py <project>/Library/Build/psp-release/EBOOT.PBP`
   shows the PARAM.SFO title, `DISC_ID`, version, and the sizes of ICON0 (144x80) and PIC1 (480x272).
4. Copy the folder contents to the memory stick as `PSP/GAME/<game folder>/` (the folder name is free; the game also looks in
   `ms0:/PSP/GAME/RENSGA/`, next to the EBOOT, and in `host0:/`).
5. Or run it from the PC with psplink: `tools/psp/psplink_deploy.sh <project>` (read the header of the script first).
6. Turn the PSP's own settings to a known state: language English, battery above 50 %, no other game suspended.
7. Bring a second memory stick or a card reader: some checks end with a save you want to look at.

## The checks

| # | Check | How | Pass | Emulator result so far | Hardware result |
| --- | --- | --- | --- | --- | --- |
| 1 | Boot | Start the game from the XMB. | The XMB shows the title, ICON0 and PIC1 of the project. The game starts, the first scene draws, no black screen within 10 s. | `tools/ci/psp_eboot.sh`: the EBOOT boots to frame 240, `allocs=0`. | |
| 2 | Home quit | Press HOME, choose Exit Game. | Back in the XMB with no freeze; start the game again and it boots. | `tools/ci/psp_services.sh input` reports the Home key in PPSSPPSDL (F4.03). | |
| 3 | Suspend and resume | Slide the power switch (sleep), wait 10 s, slide it again. | The game continues where it was; sound resumes; `OnApplicationPause(true)` then `(false)` appear in `rensga.log`. | `tools/ci/psp_services.sh headless` injects the pause events; the real switch is untested. | |
| 4 | Lighting look of C16 normals | Run `samples/psp3d_room`, scene Room, stand in front of the walls. | Light on the walls and floor looks smooth. No banding, no flicker, the shadowed side of the boxes is darker, not black. Compare with the PPSSPP frames from `tools/ci/psp_3d.sh`. | Software-renderer frames from `tools/ci/psp_3d.sh`. | |
| 5 | Near-plane clipping look | Walk the character into the wall and walk the camera into the floor and the boxes. | Triangles that cross the camera are cut cleanly: no big polygons across the screen, no vertices flying off, no flicker at the edge. | Clipping runs in `GuRenderer` on the CPU; the unit tests cover the cut. | |
| 6 | Skinning | Scene Room, watch the walking character from 1 m. | Knees and elbows bend without gaps or spikes; no limb flips. | Frame dumps show the legs moving. | |
| 7 | Lights | Scene Lights12 (12 point lights, 4 used per object). | Only the nearest four light each object; moving an object makes the light change smoothly, no popping. | Pixel probes in `tools/ci/psp_3d.sh`. | |
| 8 | On-screen keyboard | Open the keyboard from a script (`TouchScreenKeyboard`). | The system keyboard appears over the game, text comes back, Cancel returns an empty result, the game keeps its frame rate afterwards. | `tools/ci/psp_services.sh sdl` drives it in PPSSPPSDL with pad keys. | |
| 9 | Save dialog | Save with `Save::setMode(SystemDialog)`. | The system save dialog shows the icon and title of the project, the data is written, loading it again reads the same values. | `tools/ci/psp_services.sh`: AUTOSAVE in PPSSPPHeadless, the system dialog in PPSSPPSDL. | |
| 10 | Clock 333 | `Application.SetCpuClock(333)`; read it back. | The clock reads 333 (or the nearest the firmware allows) and the frame time drops against 222 (see check 11). | PPSSPP accepts it; timing is not meaningful there. | |
| 11 | Frame times | The profiler line `rensga: prof` at 222 MHz and at 333 MHz in the 3D room. | Average frame under 33.3 ms at 333 MHz in the 3D room; write the 222 MHz value too. Compare with the budget in `ConsoleTarget.json`. | PPSSPP gives virtual time only. | |
| 12 | Music streaming | Play the 2D sample with its streamed music for 5 minutes. | No stutter, no dropout, no `music stream read stalled` line in the log. | `psp_2d.sh audio`. | |
| 13 | Memory | Read `rensga: mem budget=` and `[gu] vram tex=` lines after loading the biggest scene. | RAM use under the 24 MB budget on a PSP-1000 (32 MB total, part is the system); VRAM use under 2 MB. | Logged in PPSSPP. | |
| 14 | Package on each model | Run checks 1 to 3 on a PSP-1000, a slim and a Go (Go: install through the Go's memory). | Same result on each. | - | |

## Notes

- Do not put a BIOS, firmware file or ROM in the repository. Do not attach game dumps to a bug report.
- If a check fails, save `rensga.log` (the game writes it next to the EBOOT or to `host0:/`) and the model and firmware with the report.
- G1 is closed only when rows 1 to 14 carry a hardware result. Until then F5.02 and F5.03 stay half ticked.
