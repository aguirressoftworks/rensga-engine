# End results

What Rensga Engine must deliver when the roadmap is finished: each demo game from [DEMO_GAMES.md](DEMO_GAMES.md), built from one project, for its console (run in the emulator) plus Windows, Linux and Android. Real-hardware passes are tracked separately (TASKLIST gates F5.G1, F8.G3, F9.G5, F10.G4).

## Result matrix

| Game | Console (emulator) | Windows | Linux | Android |
| --- | --- | --- | --- | --- |
| **Submersa** (survival horror) | ☐ PSX · BIN/CUE in PCSX-Redux with OpenBIOS | ☐ `.zip` (exe) | ☐ AppImage | ☐ APK |
| **Poeira** (arcade racing) | ☐ PSP · EBOOT.PBP in PPSSPP | ☐ `.zip` (exe) | ☐ AppImage | ☐ APK |
| **Pequi** (3D platformer) | ☐ PS2 · ISO in PCSX2 (your own BIOS) | ☐ `.zip` (exe) | ☐ AppImage | ☐ APK |

Tick a cell (☑) only when every check below passes for that build.

## Checks for every cell

1. **One project.** The build comes from the game's single project. No console-specific code in game scripts, and no second copy of the project.
2. **Built by the engine.** Made with `rensga build --target <id>` or the editor's Build button, using toolchains installed as build support modules (no Docker).
3. **Fits.** The cook and validate stages pass the console's budgets (memory, VRAM, SPU RAM, polygons, voices) with no override.
4. **Boots and finishes.** The game boots to its title screen. Then a scripted bot run plays the whole demo, start to end credits, with no crash or soft lock.
5. **Same game everywhere.** The bot run's final state hash is the same on the console build and on the Windows, Linux and Android builds.
6. **No per-frame allocation.** The log shows `rensga: alloc-check ... allocs=0`.
7. **Frame rate.** Holds the game's target (30 fps for Submersa, Poeira and Pequi) in the emulator, measured from the `rensga: prof` log.
8. **Saves.** Save, quit, reload and continue works: memory card on PSX and PS2, memory stick on PSP, the save folder on PC and Android.
9. **Input.** Plays fully with the console pad in the emulator, a gamepad on PC, and touch plus a gamepad on Android.
10. **Package is legal.** License files for the engine and every SDK are included. No BIOS, no Sony marks, no ripped assets. Third-party assets are credited in `THIRD_PARTY.md`.

## Per-game extras

| Game | Extra checks |
| --- | --- |
| Submersa | Fixed-point everywhere, including the PC and Android builds. Areas stream from disc with no visible stall. Saves use one memory card block with an icon. XA music plays. |
| Poeira | 6 cars and 4 tracks (plus reverse) are raceable. Time-trial ghosts save and load. Ad hoc multiplayer is optional; if it ships, it is checked with two PPSSPP instances. |
| Pequi | The hub turns green as rains return. Texture streaming into the 4 MB VRAM shows no missing textures. Rumble works on the DualShock 2 mapping. Progressive scan (480p) option. |

## Engine milestones each row needs

| Game | Needs |
| --- | --- |
| Submersa | F9 (PSX track), F6 (full editor and API), F10 (packaging) |
| Poeira | F5 (PSP track), F6, F10 |
| Pequi | F8 (PS2 track), F6, F10 |

## Status

| Date | Note |
| --- | --- |
| 2026-10-31 | Matrix defined. Engine at F4 (2D slice everywhere, PSP base). No demo game started. |
