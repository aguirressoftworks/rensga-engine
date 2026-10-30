# sprites1000

The 2D stress test (Engine 2 exercise). It is built into the desktop player, so it needs no project files:

```sh
xvfb-run -a -s "-screen 0 1280x720x24" build/<dir>/engine/runtime/rensga_desktop \
  --stress-sprites 1000 --frames 300 --screenshot sprites.png
```

1000 bouncing sprites (a CLUT4 ball and a CLUT8 box, tinted), a background bar and a digit HUD drawn with
`drawText`. The heartbeat lines report average and worst frame time (`rensga: frame 60 avg=..ms max=..ms`)
and the run ends with `rensga: alloc-check frames=<N> allocs=0`.
