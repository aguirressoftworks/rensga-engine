# levels_save

F6.11 (API 9) exercise: two levels behind an asynchronous loading screen, and a save that survives a restart of the emulator or the player. PSP profile (480x272); `tools/make_sample.py --profile ps2` makes the PS2 copy (640x448) for the memory card run. UI only, no art: every picture is a flat `UIImage`, the sound `Assets/Resources/sfx/blip.wav` is generated.

| Scene | What it is |
| --- | --- |
| `Menu` (0) | Reads SaveSystem slot 0 ("CONTINUE LEVEL 2 COINS 10" or "NEW GAME"), counts the runs in PlayerPrefs, then starts the level. Holds the `Flow` object (`LevelFlow`), which survives every load through `DontDestroyOnLoad`. |
| `Loading` | Loaded ADDITIVELY over the menu. A bar and a percentage follow `AsyncOperation::progress()` of the level load. Listed in `additiveScenes`. |
| `Level1`, `Level2` | 48 flat tiles each, loaded with `LoadSceneAsync(.., Single)` and `setAllowSceneActivation(false)` until the loading screen has been up for `minLoadFrames` frames. |
| `Hud` | Loaded asynchronously and ADDITIVELY next to the level (coins and level number). Listed in `additiveScenes`. |

The bot plays one level per run (about 100 frames), writes slot 0 (`SaveSystem::Write`, version 2, with a title and a detail line), reads it back, and quits (`afterSave` 0; the PS2 copy idles instead because PCSX2 is stopped from outside). Run 1 is a new game and saves level 2 with 10 coins; run 2 prints `continue level=2 coins=10` and saves level 3; run 3 prints `all levels done`. A version 1 save file (no `lives`) is upgraded through the migration callback.

Every step logs a line `rensga: levels: ...`:

```
prefs runs=2   resources blip valid=1 entries=1   platform=PSP profile=PSP memory=<used>/<limit>
new game | continue level=2 coins=10 lives=3
loading level=1 progress=44 frames=11 done=0     activating level=1 after 90 frames     loaded level=1 scene=Level1
hud loaded scenes=2 active=Level1     level complete level=1 coins=10
save written slot=0 version=2 level=2 coins=10 ok=1     save readback ok=1 level=2 coins=10
```

```sh
tools/ci/levels_save.sh headless   # PPSSPPHeadless, two runs on one memory stick (needs the psp-build module); screenshots in $RENSGA_CI_OUT/levels-save/shots
tools/ci/levels_save.sh desktop    # the same two runs on the Linux player
tools/ci/levels_save.sh ps2        # LOCAL ONLY: PCSX2 with your BIOS, two boots sharing one memory card file
python3 samples/levels_save/tools/make_sample.py [--profile ps2 --out <dir>]   # regenerates scenes, settings and the sound; LevelFlow.cpp is hand-written
```

`ProjectSettings/Project.json` has `"additiveScenes": [Loading, Hud]`: the cooker flags them, so the component pools are sized for a level plus these two scenes.
