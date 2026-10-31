# Submersa — Task list

Every task to make **Submersa** (PSX survival horror, 40-60 minute demo) with Rensga Engine, from an empty project to the closed END_RESULTS row, in percent from 0 to 100%. This is game work: engine work lives in [TASKLIST.md](../../../TASKLIST.md).

Sources: [SUBMERSA_ROADMAP.md](SUBMERSA_ROADMAP.md) (phases, notes, Claude prompts), [DEMO_GAMES.md](DEMO_GAMES.md) (pitch, gameplay, assets), [END_RESULTS.md](END_RESULTS.md) (the result matrix and the 10 checks), [MODELLING.md](MODELLING.md) (PSX asset limits), [TRAILER.md](TRAILER.md) (shots 14a-14g).

## How to use this list

- Tick a box (`- [x]`) when the task's "Done when" is true. Don't tick partial work: add a note under the task instead.
- Task IDs (`SUB1.01`, `SUB5.G2`) are stable. Never renumber them. New tasks go at the end of their block with the next free number.
- `Submersa 3` means phase 3 of the Submersa roadmap. `F9.08` and similar are engine task IDs from the root TASKLIST.md.
- **Percentages.** Each task carries its share of the whole game, taken from its roadmap estimate (`estimate / 30.5 weeks`), rounded with the largest remainder method so the tasks sum to exactly 100.00%. The weeks are human part-time effort and are used only as relative weights, not as a schedule. Progress = sum of ticked percentages.
- **Gates** carry no weight but must be ticked before their milestone counts as closed.
- Asset sub-tasks are plain bullets with no weight. They track the [Asset register](#asset-register); tick them as the assets land, but only the parent task's "Done when" counts.
- Legend: ◐ half ticked: done except a check that waits on something outside the repo, not counted until fully ticked.
- If a roadmap phase is added, removed or re-estimated, update this file and SUBMERSA_ROADMAP.md in the same change and recompute the weights.
- Engine blocking: a task cannot start before the engine tasks in its "Starts after" are ticked. See [Engine dependencies](#engine-dependencies).

## Progress

| Milestone | Weeks | Weight | Cumulative | Status |
| --- | --- | --- | --- | --- |
| SUB1 · Pre-production and slice | 4.5 | 14.76% | 14.76% | 0% |
| SUB2 · Core gameplay systems | 8.5 | 27.88% | 42.64% | 0% |
| SUB3 · Content, UI and audio | 8.5 | 27.86% | 70.50% | 0% |
| SUB4 · Finale and performance | 4.5 | 14.75% | 85.25% | 0% |
| SUB5 · Ship | 4.5 | 14.75% | 100.00% | 0% |
| **Total** | **30.5** | **100.00%** | | **0%** |

---

## SUB1 · Pre-production and slice — 14.76% (cumulative 14.76%)

Goal: one room proves the budgets, and the art kit and Inês exist.

Roadmap phases: Submersa 0, Submersa 1

- [ ] **SUB1.01** Submersa 0 · Vertical slice room — one room, two cameras, budgets measured · `6.56%`
  - Starts after: F9.08, F9.12, F9.13, F9.14, F9.16, F9.28.
  - Done when: `rensga play --target psx` boots the slice room in PCSX-Redux and logs `submersa: slice ok`. `rensga cook` passes the budget check with no override. The measured numbers are in the project README as draft budgets. Log shows `rensga: alloc-check frames=600 allocs=0` on the PSX build. The Linux build of the same project gives the same state hash as the PSX build after 600 frames.
  - [ ] Asset: Slice room (main street corner, 2 cameras)
  - [ ] Asset: Placeholder Inês and mud walker (template shapes)
  - [ ] Asset: Project README with asset list (see Asset register)
- [ ] **SUB1.02** Submersa 1 · Art kit and Inês — tile textures, prop kit, segmented character · `8.20%`
  - Starts after: Submersa 0; F9.05, F9.08, F9.13, F9.21, F9.27, F6.01, F6.02.
  - Done when: VRAM map shows the tile pages, Inês's atlas, the font and the UI page placed with no overlap, and the free space is written in the README. `Inês.prefab` (11 pieces, 400 triangles or fewer, 128x128 CLUT4) renders in the PSX preview mode and in PCSX-Redux with her idle pose. `rensga cook` passes with the kit and Inês, and the triangle and VRAM numbers are updated in the README. Props in the first kit are listed in the Asset register with their triangle counts.
  - [ ] Asset: Tile textures: mud, plaster, tile, rust (4-bit, tiled)
  - [ ] Asset: Inês Costa, 11 pieces, 400 triangles, 128x128 CLUT4
  - [ ] Asset: Prop kit 1: doors, crates, pews, desks, lamp posts
  - [ ] Asset: Bitmap font (4-bit)

**Gates**

- [ ] **SUB1.G1** The slice room cooks and validates with no budget override, and its numbers (triangles, RAM, VRAM pages, free VRAM) are in the project README.
- [ ] **SUB1.G2** The VRAM map shows no overlapping pages for the kit, Inês, the font and the UI page.
- [ ] **SUB1.G3** `tools/check_api_rules.sh` and a grep of the Submersa scripts find no console `#ifdef` and no console name in game code (check 1 of END_RESULTS).
- [ ] **SUB1.G4** The Linux build of the slice gives the same state hash as the PSX build.

## SUB2 · Core gameplay systems — 27.88% (cumulative 42.64%)

Goal: Inês moves, fights, solves and gets chased in the slice, in fixed-point.

Roadmap phases: Submersa 2, Submersa 3, Submersa 4, Submersa 5

- [ ] **SUB2.01** Submersa 2 · Player and fixed cameras — camera-relative control, tank option, Inês's clips · `6.56%`
  - Starts after: Submersa 1; F6.03, F6.10, F9.19, F9.20, F9.22, F9.29.
  - Done when: Inês walks through two rooms of the slice, camera cuts at the doorway, and the log prints `submersa: cut room=<id> cam=<id>` for each cut. Holding the stick across a cut keeps Inês moving the same way; the test records the path and the hash. All 11 clips play in the Animator preview and in PCSX-Redux. A deliberate `float` in a Submersa script fails the PSX build with the hint to use Scalar.
  - [ ] Asset: Inês animations: idle, walk, run, turn, aim, shoot, swing, hurt, death, push, pick up
- [ ] **SUB2.02** Submersa 3 · Items, inventory and weapons — item data, ammo, revolver, flare gun, machete · `6.56%`
  - Starts after: Submersa 2; F6.08, F6.11, F9.22.
  - Done when: A scripted test picks up a key, ammo and a bandage, fires six revolver shots, swings the machete and uses a flare; the log prints `submersa: inventory ok`. Health state changes Inês's walk clip between fine, caution and danger. The inventory grid serialises to a block of at most 512 bytes, round trip checked. `alloc-check` shows allocs=0 during shooting.
  - [ ] Asset: Item assets: keys, deeds, ammo, flares, bandages (20-150 triangles each)
- [ ] **SUB2.03** Submersa 4 · Enemies and AI — mud walker, leech swarm, bell ringer · `8.20%`
  - Starts after: Submersa 3; F6.03, F9.08, F9.22.
  - Done when: Each enemy type can be placed from the Hierarchy, plays its clips and kills or hurts Inês in a scripted test; log `submersa: enemies ok`. A room with three walkers and one swarm stays inside the room triangle budget and holds 30 fps in PCSX-Redux (prof log). `alloc-check` allocs=0 with all enemy types active. The validator refuses a room that exceeds the enemy cap, with an error naming the scene.
  - [ ] Asset: Mud walker 250 triangles, 64-128 px CLUT4
  - [ ] Asset: Leech swarm 80 triangles, one mesh, instanced
  - [ ] Asset: Bell ringer 350 triangles, 64-128 px CLUT4
  - [ ] Asset: Enemy animations: idle, walk, attack, hurt, death (4-6 each)
- [ ] **SUB2.04** Submersa 5 · Locks, puzzles and notes — keys, gauge puzzles, deeds, documents, ferryman · `6.56%`
  - Starts after: Submersa 3; F6.04, F6.11.
  - Done when: A scripted run solves the gauge puzzle, uses a key on a door and reads a note; the log prints `submersa: puzzles ok flags=<hex>`. The dependency graph document shows every key reachable before its lock, checked by a validator script in the project. All 6-8 notes are text assets that fit the font page.
  - [ ] Asset: Notes and documents: 6-8 text assets
  - [ ] Asset: Ferryman NPC, 300 triangles, short dialogue
  - [ ] Asset: Props: water gauges, pressure dial, radio

**Gates**

- [ ] **SUB2.G1** A scripted mini-loop runs end to end on PSX: pick up a key, open a door, shoot a walker, escape a bell ringer, solve one gauge puzzle. Log `submersa: core loop ok`.
- [ ] **SUB2.G2** `float` in Submersa script code is a compile error with the Scalar hint (F9.G4 holds for this project).
- [ ] **SUB2.G3** `alloc-check` allocs=0 over a 1,800-frame scripted run with three enemies active.
- [ ] **SUB2.G4** The PC build of the mini-loop gives the same state hash as the PSX build.

## SUB3 · Content, UI and audio — 27.86% (cumulative 70.50%)

Goal: four areas stream from disc, menus and saves work, music and effects play.

Roadmap phases: Submersa 6, Submersa 7, Submersa 8, Submersa 9

- [ ] **SUB3.01** Submersa 6 · Main street and church — first two areas, streaming from disc · `8.19%`
  - Starts after: Submersa 5; F9.09, F9.24.
  - Done when: A bot walks from the title through the main street into the church and back; log `submersa: area church loaded ms=<n>` and no frame over 100 ms in the prof log. Every room passes the budget report with no override. RAM peak during the overlap of two packs stays under the figure in the README. Screenshots from PCSX-Redux of each room are saved in the project's `Docs/shots/`.
  - [ ] Asset: Main street, 8-12 rooms, 4-bit tiled textures
  - [ ] Asset: Church: nave and bell tower
  - [ ] Asset: Props: pews, lamp posts, boat, doors, crates
- [ ] **SUB3.02** Submersa 7 · School and pumping station — areas 3 and 4, flooded rooms · `8.19%`
  - Starts after: Submersa 6; F9.09.
  - Done when: A bot walks the full route main street, church, school, pumping station and back; log `submersa: route ok keys=<n>`. No new VRAM page is added beyond the plan in the README, or the plan is updated and the budget still passes. The key-dependency validator passes for all four areas. Screenshots of each room are in `Docs/shots/`.
  - [ ] Asset: School: 2 classrooms and a yard
  - [ ] Asset: Pumping station: 3 rooms plus tunnels
  - [ ] Asset: Props: desks, rust pipes, valves, lamp posts
- [ ] **SUB3.03** Submersa 8 · UI, menus and saves — title, inventory, map, save radios, memory card · `6.56%`
  - Starts after: Submersa 5, Submersa 6; F6.04, F6.12, F9.03, F9.24.
  - Done when: Log shows `submersa: saved block=1 crc=<hex>` then, after a restart on the same card, `submersa: loaded crc=<hex>` with the same crc and the same state hash. Corrupt, missing and full cards each show a message and never crash. Every screen is reachable with the pad only; screenshots of title, inventory, close-up, map and save dialog are in `Docs/shots/`. The memory card icon shows in the PCSX-Redux memory card view.
  - [ ] Asset: UI: title, inventory grid, item close-up, map, save radio dialog, health status, memory card icon
  - [ ] Asset: One bitmap font
- [ ] **SUB3.04** Submersa 9 · Audio — XA music, VAG effects, ambience, SPU budget · `4.92%`
  - Starts after: Submersa 2; F9.06, F9.23.
  - Done when: XA tracks play in PCSX-Redux in the title, a safe room and the boss room (log `submersa: xa track=<n> start`). VAG residency report shows the SPU RAM total under budget for every area. A loading transition with music running logs zero underruns. Voice stress scene runs with `alloc-check` allocs=0.
  - [ ] Asset: XA music: title, safe room, boss
  - [ ] Asset: Ambience loops: wind, dripping, bell
  - [ ] Asset: About 30 VAG effects

**Gates**

- [ ] **SUB3.G1** All four areas load by streaming from disc with no frame over 100 ms in the prof log, and XA music does not underrun while a pack loads.
- [ ] **SUB3.G2** Save, quit, reload and continue works on a fresh memory card file, with the same state hash before and after; the save is one block with an icon.
- [ ] **SUB3.G3** Every area passes the budget report with no override; SPU RAM is under budget in every area.
- [ ] **SUB3.G4** The route test from the title through all four areas completes with `submersa: route ok`.

## SUB4 · Finale and performance — 14.75% (cumulative 85.25%)

Goal: the whole demo can be finished, and runs at 30 fps inside every budget.

Roadmap phases: Submersa 10, Submersa 11

- [ ] **SUB4.01** Submersa 10 · Dam control room and the Caretaker — boss fight, ending, credits · `8.19%`
  - Starts after: Submersa 4, Submersa 7, Submersa 8, Submersa 9; F6.03, F9.08.
  - Done when: A bot wins the Caretaker fight and the credits run; log `submersa: game complete hash=<hex>`. The finale is reached from every save radio in the game (checked by loading each save in a script). Boss room passes the budget report and holds 30 fps in PCSX-Redux. Boss reveal footage (low angle, slow tilt up) is recorded for TRAILER.md shot 14f.
  - [ ] Asset: Caretaker 700 triangles, 8 clips
  - [ ] Asset: Dam control room
  - [ ] Asset: Boss XA track hooked up
- [ ] **SUB4.02** Submersa 11 · Performance and budget pass — 30 fps, RAM, VRAM, sorting, streaming · `6.56%`
  - Starts after: Submersa 10; F9.09, F9.17, F6.06, F6.13.
  - Done when: Prof log shows every room at 30 fps or better in PCSX-Redux; the table of room numbers is in `Docs/budgets.md`. `rensga cook` and the validator pass with no override anywhere in ProjectSettings. `alloc-check` allocs=0 for the whole bot run. All done-when checks from Submersa 0-10 are green again.
  - [ ] Asset: Budget table per room (`Docs/budgets.md`)

**Gates**

- [ ] **SUB4.G1** The bot beats the Caretaker and reaches the credits: `submersa: game complete hash=<hex>`.
- [ ] **SUB4.G2** The finale is reachable from every save radio, and there is no soft lock in the key-dependency validator.
- [ ] **SUB4.G3** Every room holds 30 fps in PCSX-Redux (prof log); `Docs/budgets.md` has the numbers.
- [ ] **SUB4.G4** No budget override remains in ProjectSettings.

## SUB5 · Ship — 14.75% (cumulative 100.00%)

Goal: the Submersa row of END_RESULTS.md is closed on PSX, Windows, Linux and Android.

Roadmap phases: Submersa 12, Submersa 13

- [ ] **SUB5.01** Submersa 12 · PC and Android builds, bot run and determinism — same game everywhere, state hash · `8.19%`
  - Starts after: Submersa 11; F4.01, F6.05, F9.G3, F10.01.
  - Done when: Bot run on the PSX build in PCSX-Redux ends with `submersa: game complete hash=<hex>`; Windows, Linux and Android builds log the same hash. The first-divergence tool reports no differing frame between the four logs. Windows .zip, Linux AppImage and Android APK exist in `Library/Build/` and start to the title screen. Android: APK built; on-device run recorded when a phone is available (log `built, not run` otherwise).
  - [ ] Asset: Bot input file (`Tests/bot/full.bot`)
  - [ ] Asset: Touch overlay layout
- [ ] **SUB5.02** Submersa 13 · Trailer footage and packaging — TRAILER shots 14a-14g, release, END_RESULTS row · `6.56%`
  - Starts after: Submersa 12; F9.10, F10.01, F10.02.
  - Done when: Clips 14a-14g are recorded at 60 fps with a source list; the three-way split plays the same room on PSX, PC and Android. The four packages build from a clean checkout with `rensga build`, and each contains license files and `THIRD_PARTY.md`. The 10 checks plus the Submersa extras are recorded for all four cells (see SUB5 gates). The Submersa row in END_RESULTS.md is ticked by the user after review.
  - [ ] Asset: Trailer clips 14a-14g with labels
  - [ ] Asset: THIRD_PARTY.md and in-game credits

**Gates**

- [ ] **SUB5.G1** END_RESULTS cell **Submersa × PSX · BIN/CUE in PCSX-Redux with OpenBIOS**: all 10 checks pass for this build, plus the extras.
  - [ ] Check 1: One project: the build comes from `Submersa/` only; no console-specific code in game scripts, no second copy of the project.
  - [ ] Check 2: Built by the engine: made with `rensga build --target psx` or the editor's Build button, using build support modules (no Docker).
  - [ ] Check 3: Fits: cook and validate pass the console's budgets (memory, VRAM, SPU RAM, polygons, voices) with no override.
  - [ ] Check 4: Boots and finishes: boots to the title screen, then the bot run plays the whole demo to the credits with no crash or soft lock (`submersa: game complete`).
  - [ ] Check 5: Same game everywhere: the bot run's final state hash equals the reference hash in `Tests/bot/final.hash` (console, Windows, Linux and Android all match).
  - [ ] Check 6: No per-frame allocation: log shows `rensga: alloc-check ... allocs=0`.
  - [ ] Check 7: Frame rate: holds 30 fps in PCSX-Redux, measured from the `rensga: prof` log.
  - [ ] Check 8: Saves: save, quit, reload and continue works on the memory card (one block with an icon).
  - [ ] Check 9: Input: plays fully with the PSX pad in the emulator.
  - [ ] Check 10: Package is legal: license files for the engine and every SDK are included; no BIOS, no Sony marks, no ripped assets; third-party assets credited in `THIRD_PARTY.md`.
  - [ ] Extras: Submersa extras: fixed-point everywhere including this build; areas stream from disc with no visible stall; the save uses one memory card block with an icon; XA music plays.
- [ ] **SUB5.G2** END_RESULTS cell **Submersa × Windows · .zip (exe)**: all 10 checks pass for this build, plus the extras.
  - [ ] Check 1: One project: the build comes from `Submersa/` only; no console-specific code in game scripts, no second copy of the project.
  - [ ] Check 2: Built by the engine: made with `rensga build --target windows` or the editor's Build button, using build support modules (no Docker).
  - [ ] Check 3: Fits: cook and validate pass the console's budgets (memory, VRAM, SPU RAM, polygons, voices) with no override.
  - [ ] Check 4: Boots and finishes: boots to the title screen, then the bot run plays the whole demo to the credits with no crash or soft lock (`submersa: game complete`).
  - [ ] Check 5: Same game everywhere: the bot run's final state hash equals the reference hash in `Tests/bot/final.hash` (console, Windows, Linux and Android all match).
  - [ ] Check 6: No per-frame allocation: log shows `rensga: alloc-check ... allocs=0`.
  - [ ] Check 7: Frame rate: holds 30 fps on a normal PC, from the prof log.
  - [ ] Check 8: Saves: save, quit, reload and continue works in the save folder.
  - [ ] Check 9: Input: plays fully with a gamepad.
  - [ ] Check 10: Package is legal: license files for the engine and every SDK are included; no BIOS, no Sony marks, no ripped assets; third-party assets credited in `THIRD_PARTY.md`.
  - [ ] Extras: Submersa extras: fixed-point everywhere including this build; areas stream from disc with no visible stall; the save uses one memory card block with an icon; XA music plays.
- [ ] **SUB5.G3** END_RESULTS cell **Submersa × Linux · AppImage**: all 10 checks pass for this build, plus the extras.
  - [ ] Check 1: One project: the build comes from `Submersa/` only; no console-specific code in game scripts, no second copy of the project.
  - [ ] Check 2: Built by the engine: made with `rensga build --target linux` or the editor's Build button, using build support modules (no Docker).
  - [ ] Check 3: Fits: cook and validate pass the console's budgets (memory, VRAM, SPU RAM, polygons, voices) with no override.
  - [ ] Check 4: Boots and finishes: boots to the title screen, then the bot run plays the whole demo to the credits with no crash or soft lock (`submersa: game complete`).
  - [ ] Check 5: Same game everywhere: the bot run's final state hash equals the reference hash in `Tests/bot/final.hash` (console, Windows, Linux and Android all match).
  - [ ] Check 6: No per-frame allocation: log shows `rensga: alloc-check ... allocs=0`.
  - [ ] Check 7: Frame rate: holds 30 fps, from the prof log.
  - [ ] Check 8: Saves: save, quit, reload and continue works in the save folder.
  - [ ] Check 9: Input: plays fully with a gamepad.
  - [ ] Check 10: Package is legal: license files for the engine and every SDK are included; no BIOS, no Sony marks, no ripped assets; third-party assets credited in `THIRD_PARTY.md`.
  - [ ] Extras: Submersa extras: fixed-point everywhere including this build; areas stream from disc with no visible stall; the save uses one memory card block with an icon; XA music plays.
- [ ] **SUB5.G4** END_RESULTS cell **Submersa × Android · APK**: all 10 checks pass for this build, plus the extras.
  - [ ] Check 1: One project: the build comes from `Submersa/` only; no console-specific code in game scripts, no second copy of the project.
  - [ ] Check 2: Built by the engine: made with `rensga build --target android` or the editor's Build button, using build support modules (no Docker).
  - [ ] Check 3: Fits: cook and validate pass the console's budgets (memory, VRAM, SPU RAM, polygons, voices) with no override.
  - [ ] Check 4: Boots and finishes: boots to the title screen, then the bot run plays the whole demo to the credits with no crash or soft lock (`submersa: game complete`).
  - [ ] Check 5: Same game everywhere: the bot run's final state hash equals the reference hash in `Tests/bot/final.hash` (console, Windows, Linux and Android all match).
  - [ ] Check 6: No per-frame allocation: log shows `rensga: alloc-check ... allocs=0`.
  - [ ] Check 7: Frame rate: holds 30 fps, from the prof log (on a device when one is available; a phone run is deferred and must be recorded before this gate closes).
  - [ ] Check 8: Saves: save, quit, reload and continue works in the app save folder.
  - [ ] Check 9: Input: plays fully with touch plus a gamepad.
  - [ ] Check 10: Package is legal: license files for the engine and every SDK are included; no BIOS, no Sony marks, no ripped assets; third-party assets credited in `THIRD_PARTY.md`.
  - [ ] Extras: Submersa extras: fixed-point everywhere including this build; areas stream from disc with no visible stall; the save uses one memory card block with an icon; XA music plays.
- [ ] **SUB5.G5** Trailer shots 14a-14g are captured, labelled with target and source, and the three-way split uses the same room.
- [ ] **SUB5.G6** The Submersa row of END_RESULTS.md is ticked in all four cells, with the evidence (log lines, hashes, screenshots) listed in the project's `Docs/evidence.md`.

Note: the real-hardware PS1 pass is tracked by engine gate F9.G5, not here. Record its result in `Docs/evidence.md` when it exists.

---

## Engine dependencies

Engine IDs are from the root [TASKLIST.md](../../../TASKLIST.md). A milestone can start its first task once the listed engine tasks it needs for that task are ticked; the roadmap phases list them one by one.

| Milestone | Needs |
| --- | --- |
| SUB1 · Pre-production and slice | F9.01-F9.05 (toolchain, fixed-point core, platform, GPU, cooker and VRAM packer), F9.08 (GTE 3D backend), F9.12-F9.14 (PSX template, VRAM map, preview mode), F9.16 (Build and Run), F9.21 (segmented meshes), F9.27-F9.28 (primitive character and level), F6.01-F6.02 (prefabs, 3D toolset) |
| SUB2 · Core gameplay systems | F9.19-F9.22 (fixed-point rules and math, rendering, physics), F9.29 (fixed-point controller), F9.G4 (float is a compile error), F6.03 and F6.10 (Animation window, Animator), F6.04 (input, UI), F6.08 (physics), F6.11 (scenes, assets, saves) |
| SUB3 · Content, UI and audio | F9.03 (memory card, pad, CD), F9.06 and F9.23 (SPU, XA, audio API), F9.09 (area streaming), F9.24 (scenes and saves on PSX), F6.04 and F6.12 (UI), F6.11 (save data), F9.10 (memory card icon) |
| SUB4 · Finale and performance | F9.08 and F9.09 (GTE performance, streaming), F9.17 (PSX profiling), F6.06 (Profiler), F6.13 (budget helpers), F6.03 (Animator for the boss) |
| SUB5 · Ship | F9.G3 (PC and Android match the PSX hash), F4.01 and F2.G2 (shipped on every target, determinism job), F6.05 (Build pipeline), F9.10 and F10.01 (packaging), F10.02 (docs), F9.18 (PSX sample and credits). The Android phone run uses the deferred F2 device check. |

All of this sits behind the whole-game dependency in END_RESULTS.md: F9 (PSX track), F6 (full editor and API), F10 (packaging).

## Asset register

All art, audio and text for Submersa is made or commissioned by the user (owner `user`). Budgets come from DEMO_GAMES.md and MODELLING.md. Third-party assets must be CC0 or CC-BY and credited in `THIRD_PARTY.md`. Change the status to `in progress`, `done` or `cut` as work goes.

| ID | Kind | Asset | Budget / notes | Owner | Status |
| --- | --- | --- | --- | --- | --- |
| A01 | Character | Inês Costa, segmented (11 pieces) | 400 triangles, 128x128 CLUT4 | user | not started |
| A02 | Enemy | Mud walker | 250 triangles, 64-128 px CLUT4 | user | not started |
| A03 | Enemy | Leech swarm (one mesh, instanced) | 80 triangles, 64-128 px CLUT4 | user | not started |
| A04 | Enemy | Bell ringer (tall, slow) | 350 triangles, 64-128 px CLUT4 | user | not started |
| A05 | Enemy | The Caretaker (boss) | 700 triangles, 64-128 px CLUT4 | user | not started |
| A06 | NPC | Old ferryman | 300 triangles | user | not started |
| A07 | Area | Main street, 8-12 rooms | Grid tiles, 4-bit tiled textures | user | not started |
| A08 | Area | Church (nave and bell tower), 8-12 rooms | Grid tiles, 4-bit tiled textures | user | not started |
| A09 | Area | School (2 classrooms and yard), 8-12 rooms | Grid tiles, 4-bit tiled textures | user | not started |
| A10 | Area | Pumping station (3 rooms plus tunnels) | Grid tiles, 4-bit tiled textures | user | not started |
| A11 | Area | Dam control room | Grid tiles, flooded floor | user | not started |
| A12 | Texture | Tiled textures: mud, plaster, tile, rust | 4-bit, shared CLUTs | user | not started |
| A13 | Props | Doors, crates, pews, desks, lamp posts | 20-150 triangles each, vertex coloured where possible | user | not started |
| A14 | Props | Water gauges, radio, boat | 20-150 triangles each | user | not started |
| A15 | Props | Items: keys, deeds, ammo, flares, bandages | 20-150 triangles each | user | not started |
| A16 | Animation | Inês: idle, walk, run, turn, aim, shoot, swing, hurt, death, push, pick up | 11 rigid transform clips | user | not started |
| A17 | Animation | Enemies: idle, walk, attack, hurt, death | 4-6 clips each | user | not started |
| A18 | Animation | Boss: Caretaker | 8 clips | user | not started |
| A19 | UI | Title, inventory grid, item close-up, map, save radio dialog, health status, memory card icon | 4-bit | user | not started |
| A20 | UI | Bitmap font | 4-bit, one font | user | not started |
| A21 | Audio | XA music: title, safe room, boss | 3 tracks | user | not started |
| A22 | Audio | Ambience loops: wind, dripping, bell | VAG, within SPU RAM | user | not started |
| A23 | Audio | About 30 VAG effects (steps on mud and tile, door, gun, flare, enemy groans) | Within SPU RAM (512 KB minus reverb) | user | not started |
| A24 | Text | Notes and documents | 6-8 plain text assets | user | not started |
| A25 | Text | Short dialogue | Plain text assets | user | not started |

## Open questions

- [ ] **SQ1** Final demo length: aim for 60 minutes, cut to 40 if the Asset register falls behind. Decide by the end of SUB3.
- [ ] **SQ2** Which sounds and music are original and which are CC0 or CC-BY? List them in `THIRD_PARTY.md` before SUB5.
- [ ] **SQ3** Does the Android phone run happen before the SUB5 gate closes? It depends on the deferred F2 device check.
