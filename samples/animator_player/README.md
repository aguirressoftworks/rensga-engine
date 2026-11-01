# animator_player

F6.03 (Editor 9) exercise: a player with an Idle / Run / Jump Animator controller driven by script parameters. PSP profile (480x272), one scene, one script (`PlayerBot`).

| Asset | What it is |
| --- | --- |
| `Assets/Animation/Idle.clip.json`, `Run.clip.json`, `Jump.clip.json` | Property clips: `SpriteRenderer.srcX` / `srcY` steps through the player sheet; the Jump clip also squashes and stretches `Transform.localScale` with smooth (Hermite) curves |
| `Assets/Animation/Player.controller.json` | Parameters `speed` (float) and `jump` (trigger); Idle <-> Run on `speed`, any state -> Jump on the trigger, Jump -> Idle at its exit time; short cross-fades |
| `Assets/Scripts/PlayerBot.cpp` | A bot: one script step per fixed step. Runs at step 60 (speed 1), jumps at steps 140 and 330, stops at step 260, ends the run at step 420 |

Open it in the editor (`rensga-editor --project samples/animator_player`), double-click a clip to open the Animation window or the controller to open the Animator graph, press Play in editor and watch the running state light up in the graph.

```sh
tools/ci/animation.sh player        # desktop player + PPSSPPHeadless: the state changes must be equal, allocs=0
tools/ci/determinism.sh anim        # Linux headless vs the PSP simulator: state hash, per-frame trace and every "rensga: anim f=<n> h=<hex>" line
python3 samples/animator_player/tools/make_sample.py   # regenerates the sample (the script is hand-written)
```

Log lines: `rensga: animator_player: state=Run step=65` for every state change (the state reported is the one the Animator is in, so it changes when a cross-fade ends) and `rensga: animator_player: done steps=420 x=<px> changes=<n>`.

The player sheet is the one `samples/platformer2d` generates.
