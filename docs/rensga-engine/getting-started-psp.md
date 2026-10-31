# Getting started: your first PSP game

This page takes you from a fresh install to an `EBOOT.PBP` that runs in PPSSPP. You need Linux or Windows, about 2 GB of disk, and a network connection for the first install. You do not need Docker. Nothing here needs a real PSP; the last section says what only a real PSP can tell you.

## 1. Install the build support

Console builds run in pinned toolchain packages called build support modules. They install per user and every download is checked against a sha256 value in the catalog.

In the editor, open the Hub and choose Modules. Install **psp-build** (compiler, SDK, tools) and **ppsspp** (the emulator). The Hub shows the progress and the licenses; you accept each license before it installs.

From a terminal the same thing is:

```sh
rensga modules install psp-build
rensga modules install ppsspp
rensga modules doctor        # what is installed, what is missing
```

`rensga modules doctor` is the first thing to run when a build says a tool is missing.

PPSSPP is GPL. Rensga starts it as a separate program and ships it unmodified, with its license and a link to its source.

## 2. Create a project from a template

In the Hub choose Create Project, pick the PSP API, then the 2D or 3D template:

- **3D**: a small 3D scene with a player you walk with the nub and a camera that follows.
- **2D**: a platformer slice with tiles, sprites, a bot and audio.

The project is a normal folder: `Assets/`, `ProjectSettings/` and `Library/` (cache, not for version control). `ProjectSettings/ConsoleTarget.json` holds the PSP profile: 480x272, 32 MB of RAM, 4 lights per object, textures up to 512x512. A budget the scene does not fit is reported with the name of the asset, before anything runs.

To see a lit, skinned 3D scene, open `samples/psp3d_walk`. It has a character with a walking clip, three colored point lights, fog, and a camera you drive with the nub and the L and R buttons. The scene `Lights12` has twelve lights, more than the PSP draws on one object, on purpose.

## 3. Play

Press **Play** in the toolbar. The editor builds a development copy for the PSP, starts PPSSPP and shows its output in the Console. The first Play takes about 20 seconds; later Plays that change no code take a few seconds.

- **Pause** and **Step** stop the game and advance it by exact fixed frames.
- **Stop** ends the emulator.
- The Console filters by level and source (Editor, Game, Emulator) and searches the text. If the game crashes or hangs, a banner says so and offers "Relaunch with debugger". The last log is saved in `Library/Logs/last-play.log`.

The keys that stand for the PSP buttons are set in PPSSPP's controls screen. The nub, the shoulder buttons and the face buttons all have a key there.

From a terminal:

```sh
rensga play --target psp path/to/project
rensga play --target psp --headless path/to/project   # no window, for scripts
```

## 4. Build and Run, and the release EBOOT

The editor's Build and Run button does the same from the toolbar. The command line release build is:

```sh
rensga build --target psp path/to/project
```

It cooks the assets, compiles the game and packs `Library/Build/psp-release/EBOOT.PBP`. The `EBOOT.PBP` runs in PPSSPP (File, Load) and is the file you copy to a PSP with custom firmware, into `ms0:/PSP/GAME/<name>/`. The game data stays in the pack next to it.

## 5. Check your budgets

The running game logs these lines once per second (the numbers below only show the shape):

```
rensga: prof frames=60 total_us=... update_us=... render_us=... present_us=... audio_us=...
rensga: budget scene=Room draws=12/300 tris=3150/20000 textures=3/64 vram=1363968/2097152 status=ok
```

The vram value is in bytes. `status=OVER(...)` names what went over (draws, tris, textures, vram), and a warning line is logged once per scene. Triangles and VRAM limits come from the ConsoleTarget. The draw and texture limits are rules of thumb for the PSP, not hardware limits.

Two numbers to keep in mind for a 3D game:

- The PSP draws 4 lights per object (hardware limit). The renderer picks the strongest for each object, so scenes with many lights still draw; check that the important ones are chosen (set Light.renderMode to Important).
- A skinned mesh is drawn in batches of 8 bones. The cooker splits the mesh for you; fewer, larger draws are cheaper.

Set the CPU clock with `Application::SetCpuClock(222)` or `333`. PPSSPP reports a clock but its timing is not the PSP's, so measure speed on a real PSP.

## 6. Test your API calls on the PSP

`tools/ci/psp_api.sh` runs `tests/psp_api/ApiTests.cpp` on the host and in PPSSPP and prints `psp_api: PASS <name>` or `FAIL`. Copy the pattern for your own checks: a Behaviour that logs one line per check.

## 7. What only a real PSP shows

PPSSPP runs the same code, but some things need hardware: real timing and frame rate, the Home button and sleep, the 32 MB limit on a PSP-1000, the memory stick, battery, and sound latency. The checklist for a hardware pass is in `docs/rensga-engine/psp-hardware-checklist.md`.

## Where to go next

- [API pages](api/index.md) for Light, RenderSettings, SkinnedMeshRenderer, Animation, Application, TouchScreenKeyboard, SystemInfo, Save and AudioSource, with PSX, PSP and PS2 badges.
- [API availability by profile](api-availability.md).
- [Emulator launch contract](emulator-launch.md) and [pack format](pack-format.md) for how Play works inside.
