# pool_spawner

F6.13 (API 12) exercise: a projectile spawner with and without a pool, measured with `Profiler` samples. PSP profile, one `ProjectileSpawner` script, two scenes that differ in one field.

| Scene | Spawner |
| --- | --- |
| `Plain` | `pooled = false`: every shot is `Instantiate(ShotTemplate)` and, after `lifeFrames`, `Destroy(shot)`. |
| `Pooled` | `pooled = true`: `GameObjectPool<96>.Init(template)` makes 96 inactive clones in `Start`; `Get` / `Release` only flip `SetActive`. |

Both start 2 shots per frame that live 30 frames (60 live shots) for 310 frames, then log `rensga: pool: mode=<plain|pooled> frames= spawned= refused= live_max= objects=` and quit. The script wraps the two halves of its `Update` in `Profiler::BeginSample("Spawn")` and `"Move"`; the runtime prints them next to the usual frame line every 60 frames:

```
rensga: prof sample name=Spawn frames=60 calls=120 avg_us=<per frame> max_us=<worst frame>
```

```sh
tools/ci/pool_spawner.sh headless   # PPSSPPHeadless: Plain and Pooled builds, summary table in $RENSGA_CI_OUT/pool-spawner/summary.txt
tools/ci/pool_spawner.sh desktop    # the same on the Linux player
python3 samples/pool_spawner/tools/make_sample.py
```

On PPSSPPHeadless the microseconds are emulator time (the PC's CPU running the emulated PSP), so only the ratio between the two rows means something. `alloc-check allocs=0` must hold in both runs; the pooled run must also show `init made=96 of 96` and `refused=0`.
