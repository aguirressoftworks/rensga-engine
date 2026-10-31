# Poeira — Task list

Every task to take Poeira, the arcade racing demo for the PSP, from an empty project to a shipped build: 14 phases in 5 milestones, 0 → 100%. The game is made with Rensga Engine, so these tasks are game work. The engine work they wait for is in [TASKLIST.md](../../../TASKLIST.md) and is listed in the Engine dependencies table.

Sources: [POEIRA_ROADMAP.md](POEIRA_ROADMAP.md) (phases, estimates, Claude prompts), [DEMO_GAMES.md](DEMO_GAMES.md) (pitch, gameplay, assets), [END_RESULTS.md](END_RESULTS.md) (the Poeira row and the 10 checks), [MODELLING.md](MODELLING.md) (PSP model limits).

## How to use this list

- Tick a box (`- [x]`) when the task's "Done when" is true. Don't tick partial work: add a note under the task instead.
- Task IDs (`POE3.02`) and gate IDs (`POE3.G1`) are stable. Never renumber them. New tasks go at the end of their block with the next free number.
- `Poeira 5` means phase 5 of [POEIRA_ROADMAP.md](POEIRA_ROADMAP.md). The roadmap has the scope, topics and Claude prompts for that phase. `F6.08` and similar IDs are engine tasks in TASKLIST.md; a Poeira task can start only when the engine tasks named in its roadmap phase are ticked.
- **Percentages.** Each task carries its share of the whole game, taken from its roadmap estimate (`estimate / 33.5 weeks`), rounded with the largest-remainder method so all 14 tasks sum to exactly 100.00%. The weeks are human part-time effort (about 15 hours a week) and are used only as relative weights, not as a schedule. Progress = sum of ticked percentages.
- **Gates** carry no weight but must be ticked before their milestone counts as closed.
- Asset sub-tasks are plain bullets with no weight. Their status lives in the Asset register at the end.
- Legend: ⏭ optional, not counted.
- If a phase is added, removed or re-estimated, update the roadmap and this file in the same change and recompute the weights.

## Progress

| Milestone | Weight | Cumulative | Status |
| --- | --- | --- | --- |
| POE1 · Slice and data | 13.43% | 13.43% | 0% |
| POE2 · Driving core | 22.39% | 35.82% | 0% |
| POE3 · Cars, tracks and effects | 26.86% | 62.68% | 0% |
| POE4 · Modes, audio and story | 20.90% | 83.58% | 0% |
| POE5 · Polish and ship | 16.42% | 100.00% | 0% |
| **Total** | **100.00%** | | **0%** |

---

## Milestones

Milestones follow the build order: slice and data → driving core → content → modes, audio and story → ship. Content phases can overlap with systems phases once their inputs exist. The percentages measure how much is done, not when.

### POE1 · Slice and data — 13.43% (cumulative 13.43%)

A budget-proven slice and the data model. Roadmap phases: Poeira 0, Poeira 1. Engine needs: F4 (PSP base), F5.01, F5.04-F5.09, F6.01, F6.02, F6.07.

- [ ] **POE1.01** Poeira 0 · Vertical slice — one track section, one car, budgets proven in PPSSPP · `7.46%`
  - Starts after: F5.01, F5.06, F5.07, F5.09, F6.02.
  - Done when: `rensga play --target psp` boots the slice in PPSSPP; the `rensga: prof` log shows 30 fps over 600 frames; the editor budget panel reports at most 20,000 triangles and 2 MB VRAM with no override; the log shows `rensga: alloc-check ... allocs=0`; a screenshot is saved; the project README holds the measured budget table.
  - Asset: Placeholder hatchback (1,500-2,000 triangles, 256x256 CLUT8)
  - Asset: Estrada do Buriti loop section (gridded road, 2 tiling textures)
- [ ] **POE1.02** Poeira 1 · Project, data assets and tools — CarStats, TrackData, CupData, RivalData and the spline editor tool · `5.97%`
  - Starts after: Poeira 0, F6.07.
  - Done when: CarStats, TrackData, CupData and RivalData exist as reflected data assets with inspector editing, undo and a cook round-trip test; the spline tool places a racing line, brake points, checkpoints and respawn points on the slice track and saves them as data; the README asset list has one row per asset in the asset register below; `tools/check_api_rules.sh` and `tools/check_editor_core.sh` pass.
  - Asset: Racing-line, brake-point, checkpoint and respawn spline for the slice track
  - Asset: THIRD_PARTY.md and README asset list

**Gates**

- [ ] **POE1.G1** The measured budget table is in the README and matches the editor's PSP budget panel.
- [ ] **POE1.G2** The slice boots with `rensga play --target psp` and `rensga: alloc-check ... allocs=0`.
- [ ] **POE1.G3** No script branches on a console (`tools/check_api_rules.sh`, `tools/check_editor_core.sh` pass).

### POE2 · Driving core — 22.39% (cumulative 35.82%)

Physics, camera, race rules and AI. Roadmap phases: Poeira 2, Poeira 3, Poeira 4. Engine needs: F6.03, F6.04, F6.08, F6.09, F6.13, F5.12, F5.14.

- [ ] **POE2.01** Poeira 2 · Arcade car physics — raycast CarController: suspension, grip, drift, boost, jumps, walls · `8.96%`
  - Starts after: Poeira 1, F6.08.
  - Done when: CarController drives the slice car with four raycasts; drift fills the boost bar and boost works; a ramp jump lands without a bounce loop; a headless run of the same input trace twice prints the same state hash; at most 28 raycasts per fixed step for seven cars is shown in the profiler; 30 fps holds in PPSSPP; `allocs=0`.
  - Asset: Wheel and steering code on the placeholder car
  - Asset: Collision mesh for the slice track (simpler than the render mesh)
- [ ] **POE2.02** Poeira 3 · Camera, race rules and replay — chase camera, race state machine, laps, checkpoints, respawn, jump replay · `5.97%`
  - Starts after: Poeira 2, F6.09.
  - Done when: A three-lap race on the slice track runs from countdown to results; cutting a checkpoint does not count the lap; wrong-way and respawn work after a fall and after 3 seconds stuck; the jump replay camera plays in slow motion and returns to the chase view; the race log prints positions each lap; `allocs=0`; 30 fps in PPSSPP.
  - Asset: Countdown, start grid and finish arch prefabs
  - Asset: Flag waver (animation clip)
- [ ] **POE2.03** Poeira 4 · AI rivals — spline AI on the shared CarController, rubber banding, per-rival skill · `7.46%`
  - Starts after: Poeira 3.
  - Done when: Six AI rivals complete 10 headless races each on the slice track with no stuck car for longer than 3 seconds; finish order varies with skill; the same seed gives the same final state hash; AI cost is under 0.3 ms per car in the profiler; 30 fps holds in PPSSPP with seven cars; `allocs=0`.
  - Asset: RivalData for 6 rivals (skill, car, line offset)

**Gates**

- [ ] **POE2.G1** Seven cars finish 3 laps on the slice track in a headless run with no stuck car and a repeatable final state hash.
- [ ] **POE2.G2** The same input trace on the PSP build in PPSSPP and on a desktop build gives the same state hash.
- [ ] **POE2.G3** The profiler shows physics plus AI under their CPU share of a 33 ms frame.

### POE3 · Cars, tracks and effects — 26.86% (cumulative 62.68%)

All content for 6 cars, 4 tracks, reverse and night. Roadmap phases: Poeira 5, Poeira 6, Poeira 7, Poeira 8. Engine needs: F5.01, F5.02, F5.06, F5.14, F6.01, F6.02, F6.08, F6.09.

- [ ] **POE3.01** Poeira 5 · Cars — six cars with wheels, three paints and low-detail versions · `5.97%`
  - Starts after: Poeira 4.
  - Done when: All 6 cars import with no cooker error; each is within its triangle budget and has one 256x256 CLUT8 atlas; 3 paints per car switch in the garage scene; the low-detail mesh is used past the distance threshold; each car completes a lap on the slice track; the asset register rows for cars are ticked.
  - Asset: Car 1: rebuilt hatchback (player car)
  - Asset: Car 2: pickup
  - Asset: Car 3: light buggy
  - Asset: Car 4: rally sedan
  - Asset: Car 5: vintage coupe
  - Asset: Car 6: rancher's truck
  - Asset: 3 paints x 6 cars, 6 low-detail meshes
- [ ] **POE3.02** Poeira 6 · Tracks 1 and 2 — Estrada do Buriti and Serra Dourada, complete in day variant · `7.46%`
  - Starts after: Poeira 5.
  - Done when: Both tracks cook with no override and are raceable for 3 laps against 6 AI rivals; the worst frame is at most 20,000 triangles and VRAM at most 2 MB; 30 fps in PPSSPP at the busiest point; each track has racing line, brake points, checkpoints and respawn points; `allocs=0`.
  - Asset: Track 1: Estrada do Buriti (road, collision, sky dome, splines)
  - Asset: Track 2: Serra Dourada (road, collision, sky dome, splines)
  - Asset: Scenery: buriti palms, cerrado trees, fences, farm gates, rocks, bridges, cattle
  - Asset: Tiling textures: dirt, grass, rock, water
- [ ] **POE3.03** Poeira 7 · Tracks 3 and 4, reverse versions — Canavial and Chapada Velha, and reverse for all four tracks · `7.46%`
  - Starts after: Poeira 6.
  - Done when: Tracks 3 and 4 are raceable for 3 laps against 6 AI rivals; all 4 tracks also race in reverse with working checkpoints, respawns and AI; jumps land all 6 cars in the landing zone; every track and direction cooks with no override; worst frame at most 20,000 triangles; 30 fps in PPSSPP; `allocs=0`.
  - Asset: Track 3: Canavial (road, collision, sky dome, splines)
  - Asset: Track 4: Chapada Velha (road, collision, sky dome, splines)
  - Asset: Scenery: cane cards, crowd sprites, banners, checkpoint arches
  - Asset: Tiling textures: cane, rock plateau
- [ ] **POE3.04** Poeira 8 · Effects, lighting and weather — particles, night and sunset lighting, headlights, rain · `5.97%`
  - Starts after: Poeira 7, F6.09.
  - Done when: All 6 particle effects play from pools with a total cap of 300 live particles; night variants exist for all 4 tracks with headlights; the Chapada Velha sunset set works; rain runs on the one night race; the worst frame with all effects on is at most 20,000 triangles and holds 30 fps in PPSSPP; `allocs=0`.
  - Asset: VFX sprites: dust, splash, boost flame, drift smoke, sparks, rain (small CLUT4)
  - Asset: Night vertex-colour sets for 4 tracks
  - Asset: Sunset set for Chapada Velha

**Gates**

- [ ] **POE3.G1** All 4 tracks, their reverse versions and night variants cook with no override.
- [ ] **POE3.G2** All 6 cars with 3 paints each show in the garage scene and race a lap on every track.
- [ ] **POE3.G3** The busiest frame of each track holds at most 20,000 triangles, 2 MB VRAM and 30 fps in PPSSPP with seven cars and effects.

### POE4 · Modes, audio and story — 20.90% (cumulative 83.58%)

Menus, saves, ghosts, sound, story and the whole game flow. Roadmap phases: Poeira 9, Poeira 10, Poeira 11. Engine needs: F5.12, F5.15, F5.17, F6.04, F6.10, F6.11, F6.12.

- [ ] **POE4.01** Poeira 9 · UI, modes and saves — HUD, garage, cup map, Cup, Time Trial and Quick Race, ghost, saves · `8.96%`
  - Starts after: Poeira 8, F6.04, F6.12, F6.11, F5.15.
  - Done when: From the title screen the user can reach the garage, Quick Race, Time Trial and Cup; Time Trial saves a ghost and a reload plays it back with the same times; save, quit and reload keeps cups, unlocks and best times in PPSSPP (memory stick) and on desktop (save folder); every screen works with pad only; the UI atlas fits the VRAM budget; `allocs=0`; 30 fps in PPSSPP.
  - Asset: UI: title, garage, cup map, HUD, results and podium (CLUT8)
  - Asset: Fonts: one bitmap font, one large number font
  - Asset: Save icons: ICON0 and PIC1
- [ ] **POE4.02** Poeira 10 · Audio — engine loops, effects, 16-voice budget, streamed music · `5.97%`
  - Starts after: Poeira 5, F6.10, F5.17.
  - Done when: All 6 cars have 3 RPM loops with clean crossfades; effects and 6 music pieces play; the log shows at most 16 voices in the worst race and no underruns over a 3-lap race; total audio RAM is listed in the budget table and fits; music streams from the memory stick; `allocs=0`.
  - Asset: Music: title, 4 track themes, results jingle
  - Asset: Engine loops: 6 cars x 3 RPM bands
  - Asset: Effects: tyre skid, gravel, impacts, boost, countdown beeps, crowd
- [ ] **POE4.03** Poeira 11 · Rivals, story and final cup — story stills, rival data, cup flow and unlocks, final race, credits · `5.97%`
  - Starts after: Poeira 9.
  - Done when: The full game flow plays from the title through Bronze, Silver and Gold to the podium, credits and title; unlocks appear in the garage after each cup; story stills show between cups; the final race uses the champion's AI; the log prints `game complete` at the credits; save and reload mid-cup resumes at the right race; `allocs=0`.
  - Asset: Driver portraits: Lia and 6 rivals (256x256 CLUT8 stills, 64x64 HUD icons)
  - Asset: Story stills: garage, rivals, old champion, ending
  - Asset: Credits text from THIRD_PARTY.md

**Gates**

- [ ] **POE4.G1** Cup Bronze to Gold plays with unlocks; save, quit and reload resumes in PPSSPP and on desktop.
- [ ] **POE4.G2** A time-trial ghost saves, loads and replays with identical times.
- [ ] **POE4.G3** In the worst race at most 16 voices are used and the log shows no underrun.

### POE5 · Polish and ship — 16.42% (cumulative 100.00%)

Builds, budget pass, bot run, trailer and the END_RESULTS row. Roadmap phases: Poeira 12, Poeira 13. Engine needs: F5.02, F5.03, F5.16, F5.18, F6.05, F6.06, F6.13, F10.01, F10.G2.

- [ ] **POE5.01** Poeira 12 · Builds, input and performance pass — Windows, Linux and Android builds, pad and touch, PSP budget pass · `8.96%`
  - Starts after: Poeira 11, F5.02, F5.16, F6.05, F6.06.
  - Done when: `rensga build --target psp` makes EBOOT.PBP and the Windows, Linux and Android builds come from the same project; the game plays fully with the PSP pad in PPSSPP, a gamepad on PC and touch plus a gamepad on Android; the budget table lists the worst frame for each of the 8 track variants and each is within 20,000 triangles, 2 MB VRAM, 32 MB RAM and 16 voices; `rensga: prof` shows 30 fps in PPSSPP; no budget override; `allocs=0` in all four builds.
  - Asset: Android touch layout art (CLUT8)
  - Asset: Budget table for 8 track variants (README)
- [ ] **POE5.02** Poeira 13 · Bot run, trailer footage and packaging — whole-game bot, hash match on four builds, trailer shots, END_RESULTS row · `7.46%`
  - Starts after: Poeira 12, F10.01.
  - Done when: The bot run prints `game complete` on all four builds with the same final state hash; `rensga: alloc-check ... allocs=0` and 30 fps in the `rensga: prof` log on the PSP build; shots 15a-15h are recorded and listed with their source; packages for PSP, Windows, Linux and Android contain all license files and THIRD_PARTY.md; the Poeira row of END_RESULTS.md is ticked in all four cells.
  - Asset: Trailer footage list (15a-15h) with source labels
  - Asset: Release packages: EBOOT.PBP, .zip, AppImage, APK

**The 10 checks** (from [END_RESULTS.md](END_RESULTS.md); each cell below needs all of them):

1. **One project.** The build comes from the single Poeira project. No console-specific code in game scripts, and no second copy of the project.
2. **Built by the engine.** Made with `rensga build --target <id>` or the editor's Build button, using build support modules (no Docker).
3. **Fits.** The cook and validate stages pass the PSP budgets (memory, VRAM, triangles, voices) with no override.
4. **Boots and finishes.** Boots to the title screen; a scripted bot run plays the whole game to the credits with no crash or soft lock.
5. **Same game everywhere.** The bot run's final state hash is the same on the PSP build and the Windows, Linux and Android builds.
6. **No per-frame allocation.** The log shows `rensga: alloc-check ... allocs=0`.
7. **Frame rate.** 30 fps in the emulator, measured from the `rensga: prof` log.
8. **Saves.** Save, quit, reload and continue works: memory stick on PSP, the save folder on PC and Android.
9. **Input.** Plays fully with the PSP pad in the emulator, a gamepad on PC, and touch plus a gamepad on Android.
10. **Package is legal.** License files for the engine and every SDK are included. No BIOS, no Sony marks, no ripped assets. Third-party assets are credited in `THIRD_PARTY.md`.

**Gates**

- [ ] **POE5.G1** Result matrix cell, Poeira · PSP in PPSSPP: `EBOOT.PBP` runs in PPSSPP, and all 10 checks pass for this build. Tick the cell in END_RESULTS.md.
- [ ] **POE5.G2** Result matrix cell, Poeira · Windows: `.zip` with the exe runs on Windows, and all 10 checks pass for this build. Tick the cell in END_RESULTS.md.
- [ ] **POE5.G3** Result matrix cell, Poeira · Linux: AppImage runs on Linux, and all 10 checks pass for this build. Tick the cell in END_RESULTS.md.
- [ ] **POE5.G4** Result matrix cell, Poeira · Android: APK runs on Android (touch and gamepad), and all 10 checks pass for this build. Tick the cell in END_RESULTS.md.
- [ ] **POE5.G5** Poeira extras (END_RESULTS.md): 6 cars and 4 tracks (plus reverse) are raceable; time-trial ghosts save and load. If ad hoc multiplayer ships (OPT-1), it is checked with two PPSSPP instances.
- [ ] **POE5.G6** Trailer footage for shots 15a-15h exists, each clip listed with its source (emulator, build or device).
- [ ] **POE5.G7** The Poeira row of END_RESULTS.md has all four cells ticked and its status note updated.
- Real-hardware passes are not part of these gates; they are tracked in the engine list (F5.G1, F10.G4).

---

## Engine dependencies

What each milestone needs from [TASKLIST.md](../../../TASKLIST.md). Do not start a Poeira task before its engine tasks are ticked.

| Milestone | Engine tasks | What for |
| --- | --- | --- |
| POE1 · Slice and data | F4 (PSP base), F5.01, F5.04-F5.09, F6.01, F6.02, F6.07 | GU 3D backend, PSP target module, import settings (swizzled CLUT8), PSP preview, Build and Run, 3D toolset, prefabs, data assets |
| POE2 · Driving core | F6.03, F6.04, F6.08, F6.09, F6.13, F5.12, F5.14 | Physics raycasts, animation and camera API, UI for the HUD basics, PSP profile and rendering API, pooling and budgets |
| POE3 · Cars, tracks and effects | F5.01, F5.02, F5.06, F5.14, F6.01, F6.02, F6.08, F6.09 | Meshes, materials, lights, prefabs, particles, PSP performance, light limits and fog |
| POE4 · Modes, audio and story | F5.12, F5.15, F5.17, F6.04, F6.10, F6.11, F6.12 | Canvas UI and UI API, save data and the PSP save dialog, audio API and mixer, PSP audio defaults |
| POE5 · Polish and ship | F5.02, F5.03, F5.16, F5.18, F6.05, F6.06, F6.13, F10.01, F10.G2 | PSP performance and profiling, packaging and release, Build Settings for all targets, API tests, ship tasks, SDK license notices |

Optional, not counted: ⏭ **OPT-P1** Ad hoc 2-4 player races for Poeira (needs engine OPT-1, PSP API 6). If it is skipped, the trailer uses the ghost race for shot 15g.

## Asset register

Every asset from DEMO_GAMES.md for Poeira. Owner is the user; status is `todo`, `wip`, `done` or `cooked` (passes the cook with no budget error).

| Asset | Count | Budget / notes | Phase | Owner | Status |
| --- | --- | --- | --- | --- | --- |
| Car 1 hatchback (player) | 1 + 3 paints + low-detail | 1,500-2,000 tris, 256x256 CLUT8; low-detail 600 | Poeira 5 | user | todo |
| Cars 2-6 (pickup, buggy, rally sedan, coupe, rancher's truck) | 5 + 3 paints each + low-detail | 1,000-1,500 tris, 256x256 CLUT8; low-detail 600 | Poeira 5 | user | todo |
| Wheels (separate, per car) | 6 sets of 4 | included in car budget | Poeira 5 | user | todo |
| Driver portraits: Lia + 6 rivals | 7 stills + 7 icons | 256x256 CLUT8 stills, 64x64 HUD icons | Poeira 11 | user | todo |
| Track 1 Estrada do Buriti: road, collision, sky dome, night set | 1 | gridded road, CLUT8 tiling textures, 20,000 tris/frame total | Poeira 6 | user | todo |
| Track 2 Serra Dourada: road, collision, sky dome, night set | 1 | same budget | Poeira 6 | user | todo |
| Track 3 Canavial: road, collision, sky dome, night set | 1 | same budget | Poeira 7 | user | todo |
| Track 4 Chapada Velha: road, collision, sky dome, night and sunset sets | 1 | same budget | Poeira 7 | user | todo |
| Scenery props: buriti palms, cerrado trees, fences, farm gates, cattle, rocks, bridges | about 10 kinds | 20-500 tris; billboards for distant trees | Poeira 6 | user | todo |
| Scenery: crowd sprites, banners, checkpoint arches | 3 kinds | small CLUT4 sprites, low tris | Poeira 7 | user | todo |
| Tiling textures: dirt, grass, rock, cane, water | 5 | CLUT8, shared palettes, 2 MB VRAM total | Poeira 6 | user | todo |
| Spline data: racing line, brake points, checkpoints, respawn points | 4 tracks x 2 directions | data assets | Poeira 1 | user | todo |
| Animation: flag waver, crowd loops; wheel spin, steer, suspension (code) | 2 clips + code | procedural | Poeira 3 | user | todo |
| VFX sprites: dust, splash, boost flame, drift smoke, sparks, rain | 6 | small CLUT4; cap 300 live particles | Poeira 8 | user | todo |
| UI: title, garage, cup map, HUD, results and podium, story stills | 6 screen sets | CLUT8 atlases | Poeira 9 | user | todo |
| Fonts: bitmap font, large number font | 2 | CLUT8 | Poeira 9 | user | todo |
| Save icons: ICON0, PIC1 | 2 | PSP save dialog sizes | Poeira 9 | user | todo |
| Music: title, 4 track themes, results jingle | 6 tracks | streamed from memory stick | Poeira 10 | user | todo |
| Engine loops | 6 cars x 3 RPM bands | short mono loops, in the RAM budget | Poeira 10 | user | todo |
| Effects: tyre skid, gravel, impacts, boost, countdown beeps, crowd | about 12 | 16-voice mixer | Poeira 10 | user | todo |
| Story text and credits | 5-6 scenes | plain text assets | Poeira 11 | user | todo |
| Android touch layout art | 1 set | CLUT8 | Poeira 12 | user | todo |
