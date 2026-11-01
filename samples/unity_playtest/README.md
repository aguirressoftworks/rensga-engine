# unity_playtest (F6.07, Editor 13)

A small game, **CoinRun**, built by following only [guide-for-unity-users.md](../../docs/rensga-engine/guide-for-unity-users.md) and `samples/extending_editor`, the way a Unity developer would. The frictions found while building it are in [playtest-notes.md](../../docs/rensga-engine/playtest-notes.md). The playtest was **simulated by an agent**; the task F6.07 stays half done until a human Unity developer repeats it.

Collect the coins before the time runs out (PSP profile, 3D):

| Unity idea | Here |
| --- | --- |
| ScriptableObject with the tuning | `Assets/Scripts/GameTuning.h` + `Assets/Data/Tuning.asset.json` (speed, coin count, coin value, time limit, arena size) |
| Prefab | `Assets/Prefabs/Coin.prefab` (spinning gold slab, trigger collider, tag `Coin`) |
| Score UI | the `ScoreText` object (`UIText`), formatted without allocation: `SetFormat("Score {0} ...")` |
| Player | `PlayerMove` + `CharacterController`; the nub moves (Move action); `bot = true` plays by itself |
| Manager | `CoinGame` spawns the coins from the prefab, counts down, stores the best score with `PlayerPrefs` |

Scenes: `Main` (play with the nub) and `MainBot` (the bot collects all coins and quits; the AUTOSTART scene of the PSP run).

```bash
build/host/editor/app/rensga-editor --project samples/unity_playtest      # Play in editor
tools/ci/unity_playtest.sh [linux] [psp] [desktop]                         # cook, run MainBot on the headless player, PPSSPPHeadless and the GL player
python3 samples/unity_playtest/tools/make_sample.py                        # regenerate scenes, prefab, asset and settings (scripts are kept)
```

Log lines: `rensga: coingame start coins=8 limit_s=30`, `rensga: coin collected <n>/<m> score=<s>`, `rensga: coingame done score=<s> collected=<n>/<m> best=<b> reason=all|time`.
