# Remote profiler (F6.06, Editor 12)

Development builds stream one text line per frame. The editor's Profiler window (Frames tab) reads them from the log channel (consoles, emulators, wine) or from a localhost TCP socket (Linux player, an Android phone through `adb forward tcp:47017 tcp:47017`).

```
rensga: pf f=<frame> t=<total_us> u=<update_us> r=<render_us> p=<present_us> a=<audio_us> d=<draws> tr=<tris> tx=<textures> v=<vram_bytes> m=<arena_used> mc=<arena_capacity> s=<name>:<us>:<calls>:<parent>[,...]
```

One physical line. `s=-` when no `Profiler::BeginSample` ran that frame (the table shows only the samples of that frame, at most 8; `parent` is an index into this list, -1 = top level). Names have space, comma, colon and `=` replaced by `_`.

## Game side (`engine/runtime/prof_stream.{h,cpp}`, called by `FrameProfiler::frameEnd`)

* Off unless asked. `-DRENSGA_PROF_STREAM_LOG=ON` (the pipeline passes it for development builds on consoles) logs the line on the `game` channel every frame. `RENSGA_PROF_STREAM=log|tcp[:port]|both` does the same at run time on PC (default port 47017).
* TCP: the player listens on 127.0.0.1 (Linux and Android builds), the editor connects. A fixed ring of 64 lines, non-blocking sockets, the oldest whole line is dropped when the client is slow. No heap per frame: lines are formatted into static buffers; `alloc-check allocs=0` holds with the stream on.
* Windows builds use the log channel only. PSP and PS2 use the log channel (the launcher already scrapes it).
* The stream only reads clocks and counters: determinism hashes are unchanged.

## Editor side

`editor/core/profiler_model.{h,cpp}` parses the lines (`parseProfFrameLine`) and keeps the last 1200 frames (`ProfilerFeed`). `PlayController::pump` feeds game log lines to it (they are kept out of the Console) and attaches to the TCP port when a Linux development build runs; Play in editor pushes its own frames (sim time, draws, triangles, memory, `Profiler` samples).

Window > Profiler > Frames: per-frame timeline (Update, Render, Present, Audio, other; a red top marks frames over 1.5 x budget; click a bar to inspect that frame and pause), Pause, Select slowest frame, per-system table, sample hierarchy, memory view (arena, texture VRAM, triangles against the ConsoleTarget). The Summary tab is the F5 window (averages per 60 frames).

`--ui-event` verbs: `profiler` | `profilerfloat[:off]` | `profilerframes` (prints `frames= received= source= median_ms= worst_frame= worst_ms=`) | `profilerselect:<frame|worst|latest>` | `profilerpause[:0]` | `profilerattach[:port]` | `profilerframedump` | `profilerclear`.

PPSSPP time is emulator time (your PC running the emulated PSP, a vsync-locked frame sits at 16.4 ms): read the shape of the timeline and ratios between runs, not absolute milliseconds.

## Exercise

`samples/frame_spike` (README there): a planner runs a 1500000-iteration job inside one `Update` every 120 frames. `tests/editor/profiler_frames_ui.sh` finds the spike in the Profiler window, moves the fixed scene up in Build Settings and compares; `tools/ci/frame_spike.sh` prints the numbers from the CLI.
