# platformer2d

The Engine 6 vertical slice: a small, complete 2D game that runs on PS2 (640x448), PSP (480x272, the same project with other
`ProjectSettings`), Windows, Linux and Android. It started as the one-room Engine 5 exercise and grew into three rooms with a title,
lives, a game over screen, a pause menu, a goal, a results screen, a HUD, music and sound effects, and saves. It uses the gameplay
framework in `engine/core` (`gameplay.h`, `scene_manager.h`, `save.h`) with C++ scripts in `Assets/Scripts` (Behaviour + REFLECT,
like `templates/psp-3d`).

## The game

`Title` -> `Room` (room 1) -> `Room2` -> `Room3` -> `Results`. `GameOver` follows the third lost life.

| Room | Scene | What is in it |
| --- | --- | --- |
| 1 | `Room` | 40 tiles wide. A slope run up to a plateau, two one-way planks, a walker, a hopper, a checkpoint flag, stairs up to the exit door. 7 coins. |
| 2 | `Room2` | 48 tiles wide. Two slope hills, a staircase of three one-way planks (jump up through them, Down + jump drops through), two hoppers, a checkpoint. 6 coins. |
| 3 | `Room3` | 50 tiles wide. The gauntlet: a pillar, one plank, three walkers and two hoppers on a long floor, a checkpoint, stairs up to the goal flag. 6 coins. |

- **Lives and HP**: 3 lives, 3 HP. An enemy touching you from the side costs 1 HP, knocks you back and gives about 1.5 s of blinking
  invulnerability. At 0 HP you lose a life and restart at the last checkpoint with full HP. At 0 lives: game over.
- **Stomp** an enemy from above to defeat it and bounce. **Coins** are optional score for the results; the exit is always open.
- **Checkpoint** (the flag): the respawn point for the rest of the room.
- **Exit door** (rooms 1 and 2) loads the next room; the **goal flag** (room 3) ends the game and shows the results.
- **Pause**: Start (Enter on a keyboard) stops the game time and shows Resume and Quit to title. A South press on Resume does not jump.
- **Game over**: Continue restarts the room you were in, from the moment you entered it, with 3 lives; or quit to the title.
- **Results**: game time, coins (of 19) and the best time ("NEW BEST TIME" when you beat it). Play again or go to the title.
- **HUD**: coins and room (top left), lives and HP (top centre), game time (top right), a message line in the middle.
- **Time** is game time: the fixed steps the player ran while a room was in play (the pause and the room transitions do not count),
  shown as m:ss at the target's fixed rate (60 Hz). It is never the wall clock, so a run is the same on every target.

### Saves

Saves go through `Save::SaveInt` (files in the platform's save folder; `mc0:/RENSGA/` on the PS2 memory card, with an `icon.sys`).

| Key | Meaning | Written |
| --- | --- | --- |
| `room` | the room to continue in (0 = nothing to continue) | when a room is entered; 0 when the game is finished |
| `coins`, `ticks`, `lives` | the totals when that room was entered | when a room is entered |
| `best_time` (+ `best_coins`) | game ticks of the fastest finished run | when a run finishes faster than the stored best |

The title shows **New Game**, and **Continue  ROOM n** when `room` > 0 (the button is greyed out and skipped by the focus otherwise).
Tests and bot runs start from an empty save: every test uses a fresh folder; for a desktop run set `XDG_DATA_HOME` to an empty folder.

### Audio

Effects (`jump`, `coin`, `hit`, `save`, `lose`, `clear`, `select`, `over`) are packed clips played through `GameState::Sfx`. The music is
streamed (`Streaming/*.wav`, not packed): the runtime starts `music.wav` at boot and each scene switches the one music voice with
`AudioMixer::playMusic` (`GameState::PlayMusic`): `music.wav` for the title, room 1 and the results, `music2.wav` for room 2, `music3.wav`
for room 3; the game over scene is silent. The same track is never restarted. A file missing on a target falls back to `music.wav`, so a package
that only carries `music.wav` (today's Android APK) still plays one track. Every `Streaming/*.wav` must be copied next to `game.rpak`
(the cooker does) and into the platform package (`tools/ci/ps2.sh` puts all of them on the disc root).

## Controls

| Input | Move | Jump | Pause |
| --- | --- | --- | --- |
| Keyboard | arrow keys or A / D | Space or Z | Enter |
| Gamepad | D-pad or left stick | South button (the bottom face button) | Start |
| Touch (Android, or any touch screen) | left-bottom D-pad zone | right-bottom South zone | the Start zone at the top |

Menus: D-pad up / down moves the focus (reading order on screen), South presses. The on-screen touch overlay shows the zones on Android always,
and on desktop once a finger has touched the window.

## UI anchors

Every `UIText`, `UIImage` and `UIButton` has an `anchor` (0 TopLeft = the old behaviour, 1 Top, 2 TopRight, 3 Left, 4 Center, 5 Right,
6 BottomLeft, 7 Bottom, 8 BottomRight). It names a point of the current view and the point of the element that sits on it; `x` / `y` are
offsets from it in virtual pixels. All scenes here use anchors, so **the same scenes fit 640x448 and 480x272**: the PSP project differs from the
PS2 project only in `ProjectSettings`. `tests/sample` checks every scene at both sizes (no text or widget leaves the view).

## Scenes

`Title`, `Room`, `Room2`, `Room3`, `GameOver`, `Results`, `TitleBot`, `InputTest` and `RoomBot`. `RoomBot` is room 1 plus the pilot (the way in that skips the
title). `TitleBot` is the Title plus an `AutoPilot` object that survives
scene loads and plays the whole game with no controller: it presses New Game, then reads the world every frame (climbs slopes, jumps planks,
stomps enemies, touches checkpoints, collects every coin, walks into the exit) and chooses Continue after a game over. It logs
`rensga: room cleared <n> ...` per room and `rensga: game complete coins=<c> time=<t> ...` (time in game frames) and, on the Results scene,
`rensga: bot finished the game`. Used by the headless test, the desktop run, and emulator runs where no pad exists. Its `quitAfterResultsFrames`
field (default 0 = keep running) makes automated runs exit. `InputTest` is the F3.06 input exercise (compact, 480x272-safe).

## Generating the project

Data files and art are generated by `tools/make_sample.py` (stdlib only, deterministic; the output is committed). The scripts are C++ in `Assets/Scripts`.

```sh
python3 samples/platformer2d/tools/make_sample.py                          # the PS2 project, written into samples/platformer2d
python3 samples/platformer2d/tools/make_sample.py --profile psp --out DIR  # the PSP project (480x272, 16-bit framebuffer) in DIR
```

`tools/gen_sfx.py` writes the sound effects and the music loop (called by `make_sample.py`). The PSP output is a complete project (scripts and
audio are copied) whose only differences are `ProjectSettings/Project.json` (`target`) and `ProjectSettings/ConsoleTarget.json`
(profile, resolution, budgets, texture formats, framebuffer, audio). `tests/sample` regenerates both and diffs them.
Do not commit a generated PSP project here; `samples/platformer2d_psp` belongs to the PSP slice task.

## Build and run

Desktop (Linux; use the same on Windows with the `windows` preset):

```sh
cmake --preset linux-host -B build/sample-game -DRENSGA_PROJECT_DIR=$PWD/samples/platformer2d
cmake --build build/sample-game -j4 --target rensga_desktop rensga-cooker
build/sample-game/tools/cooker/rensga-cooker cook --project samples/platformer2d --target ps2
build/sample-game/engine/runtime/rensga_desktop --pack samples/platformer2d/Library/Cooked/ps2/game.rpak            # play it
build/sample-game/engine/runtime/rensga_desktop --pack samples/platformer2d/Library/Cooked/ps2/game.rpak --scene TitleBot   # the pilot plays the game
```

Run headless GUI checks under `env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 xvfb-run -a ...`. Music and sounds need the `music.wav` the cooker copies next to `game.rpak`.

PS2 (needs the cooked pack above and the `ps2-build` module; PS2 does not run in public CI):

```sh
RENSGA_PS2_PROJECT=samples/platformer2d RENSGA_PS2_PACK=samples/platformer2d/Library/Cooked/ps2/game.rpak tools/ci/ps2.sh   # build/ci/ps2/rensga.iso
tools/emulators/pcsx2_boot.sh build/ci/ps2/rensga.iso --frames 600      # PCSX2 with your own BIOS (see the script header)
```

For an emulator run with no controller: `RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_AUTOSTART=TitleBot -DRENSGA_PS2_FORMAT_BLANK_CARD=ON -DRENSGA_PS2_CHECK_FRAMES=3000"`
starts the pilot at frame 30 (it plays about 2400 game frames in PCSX2) and formats the blank memory card PCSX2 starts with. The log then has the stomps,
`rensga: room cleared 1/2/3 ...`, `rensga: game complete coins=19 time=...` and `alloc-check frames=... allocs=0`.
The first open of the card writes `icon.sys` and `rensga.ico` (`engine/backends/platform/ps2/include/rensga/platform/ps2_icon.h`) so the BIOS
browser shows "Rensga Engine / Platformer 2D" with a gold cube; `tests/sample/test_ps2_icon.cpp` checks the byte layout on the host.

Android (APK only; the phone run is manual): cook the pack as above, then `tools/ci/android.sh` writes `build/ci/android/rensga-player.apk`
with `assets/game.rpak` and `assets/music.wav`.

## Cross-target QA checklist (F4.01 / gate F4.G2)

One codebase, one game: the same scripts and the same scenes run on PS2, PSP, Windows, Linux and Android. The checks below are scripts
(`tools/ci/run_local.sh <job>` runs the same file as CI). Every job builds and cooks **this** sample, not the 3D template.

| Target | How it is built | How it is checked | Script |
| --- | --- | --- | --- |
| Linux | host tree with the sample scripts built into `rensga_desktop` and `rensga-cooker` (`tools/ci/lib.sh` `slice_tools`) | `TitleBot` under xvfb with `GALLIUM_DRIVER=softpipe`: `game complete`, `bot finished the game`, `alloc-check allocs=0`, screenshots of Title and Results | `tools/ci/linux_host.sh` (public CI) |
| Windows | llvm-mingw cross build of `rensga_desktop.exe` with the sample scripts; the pack is cooked in the job and copied next to the exe with the music | the exe under wine + Xvfb, `TitleBot`: `game complete`, `alloc-check allocs=0`, Results screenshot | `tools/ci/windows.sh`, `windows_run.sh` (public CI) |
| Android | NDK + SDLActivity + `libmain.so` (the player with the sample scripts); the APK carries `game.rpak` and every `music*.wav` | APK built in public CI; exits 77 locally without the `android-build` module (Google's SDK license is yours to accept). The phone run is deferred. | `tools/ci/android.sh`, `android_device.sh` ("built, not run" without a phone) |
| PS2 | ps2dev cross build with the sample scripts; the pack is cooked for the ps2 profile; ISO with `GAME.RPK` and `MUSIC*.WAV`, refused if the GS cannot draw the pack (`tools/ci/pack_check.py`) | build only in public CI; locally in PCSX2 (your own BIOS): `game complete`, `prof` lines, `icon.sys ok`, a card save read back, `alloc-check allocs=0`, no texture upload error, Results screenshot | `tools/ci/ps2.sh` (public CI), `ps2_check.sh` (local) |
| PSP | the generated PSP project (`make_sample.py --profile psp`), cooked for psp (swizzled textures, compact meshes), EBOOT / ELF | PPSSPP headless: `game complete`, a screenshot per room, `allocs=0` | `tools/ci/psp_2d.sh` |

Manual pass, once per release candidate, on each target that has a screen (about 5 minutes each; the bot only proves the happy path):

- [ ] **Boot**: the Title shows New Game; Continue is greyed out with an empty save and reads `Continue  ROOM n` after a room was entered.
- [ ] **Controls**: the D-pad / arrows / left stick move, South / Space jumps, Start / Enter pauses, the pause menu shows Resume and Quit to title, and the South press on Resume does not jump. On Android the touch zones work and the overlay is visible.
- [ ] **Rooms**: room 1 to 3 are reachable through the exit doors; stomp, coins, the checkpoint flag and one-way planks (Down + jump drops through) behave the same on every target.
- [ ] **Lives**: lose three lives; Game Over offers Continue (restarts the room with 3 lives) and Quit.
- [ ] **Results**: the goal flag shows time, coins `n/19` and `NEW BEST TIME` on a first finish; Play again and Title both work.
- [ ] **Layout**: no text or button leaves the view (640x448 on PS2, Windows, Linux and Android; 480x272 on PSP). Nothing is a solid rectangle where a sprite should be.
- [ ] **Audio**: the music changes per room (`music.wav`, `music2.wav`, `music3.wav`), effects play, nothing crackles.
- [ ] **Saves**: quit in room 2 and relaunch: Continue starts room 2 with the same coins, lives and time. PS2: the memory card shows "Rensga Engine / Platformer 2D" with the gold cube in the BIOS browser.
- [ ] **Budget**: the game logs `alloc-check allocs=0` and a `rensga: prof` line every 60 frames (PS2, PSP, desktop).

Known differences, not bugs: the bot's game time differs per target (wall-clock frames: PS2 2090 to 2365, PSP 2083, desktop 1870 to 2167), and the
bot may miss a coin when frames are very slow (the wine run under softpipe collected 18 of 19). The headless test is the deterministic one.
Desktop alloc-checks run with `GALLIUM_DRIVER=softpipe`: llvmpipe's shader JIT allocates through `operator new` (a Mesa artefact, 147 allocations once).

### Evidence (Stage 3 acceptance, 2026-10-31, `RENSGA_TOOLCHAIN=native tools/ci/run_local.sh`)

| Target | Result |
| --- | --- |
| PS2 (PCSX2 2.8.2, ISO 3.8 MB, `ps2_check.sh`) | `game complete coins=19 time=2020`, `bot finished the game`, `rensga: prof` lines, `icon.sys ok, rensga.ico ok`, `memory card ready`, `save selftest previous=-1 wrote=1 readback=1020`, `alloc-check frames=3000 allocs=0`, no texture upload error. Screenshot: the Results scene. |
| Windows (wine + Xvfb, `windows_run.sh`) | `game complete coins=19 time=1903`, `alloc-check frames=1499 allocs=0` (an earlier slow run collected 18 of 19). Screenshot: the Results scene. |
| Linux desktop (xvfb, softpipe, `linux_host.sh` slice step) | `room cleared 1/2/3`, `game complete coins=19 time=1890`, `bot finished the game`, `alloc-check frames=1499 allocs=0`. Screenshots: Title and Results. |
| Android | APK step reaches the module check and exits 77 here (module not installed); the build runs in public CI; phone run deferred |
| PSP (PPSSPPHeadless 1.20.4, `psp_2d.sh slice`) | `game complete coins=19 time=2004`, `alloc-check frames=3499 allocs=0`, `vram tex=19328/1261568 ram_tex=0`, six saves, a screenshot of the title, each room and the results; `psp_2d.sh gles` completes with no `Unknown syscall` and no GPU errors (its pixels are a manual check). Linux vs PSP: `tools/ci/determinism.sh 2d` per-frame trace equal over 2700 frames (`MATCH hash=e634f035`). |

## Tests

`ctest --test-dir build/host -R platformer` runs `tests/sample`:

- the room mechanics: tile collision, slope run, one-way planks, enemy patrols, hurt, stomp, respawn, the checkpoint, the camera;
- the game flow: lives, game over and Continue, pause and resume, room exits, the goal and the results, Title with New Game / Continue, saves, game time;
- UI anchors: the math of all nine anchors, every scene drawn at 640x448 and 480x272, the HUD hanging from the view edges;
- the generator: the ps2 output equals the committed files, the psp output differs only in `ProjectSettings`, and the psp project cooks and plays through;
- the bot: it plays the whole game headless (also with uneven frame times), its pad is recorded as an `InputScript` and replayed on the plain
  Title scene: same coins, same game time, same stored best time;
- the PS2 memory card icon (`icon.sys` and the 3D icon byte layout).

## Audio (Engine 4)

`tools/gen_sfx.py` (stdlib only, deterministic) writes `Assets/Audio/*.wav` (packed as AUDI clips, played by the scripts through
`GameState::Sfx`) and `Streaming/music.wav` (8 s loop, 22050 Hz mono). Files in `Streaming/` are not packed: the cooker copies them next to
`game.rpak` and the mixer streams them in 4096-frame double-buffered chunks as `data:/music.wav` (PC: next to the pack; PS2: `MUSIC.WAV` on
the disc root, added by `tools/ci/ps2.sh`; Android: must be in the APK assets).
Stress: `rensga_desktop --pack <game.rpak> --audio-stress --frames 600`, or PS2 `RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_AUDIO_STRESS=600"`.
Both log `rensga: audio-stress voices=16 mix_us=<avg>/<max>`. `ConsoleTarget.json` has `"audio": {"sampleRate", "voices"}`.
