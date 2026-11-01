# frame_spike

F6.06 (Editor 12) exercise: find a frame spike in the Profiler window during a PSP run, fix it, compare. PSP profile, one `RoutePlanner` script, two scenes that differ in one field.

| Scene | Planner |
| --- | --- |
| `Spiky` | `spread = false`: every 120 frames one whole job (1500000 loop iterations) runs inside a single `Update`. That frame takes many times the budget, the others are almost empty. |
| `Smooth` | `spread = true`: the same job is cut into 40 slices, one per frame. Same total work, no spike. |

The job is wrapped in `Profiler::BeginSample("PlanRoute")`. A development build streams one `rensga: pf ...` line per frame (frame time, Update / Render / Present / Audio, draws, triangles, VRAM, arena use, the samples); the editor reads them into the Profiler window (`Frames` tab): the timeline shows a bar with a red top every 120 frames, "Select slowest frame" selects it and the sample tree names `PlanRoute`.

The exercise:

1. Open the project, Play on console (PSP, development build). Open Window > Profiler > Frames.
2. Find the spike in the timeline, select it, read `PlanRoute` in the sample tree.
3. Fix it: set the planner to spread its work. In Build Settings, move `Smooth` above `Spiky` (or set `spread` on the planner in `Spiky`) and run again.
4. Compare the two timelines.

```sh
tools/ci/frame_spike.sh          # PPSSPPHeadless, both scenes, numbers in $RENSGA_CI_OUT/frame-spike/summary.txt
python3 samples/frame_spike/tools/make_sample.py
```

On PPSSPP the microseconds are emulator time (your PC running the emulated PSP at its own pace), so read the ratio between the runs and the shape of the timeline, not the absolute numbers.
