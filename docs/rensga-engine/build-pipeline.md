# Build pipeline and Build Settings (F6.05, Editor 11)

One project builds to one console image and to the PC and phone players. The Build Settings window (File > Build Settings...) and `rensga build --platform <id>` run the same code, `tools/pipeline` (`Session::run`). Nothing in `editor/core`, `tools/pipeline` or `tools/cli` names a console: the platform table is data in `platform_build.cpp`, the PSP side stays a `TargetModule`.

## Platforms

| Id | Offered when | Toolchain | Cook target | Output (`<output folder>/<id>/`) | Build and Run |
| --- | --- | --- | --- | --- | --- |
| `psp` | the project profile is psp | `psp-build` module | `psp` | `EBOOT.PBP` (release) or the dev ELF folder, `game.rpak`, music, `LICENSES/` | PPSSPP (EBOOT.PBP for release, the dev ELF for a development build) |
| `ps2` | the project profile is ps2 | `ps2-build` module | `ps2` | `rensga.iso` (SYSTEM.CNF, RENSGA.ELF, GAME.RPK, MUSIC*.WAV) | PCSX2 through `tools/emulators/pcsx2_boot.sh` (local only). It needs your own BIOS: the editor asks once and keeps the path in `~/.config/rensga/emulators.json` (`{"pcsx2":{"bios":"<path>"}}`), used in place, never copied or committed. |
| `windows` | always | `windows-build` module (llvm-mingw, SDL2) | `windows` | `<name>.exe`, `SDL2.dll`, `game.rpak`, music | wine (private prefix `build/wine-prefix`, software GL) when installed, else "built, not run" |
| `linux` | always | the host compiler | `linux` | `<name>`, `game.rpak`, music | the player, as a child process |
| `android` | always | `android-build` module (Gradle, NDK) | `android` | `<name>.apk` (debug build, signed with the generated Android debug key) | `adb install` + start when a device is attached, else "built, not run" |

One project = one profile (design doc): a PSP project does not list PS2. `tools/ci/build_all.sh` therefore uses `templates/psp-2d` for the PSP, Windows, Linux and Android rows and `templates/ps2-2d` for the PS2 ISO.

## What one click does

1. **Validate.** The project and Build Settings are read; at least one scene must be enabled. Every scene in the build is checked against the target's budgets while it is cooked: memory (pack against RAM), VRAM (textures), triangles, objects, components per type, features, and audio voices (more `AudioSource`s starting with the scene than the target has voices blocks the build). A failure names the scene, its GUID and the heaviest object.
2. **Cook** for the platform (`cookProject`, target = the platform id; the PC and phone targets keep RGBA8 textures and the project's mesh format).
3. **Compile** the player with the project's scripts statically (reflection codegen on the host, CMake preset, native module). A pack the platform's renderer cannot draw is refused first (`tools/ci/pack_check.py`). The compile is skipped when no script, engine file or option changed (same stamp idea as the PSP module).
4. **Package** into `<output folder>/<id>`.
5. **Launch** (Build and Run only) through `PlayController` and `Session`, like Play on console: status, Console lines, Stop, hang and crash verdicts. `Session::lastOutput()` is the file a user opens, `builtNotRun()` and `lastNote()` say why nothing ran.

## Build Settings data

`ProjectSettings/Project.json` (schema in `project-settings.md`): `buildScenes` (every scene with its enabled flag, in build order), `scenes` (the enabled GUIDs, what the cooker reads, first = start scene), `development`, and two kept extras: `buildPlatform` (selected platform) and `buildOutput` (output folder, default `Builds`, relative to the project or absolute).

`development` on: the profiler stream is compiled in (`-DRENSGA_PROF_STREAM_LOG=ON` for consoles; the Linux player is started with `RENSGA_PROF_STREAM=tcp`, wine with `log`), PSP is built as a dev ELF. Off: PSP builds the release `EBOOT.PBP`.

## CLI

```
rensga platforms [project]                                       the platforms Build Settings offers
rensga build --platform linux|windows|android|psp|ps2 [--development] [--output DIR] [--run] [--quit-after S] [--headless] [--show-prof] [project]
rensga play  --platform ...                                      same as build --run --development
```

The CLI loads the project's own script module (`<project>/Library/ScriptAssemblies`, built on demand) so `rensga cook|build|play` works for any project; before F6 it only knew the scripts of the project it was built with (hook register row F6.05).

## --ui-event verbs (tests/editor/build_settings_ui.sh)

`buildsettings[:off]` | `bsplatforms` (status of each) | `bsplatform:<id>` | `bsdev:<0|1>` | `bsoutput:<folder>` | `bsscene:<add|remove|enable|disable|up|down>|<scene name>` | `bsscenes` | `bsbuild` | `bsrun` | `bsstop` | `bswait:<finished|running>|<secs>` | `bsstate` (prints `state= platform= dev= output= notrun= note=`). Emulator manager: `biosstate` | `biosset:<path>` | `biosdialog`. `RENSGA_BS_QUIT_AFTER=<s>` makes the Linux player and wine end by themselves.
