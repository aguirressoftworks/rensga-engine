# Unity-user playtest notes (F6.07, Editor 13)

**How this was done, honestly.** The roadmap exercise asks a Unity developer to build a small game from the sample project and note every friction point. The user decided that an agent simulates it and that **F6.07 stays half done until a human repeats it**. The agent (D3) wrote the guide, so it was not a stranger: it had read the engine source and the other agents' reports. Treat the list as a floor. A real Unity developer will hit more, mostly in the first ten minutes. The agent did follow the rule of the exercise on purpose: the game, `samples/unity_playtest`, was written only with the guide and `samples/extending_editor` open, and every problem was written down at the moment it appeared, even when the agent knew the answer.

**The game.** CoinRun: collect 8 coins in a 3D arena before the time runs out. A `GameTuning` data asset holds the tuning, `Coin.prefab` is spawned from a `Guid` field, a `UIText` shows the score, `PlayerPrefs` keeps the best score, a bot plays the `MainBot` scene.

**Results.**

| Run | Result |
| --- | --- |
| Cook for Linux (`rensga-cooker`, project-built) | ok, 1 `DATA` asset, the prefab becomes a template (24 objects) |
| `rensga_headless --scene MainBot` | `coingame done score=80 collected=8/8 best=80 reason=all`, `allocs=0` |
| PPSSPPHeadless (software GE), PSP ELF with AUTOSTART `MainBot` | the same done line, `allocs=0`, screenshots `d3-playtest-psp-start.png` / `-mid.png` |
| Linux and PSP | the done lines are identical (the bot moves in `FixedUpdate`) |
| GL player, softpipe | score text drawn, coins visible (`d3-playtest-desktop.png`) |
| Play in editor (`--ui-event openscene:MainBot showgame playedit`) | works: the data asset is read, 8 coins are cloned from the prefab template (the Hierarchy lists them as runtime objects), the bot collects them (`d3-playtest-editor-play.png`) |

The first build of the game had one compile error (friction 2) and no runtime surprise.

## Frictions

Severity: **High** blocks a task or misleads for a long time, **Medium** costs minutes and needs a source dive, **Low** is a papercut. "Fixed" means fixed in this task (guide or own files); "Open" lists the owner.

| # | Where | Friction | Sev. | Status |
| --- | --- | --- | --- | --- |
| 1 | Prefabs, guide first draft | The first draft said "find the template by name". The real route is `gameObject()->world->findByGuid(guidField)` and appears only in `prefabs.md`. A Unity developer expects `Instantiate(prefabField)`. There is no `Instantiate(Guid)` helper and no `GameObject* prefab` field type. | High | **Fixed** (Stage 4): `Instantiate(const Guid&, pos, rot[, parent])` and `Instantiate(const Guid&)` in `engine/api/Object.h` find the template, clone and activate it; `samples/unity_playtest` uses it; test `objects_instantiate_prefab_guid_and_transform_call` |
| 2 | Compile error `no match for call to '(rensga::Transform) ()'` | In a Behaviour `transform()` is a method, but on another object it is a member: `other->transform.position()`. The compiler message does not hint at it. | Medium | **Fixed** (Stage 4): `Transform::operator()` returns the Transform pointer, so `other->transform()->position()` works like `transform()->position()` in a Behaviour and `other->transform.position()` is unchanged |
| 3 | `rensga cook` on a project | The CLI cooker only knows the scripts it was built with. The agent had to configure a host tree with `-DRENSGA_PROJECT_DIR=samples/unity_playtest` to cook outside the editor. In the editor it just works. | High for CLI users | Known hook row (F6.05, D2): the cooker loads the project's script module |
| 4 | Logging a `Scalar` | No `%f`. Scripts print `static_cast<int>(x * 100)` and name the field `speed_cm`. Fine for a console developer, odd for a Unity one. | Low | **Fixed** (Stage 4): `Debug::LogFormat("{0}", scalar)` (also `LogWarningFormat`, `LogErrorFormat`) through `Format`, no heap; the guide shows it |
| 5 | UI | The guide first led with `Canvas`. `UIText` works in a 3D scene with no Canvas at all, anchored to the view; the text is `label` (editor) or `SetFormat`/`SetText` (script). Which one wins is explained only in the header comment. | Low | Guide says "UIText in a scene needs no Canvas"; **Open**: D1 to document the Canvas-versus-bare split in `ui.md` |
| 6 | Tags | The generator wrote `Coin` into `TagManager.json` (`tags`) by hand. A Unity developer would use Tags and Layers in Project Settings (D1); not exercised. | Low | Open (D1, Tags and Layers window) |
| 7 | `FindGameObjectsWithTag` | Takes an output array and a capacity (no heap), not a list. Correct but unexpected; the guide mapping table did not list it. | Low | Guide mentions it in section 4 |
| 8 | Pools | `Instantiate` fails (returns null, logs the pool name) when a component pool is full. The default spare is 8 slots per type, so spawning 16 coins needs `"componentReserve": {"MeshRenderer": 16, ...}` in `Project.json`. A Unity developer has no idea that pools exist. | Medium | Guide section 4 mentions it; **Open**: the Inspector or the validator could suggest a reserve when a script calls `Instantiate` on a type |
| 9 | Input | `Input::GetAxis2("Move")` has y up; world forward is -z, so a top-down camera needs `Vector3(x, 0, -y)`. The template's controller solves it with 20 lines of camera maths; the guide shows no short version. | Low | Guide input paragraph unchanged; **Open**: a `Camera::main()->transform` based helper |
| 10 | Data assets and the Resources limit | `DataAssets::Get<T>` needs `T` to be `REFLECT`ed and at most 512 bytes. A struct with a `String32` x 16 fails with a log line at run time, not at compile time. | Low | Cooker error names the asset at cook time; fine |
| 11 | Editor module | Windows build of the `Editor/` module is untested (import library). | Medium for Windows users | Open (same as game scripts, F6.05/D2) |
| 12 | `Application::Quit()` in a bot scene | Works on the headless player, the desktop player and PPSSPP. What it does in Play in editor was not checked. There is no `EditorApplication.isPlaying` equivalent for tests. | Low | Open |
| 13 | Desktop player speed | `rensga_desktop` runs fixed steps as fast as it can for a short `--frames` run, so a screenshot "at frame 400" shows the finished game. Use a small frame count (14) for the mid-game shot. | Low | Documented in the sample README's CI notes |
| 14 | Names | `activeInHierarchy()` and `activeSelf()` are methods; Unity has properties. The guide says "getter named like the property" once, in the table; a Unity developer reads the table late. | Low | Guide section 2 keeps the rule next to the first code sample |
| 15 | Game view | The *Stats* overlay (on by default) is drawn over the top-left corner of the Game view, exactly where a score `UIText` sits, so the score is hidden until Stats is switched off. | Low | Open (D1 / Game view owner): move the overlay or make it translucent |

## What worked without help

- The side-by-side script (guide section 3) was copied almost as is: `REFLECT`, `SERIALIZE_FIELD`, `RANGE`, `OnTriggerEnter(GameObject*)`.
- `SERIALIZE_ENUM`, arrays and `String32` in the data asset needed no explanation: the Inspector widgets matched Unity's.
- The template's `Pickup` and `PickupCounter` were the best documentation of the trigger pattern; the guide should link them. Added to section 10.
- The cook errors named assets and fields (the data asset check was exercised with a wrong field name in `tests/cooker/test_data_cook.cpp`).

## What a human should try

1. Create the project from the Hub, add a script with Create > C++ Script, compile errors, fix, hot reload while playing.
2. Build the same game again from the guide **without** this sample, and count the compile errors.
3. Create a data asset type from scratch (struct, rename a field, see the warning in the Inspector).
4. Write an editor extension from `docs/rensga-engine/editor-extensions.md` only.
5. Build and run on PSP from Build Settings.
