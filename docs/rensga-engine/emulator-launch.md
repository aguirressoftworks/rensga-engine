# Emulator launch contract (F5.19 to F5.22)

How the editor and the CLI start a console game in an emulator, control it and judge how it ended. The code is `tools/pipeline` (`EmulatorLauncher`, `LaunchSpec`) and one launcher per emulator in `editor/targets/<console>/`. Nothing in `editor/core`, `tools/pipeline` or `tools/cli` names an emulator.

Emulators are GPL: they always run as a separate process, unmodified, never linked.

## LaunchSpec

| Field | Meaning |
| --- | --- |
| `executable` | The game image to boot directly (the dev ELF). Empty = `<workingDir>/rensga_game.elf`. |
| `workingDir` | The dev build directory. It is the game's `host0:` root: logs, the control file and `game.rpak` live here. |
| `configDir` | `<project>/Library/Emulators`. The per-project emulator sandbox. |
| `memstickDir` | Where the emulator keeps saves and its config. Empty = the launcher's own folder inside `configDir` (PPSSPP: `<configDir>/ppsspp`), so nothing leaves the project. A set path is linked in. |
| `headless` | No window (CI). The launcher runs the emulator's windowless binary. |
| `embed` | The editor will reparent the window into a docked view. |
| `debugger` | Start the emulator with its debugger on (Relaunch with debugger). |
| `env` | Extra environment variables. |
| `logPath` | `<project>/Library/Logs/last-play.log`. Written when the session ends, also after a crash or a hang. |
| `hangTimeoutSeconds` | Alive, but no `rensga: frame` line for this long = Hung (default 15, 0 = off). The clock starts at the first game line. |
| `bootTimeoutSeconds` | No game line at all this long after the start = Hung (default 60, 0 = off). |
| `quitAfterSeconds` | Stop the emulator after N seconds (CI). That stop is not a crash. |

## Per-emulator rows

| Emulator | Binary | Headless | Sandbox | Debugger | Control | State |
| --- | --- | --- | --- | --- | --- | --- |
| PPSSPP (PSP) | `PPSSPPSDL`, version checked against `third_party/emulators/ppsspp/VERSION` (1.20.4) | `PPSSPPHeadless <elf> -r <workingDir> -l --graphics=software` | `XDG_CONFIG_HOME=<configDir>`, memory stick `<configDir>/ppsspp` | `[General] RemoteDebuggerOnStartup = True` in the sandbox `ppsspp.ini` (WebSocket on an ephemeral port, `ws://127.0.0.1:<port>/debugger`; `--debugger=0` for Headless) | `host0:/rensga.ctl` (below) | done (F5) |
| PCSX2 (PS2) | the `pcsx2` module's binary (`tools/emulators/pcsx2_bin.sh`) | none (xvfb for local checks; PS2 cannot boot in public CI) | a folder under `<configDir>/pcsx2`; the user's own BIOS is referenced in place, never copied | F8 decides | F8 fills this row: the same `rensga.ctl` file if the PS2 runtime can read the host path, otherwise PCSX2's own control interface | F8 |
| PCSX-Redux (PSX) | `pcsx-redux` with OpenBIOS | its no-GUI mode | a folder under `<configDir>/pcsx-redux` | its built-in Lua console / GDB server | F9 fills this row: Pause and Step through the emulator's own control interface, because the PSX runtime has no SDL or host filesystem | F9 |

A new launcher implements `EmulatorLauncher` (`start`, `stop`, `running`, `drainLog`, and the optional `pause`, `resume`, `step`, `sendCommand`, `exitState`, `exitCode`, `exitSignal`, `exitDetail`, `paused`, `saveLog`). A call the emulator cannot do returns false.

### PPSSPP details

- The version is read with `PPSSPPSDL --version` at every launch (cached per path). A different version gives a `warning: PPSSPP x differs from the pinned 1.20.4` line in the Console and the launch goes on.
- `ppsspp.ini` in the sandbox is patched, not replaced: first-run prompts off (only if the key is missing), `RemoteDebuggerOnStartup` set every launch. Keys a test or the user sets (`[SystemParam] GameLanguage`, `ButtonPreference`) are kept. `ProjectSettings/Emulators/ppsspp/controls.ini` is copied over the sandbox copy.
- `PPSSPPHeadless` ignores `ppsspp.ini`.
- A dev build boots the ELF straight from `Library/Build/psp-dev/`. `game.rpak` there is a link to `Library/Cooked/psp/game.rpak`, so a re-cook is seen by the game without a copy (a copy is the fallback where links are refused).

## Control channel: `host0:/rensga.ctl`

The launcher writes the file (temp file, then rename) and the game's `devControlPoll()` (`engine/core/src/dev_control.cpp`) reads it once per loop iteration while running (every 8th) and every iteration while paused. On the desktop player and the headless sim the path comes from `RENSGA_DEVCTL`; the launcher sets it for any process it starts.

One command per line: `<seq> <command> [argument]`, `seq` strictly increasing from 1. Lines whose `seq` was already applied are ignored. The launcher keeps the last 12 lines.

| Command | Effect | Log line written by the game |
| --- | --- | --- |
| `pause` | Stop advancing game frames. The loop keeps drawing and presenting, so the heartbeat continues. | `rensga: devctl paused frame=<f>` |
| `resume` | Run normally. | `rensga: devctl resumed frame=<f>` |
| `step <N>` | Pause if needed, run exactly N fixed frames, pause again. | `rensga: devctl step <N> frame=<before>`, then `rensga: devctl stepped <N> frame=<after>` |
| `apppause 0\|1` | `injectApplicationPause` (the Home popup / suspend path, which PPSSPP cannot raise itself). | `rensga: devctl apppause <v>` |
| `quit` | `Application::Quit()`. | `rensga: devctl quit` |
| `crash`, `hang` | Test only: write to address 0, or spin forever. They prove crash and hang detection. | `rensga: devctl crash (test)` / `hang (test)` |

`frame` is `Time::frameCount()`. After a step, `after - before` is N.

If the game does not answer a `pause` within 3 s (an ELF built without the channel), the launcher freezes the emulator process with SIGSTOP and says so in the Console. Resume sends SIGCONT. Step needs the channel.

## Session end

`exitState()` is one of:

| State | When |
| --- | --- |
| `Running` | The process lives. |
| `Exited` | It ended with code 0, or because Stop or quit-after was requested. |
| `Crashed` | Killed by a signal, a non-zero exit code that was not asked for, or (still running) the emulator logged a guest CPU exception. |
| `Hung` | Alive, but the heartbeat stopped for `hangTimeoutSeconds`. It clears if the heartbeat returns (a long load). A game frozen on purpose with SIGSTOP is not hung. |

`exitDetail()` gives one sentence ("crashed: the emulator was killed by signal 11 (SIGSEGV)", "hung: no 'rensga: frame' heartbeat for 15 s", "exited normally"). The CLI exits 3 for a crash and 4 for a hang (after saving the log and stopping the emulator), and prints `{"type":"exit",...}` with `--json`. The editor shows the sentence at the top of the Console with a Relaunch with debugger button.

## Log

Game lines arrive twice: through the emulator's output and through the tail of `host0:/rensga.log`. The launcher pairs identical lines across the two sources and keeps one. Emulator error lines are routed to the Console's Emulator source (the launcher tags them `[emu]`); its boot chatter is dropped, and an identical error repeated every frame is shown once with a count. Launcher notes are `[launcher] ...`. The Console filters by level, by source (Editor, Game, Emulator) and by a text search. `Library/Logs/last-play.log` holds every kept line of the last session.

## Timings

The pipeline logs `pipeline: stage <name> <ms>` after each stage and `pipeline: ready <ms> ms after Play`. Dev Play skips the compile when scripts, engine sources and build options are unchanged (`compile skipped: ...`), skips the CMake configure when its arguments and the script file list are unchanged, and cooks incrementally (per-asset cache).
