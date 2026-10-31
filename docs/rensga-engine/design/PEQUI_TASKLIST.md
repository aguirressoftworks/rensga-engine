# Pequi — Task list

Every task from an empty Rensga project to a finished Pequi, a PS2 3D mascot action platformer that also ships for Windows, Linux and Android, in one list from 0 to 100%.

Sources: [PEQUI_ROADMAP.md](PEQUI_ROADMAP.md) (phases, notes, Claude prompts), [DEMO_GAMES.md](DEMO_GAMES.md) (pitch, gameplay, assets), [END_RESULTS.md](END_RESULTS.md) (the Pequi row and the 10 checks), [MODELLING.md](MODELLING.md) (PS2 asset limits). Engine work lives in the main [TASKLIST.md](../../../TASKLIST.md); this list only uses it.

## How to use this list

- Tick a box (`- [x]`) when the task's "Done when" is true. Don't tick partial work: add a note under the task instead.
- Task IDs (`PEQ2.01`, `PEQ5.G1`) are stable. Never renumber them. New tasks go at the end of their block with the next free number.
- `Phase 3` and similar references mean that phase of PEQUI_ROADMAP.md. `F8.02` and similar mean a task of the engine TASKLIST.md.
- **Percentages.** Each task carries its share of the whole game: `phase estimate / 33 weeks`, rounded with the largest-remainder method so the 14 tasks sum to exactly 100.00%. The weeks are human part-time effort (about 15 hours a week), used only as relative weights, not as a schedule. Progress = sum of ticked percentages.
- **Gates** carry no weight but must be ticked before their milestone counts as closed. Asset bullets carry no weight but must be done before their task is ticked.
- A task may start only when the engine tasks it needs (table at the end) are ticked in the engine TASKLIST.md. Until then, work on the data side (design, art, audio) can go ahead.
- PS2 checks run locally in PCSX2 with your own BIOS (no public CI boot). Never add a BIOS to the repository.

## Progress

| Milestone | Weight | Cumulative | Status |
| --- | --- | --- | --- |
| PEQ1 · Pre-production and vertical slice | 16.67% | 16.67% | 0% |
| PEQ2 · Core gameplay | 22.73% | 39.40% | 0% |
| PEQ3 · Levels and mini bosses | 18.18% | 57.58% | 0% |
| PEQ4 · Presentation and finale | 22.73% | 80.31% | 0% |
| PEQ5 · Ship | 19.69% | 100.00% | 0% |
| **Total** | **100%** | | **0%** |

---

## PEQ1 · Pre-production and vertical slice — 16.67% (cumulative 16.67%)

Plan, tools, and one level that proves the PS2 budgets.

- [ ] **PEQ1.01** Phase 0 · Pre-production and style bible — Scope, pillars, level plans, budgets on paper · `3.03%`
  - Starts after: Engine milestone F4 done (2D slice, UI and font trial); F8.05 PS2 primitive test level.
  - Done when: A one-page design, the budget sheet and four level maps are committed under the project; `rensga cook` of the empty PS2 project prints the budget table with nothing over; the asset register below is copied into the project README.
  - Asset: Project README with the asset list
  - Asset: Concept sketches for Pequi, the Baron and the four level moods
- [ ] **PEQ1.02** Phase 1 · Tools and data assets — Blender kit, cook settings, data assets, import checks · `6.06%`
  - Starts after: Phase 0; F8.01, F6.02 (3D toolset), F6.07 (data assets).
  - Done when: A crate, a kit floor piece and the hero test mesh go Blender to cook to PCSX2 with the cooker budget lines in the log; MoveSet and LevelInfo data assets edit in the inspector and undo/redo; the importer rejects a 512x512 RGBA test texture with an error naming the asset.
  - Asset: Blender export preset and checklist
  - Asset: Crate, rain jar and water drop props
  - Asset: Floor, wall and slope kit pieces
  - Asset: Placeholder hero (grey box, 30 bones)
- [ ] **PEQ1.03** Phase 2 · Vertical slice: Vereda blockout — One level that proves the budgets on PCSX2 · `7.58%`
  - Starts after: Phase 1; F8.01, F8.02, F8.04, F8.05.
  - Done when: `rensga cook --target ps2` passes with no override; PCSX2 run of 1,800 frames logs `rensga: prof` at or above 30 fps, `rensga: alloc-check ... allocs=0` and zero missing-texture warnings; a screenshot of the slice is committed; the budget sheet is updated with measured numbers.
  - Asset: Vereda blockout level (kit pieces)
  - Asset: Moving platform
  - Asset: Fog and light setup
  - Asset: Eight test skinned dummies

**Gates**

- [ ] **PEQ1.G1** The slice scene passes `rensga cook --target ps2` with no budget override, and PCSX2 holds 30 fps with zero missing textures and `allocs=0`.
- [ ] **PEQ1.G2** The budget sheet holds measured numbers (triangles, VRAM evictions, skinning time), and the plan is changed if any limit failed.
- [ ] **PEQ1.G3** One asset travels Blender, cook, PCSX2 with the cooker's budget line in the log.

## PEQ2 · Core gameplay — 22.73% (cumulative 39.40%)

Player, camera, enemies, collectibles and the hub.

- [ ] **PEQ2.01** Phase 3 · Player and camera — Moves, character physics, right-stick camera, rumble · `9.09%`
  - Starts after: Phase 2; F8.02, F8.06, F6.03, F6.08, F3.06.
  - Done when: A test room in PCSX2 shows every move working with the pad; a bot input file repeats the jump-gap test and logs `peq: jump gap ok`; the camera test room logs zero clip frames; rumble events appear in the log; `alloc-check allocs=0` over 3,600 frames.
  - Asset: Pequi hero mesh, 5,000 triangles, 256x256 CLUT8, 30 bones
  - Asset: Hero clips: idle, idle fidget, walk, run, jump, double jump, fall, land, glide, spin, ground pound, roll, wall kick, hurt, death, victory dance, collect
  - Asset: Two alternative skins
  - Asset: Leaf scarf secondary motion (bones)
- [ ] **PEQ2.02** Phase 4 · Enemies and combat — Drones, critters, armadillos, turrets and AI · `7.58%`
  - Starts after: Phase 3; F6.01 (prefabs), F6.08.
  - Done when: All four enemy types fight in one PCSX2 test room; the bot input defeats each and logs `peq: enemy <type> down`; prof stays at 30 fps with the room's maximum enemy count; `alloc-check allocs=0`.
  - Asset: Pump drone, dry-thorn critter, robo-armadillo, steam turret meshes (800-2,000 triangles each)
  - Asset: Enemy clips, 6-8 per type
  - Asset: Crate prop
- [ ] **PEQ2.03** Phase 5 · Collectibles, hub and progress — Rains, drops, sprouts, hub that turns green, level doors · `6.06%`
  - Starts after: Phase 4; F6.01, F6.09, F6.11.
  - Done when: In PCSX2 the hub is dry at 0 rains and visibly greener at 1, 4 and 7 (three screenshots); a bot collects rains and logs `peq: hub stage 1/2/3`; the progress object round-trips through save and load with identical hash; `alloc-check allocs=0`.
  - Asset: Hub, dry and green texture sets and vertex colours
  - Asset: Seed spirits (7 rains, 800-3,000 triangles)
  - Asset: Vovó Buriti (old palm, hub guide)
  - Asset: Water drops, rain jars, spring stones, seed totems
  - Asset: VFX: rain return, collectible sparkle

**Gates**

- [ ] **PEQ2.G1** The bot input finishes a test course using every move and defeating all four enemy types, with the same state hash on PCSX2 and the Linux build.
- [ ] **PEQ2.G2** Hub stages 1, 2 and 3 show in three screenshots, and the progress object saves and loads with an identical hash.
- [ ] **PEQ2.G3** `alloc-check ... allocs=0` over 10,000 frames of the test course on PS2.

## PEQ3 · Levels and mini bosses — 18.18% (cumulative 57.58%)

Three levels and three mini bosses.

- [ ] **PEQ3.01** Phase 6 · Levels 1 and 2: Vereda and Chapada — Swamp with rising water, cliffs with wind currents · `9.09%`
  - Starts after: Phase 5 (the slice from phase 2 becomes Vereda); F6.01, F6.09, F8.05.
  - Done when: Both levels are playable start to finish with the bot; PCSX2 logs `peq: level vereda complete` and `peq: level chapada complete` with 2 rains each, prof at or above 30 fps, no missing textures, `alloc-check allocs=0`; each level passes the cooker budget with no override.
  - Asset: Vereda dos Buritis level, kit and textures
  - Asset: Chapada dos Ventos level, kit and textures
  - Asset: Wind fans, palm platforms, moving platforms
  - Asset: VFX: water splash, dust
- [ ] **PEQ3.02** Phase 7 · Level 3 and mini bosses — Fabrica Seca and three mini bosses · `9.09%`
  - Starts after: Phase 6; F6.01, F6.03.
  - Done when: Fábrica Seca and the three mini bosses are beaten by the bot in PCSX2 and log `peq: boss mosquito down`, `peq: boss kite down`, `peq: boss golem down`; 6 of 7 rains collectible; prof at or above 30 fps in each boss arena; cooker budgets pass.
  - Asset: Fabrica Seca level, kit and textures
  - Asset: Conveyors, pistons, steam, crushers
  - Asset: Three mini boss meshes (4,000-8,000 triangles) and clips (10-15 each)
  - Asset: VFX: steam, dust, sparks

**Gates**

- [ ] **PEQ3.G1** The bot finishes Vereda, Chapada and Fabrica Seca and beats the three mini bosses; 6 of 7 rains are collectible.
- [ ] **PEQ3.G2** Every scene passes cook and validate with no override; PCSX2 prof is at or above 30 fps in each level and boss arena.
- [ ] **PEQ3.G3** VRAM Map shows no thrashing: evictions per level stay flat after the first pass, and no missing textures.

## PEQ4 · Presentation and finale — 22.73% (cumulative 80.31%)

UI and saves, audio, the Baron and the ending.

- [ ] **PEQ4.01** Phase 8 · UI, menus and saves — HUD, pause, level select, gallery, memory card with icon · `7.58%`
  - Starts after: Phase 7; F6.04, F6.11, F6.12.
  - Done when: PCSX2 run: title to hub to pause to options to gallery by bot; `peq: saved rains=3` then `peq: loaded rains=3` with the same state hash after restart; the 480p option works; the save has the icon in the memory card image; `alloc-check allocs=0`.
  - Asset: Title, hub map, level select, HUD, pause, options, gallery art (CLUT8)
  - Asset: Bitmap font
  - Asset: Concept art images for the gallery (5 per level)
  - Asset: Memory card icon.sys and 3D icon model
- [ ] **PEQ4.02** Phase 9 · Audio — Themes, effects, mixing on 16 voices · `6.06%`
  - Starts after: Phase 8 (UI sounds); F6.10, F2.04.
  - Done when: PCSX2 run with sound: every level and boss plays its theme, the hub cross-fades at each rain, no underruns in `rensga: audio` lines over a 10-minute bot run; cooker audio budgets pass; `alloc-check allocs=0`.
  - Asset: Hub theme, dry and green versions
  - Asset: Three level themes and the boss theme
  - Asset: About 80 sound effects
- [ ] **PEQ4.03** Phase 10 · Final boss and ending — The Baron's pump-walker, finale, credits · `9.09%`
  - Starts after: Phases 7, 8, 9.
  - Done when: The bot runs from a fresh save through hub, three levels, three mini bosses and the Baron to the credits in PCSX2 and logs `peq: game complete` with all 7 rains; the save shows completion; prof at or above 30 fps in the arena; `alloc-check allocs=0`.
  - Asset: Baron Ressequido pump-walker mesh and 10-15 clips
  - Asset: Final arena level
  - Asset: Credits text and ending camera sequence
  - Asset: VFX: pump explosions, final rain

**Gates**

- [ ] **PEQ4.G1** Title to credits works by pad alone, including pause, options (480p, rumble) and the gallery; a save made mid-game continues with the same hash.
- [ ] **PEQ4.G2** The Baron fight is beaten by the bot and logs `peq: game complete`; the hub is fully green.
- [ ] **PEQ4.G3** A 10-minute audio run has no underruns and the cooker audio budget passes.

## PEQ5 · Ship — 19.69% (cumulative 100.00%)

PC and Android builds, performance, bot run, trailer, packaging.

- [ ] **PEQ5.01** Phase 11 · PC and Android builds with input — Windows, Linux, Android from the same project · `4.54%`
  - Starts after: Phase 10 (game complete on PS2); F6.05, F6.04, F10.01.
  - Done when: Windows .zip, Linux AppImage and Android APK are built by `rensga build`, each plays the title and the first level with a gamepad; Android also with touch; saves work on all three; the first-600-frame hashes match PS2.
  - Asset: Input map asset
  - Asset: Android touch overlay art
  - Asset: Launcher icons and package metadata
- [ ] **PEQ5.02** Phase 12 · Performance, budget pass and bot run — 30 fps everywhere, alloc-check, state-hash determinism · `9.09%`
  - Starts after: Phases 10, 11; F6.06, F6.13, F2.G2.
  - Done when: Every scene passes cook and validate with no override; PCSX2 prof is at or above 30 fps in every level and boss; the full bot run ends with `peq: game complete` on PS2, Windows, Linux and Android with the same final state hash; `rensga: alloc-check ... allocs=0` on all four.
  - Asset: Full-game bot input file
  - Asset: Hash checkpoint table per level
- [ ] **PEQ5.03** Phase 13 · Trailer footage, packaging and END_RESULTS row — Capture shots 16a-16g, legal pass, close the Pequi row · `6.06%`
  - Starts after: Phases 11, 12; F10.01, F10.02.
  - Done when: All seven trailer shots exist as labelled clips; THIRD_PARTY.md and licence files are in each package; the four END_RESULTS cells for Pequi are ticked with a dated note and links to the log lines and hashes.
  - Asset: Trailer clips 16a-16g
  - Asset: THIRD_PARTY.md entries
  - Asset: Release notes

**Gates**

- [ ] **PEQ5.G1** END_RESULTS cell: **PS2 · ISO in PCSX2 (your own BIOS)**. Built with `rensga build --target ps2`, passes the 10 checks of END_RESULTS.md (one project, built by the engine, fits, boots and finishes, same game everywhere, no per-frame allocation, 30 fps, saves, input, legal package), plus the Pequi extras: the hub turns green as rains return, texture streaming into the 4 MB VRAM shows no missing textures, rumble works on the DualShock 2 mapping, progressive scan (480p) option. Evidence: `peq: game complete`, final state hash, `rensga: prof` at or above 30 fps, `alloc-check ... allocs=0`, memory card save with icon.
- [ ] **PEQ5.G2** END_RESULTS cell: **Windows `.zip` (exe)**. All 10 checks pass: gamepad input, save folder, same final state hash as PS2, `allocs=0`, licence files and `THIRD_PARTY.md` in the package.
- [ ] **PEQ5.G3** END_RESULTS cell: **Linux AppImage**. All 10 checks pass, same evidence as the Windows cell.
- [ ] **PEQ5.G4** END_RESULTS cell: **Android APK**. All 10 checks pass with a gamepad and with touch, run on a phone (`tools/android/run_device_check.sh`), same final state hash as PS2. Built-but-not-run does not tick this gate.
- [ ] **PEQ5.G5** The Pequi row of END_RESULTS.md is ticked (all four cells) and its Status table has a dated note.
- [ ] **PEQ5.G6** The seven trailer shots 16a-16g are captured and labelled with target and source; no BIOS, Sony mark or ripped asset is in any package.

---

## Engine dependencies

Engine tasks are in [TASKLIST.md](../../../TASKLIST.md). A Pequi task may start once its engine tasks are ticked.

| Milestone | Needs | What for |
| --- | --- | --- |
| PEQ1 | F4.01, F4.08 (2D slice, 2D toolset) · F3.02 (asset database, VRAM map) · F6.02 (3D toolset) · F6.07 (data assets) · F8.01 (ps2gl 3D renderer) · F8.02 (3D gameplay and performance) · F8.03-F8.05 (PS2 primitives, blob shadow, test level) | Import, cook, strips, fog, texture streaming, the slice |
| PEQ2 | F8.02 · F8.06 (dual-analog controller, rumble) · F3.06 (input) · F6.01 (prefabs) · F6.03 (Animator) · F6.08 (physics) · F6.09 (particles) · F6.10 (animation and audio API) · F6.11 (saves) | Moves, camera, enemies, hub state |
| PEQ3 | F6.01 · F6.03 · F6.09 · F8.02 · F8.05 | Modular levels, moving platforms, wind, bosses |
| PEQ4 | F6.04 (UI, input actions) · F6.12 (UI API) · F6.11 · F6.10 · F2.04 (audio mixer) · F6.03 | HUD and menus, memory card, music, the Baron |
| PEQ5 | F6.05 (build pipeline) · F6.06 (profiler) · F6.13 (performance helpers) · F2.G2 (determinism) · F8.G1, F8.G2 (PCSX2 launcher, BIOS runner) · F10.01, F10.02 (packaging, docs) | Builds for four targets, budget pass, bot run, legal package |

## Asset register

Every asset from [DEMO_GAMES.md](DEMO_GAMES.md). Owner is the user. Status: `todo`, `wip`, `done`, `checked` (inside budget in the editor).

| Asset | Kind | Budget / notes | Owner | Phase | Status |
| --- | --- | --- | --- | --- | --- |
| Pequi (round body, seed-spike crest, leaf scarf) | Hero | 5,000 triangles, 256x256 CLUT8, 30 bones | user | 3 | todo |
| Pequi skin 1 | Hero | Same mesh and rig, 256x256 CLUT8 | user | 3 | todo |
| Pequi skin 2 | Hero | Same mesh and rig, 256x256 CLUT8 | user | 3 | todo |
| Seed spirits (7 rains) | Ally | 800-3,000 triangles | user | 5 | todo |
| Vovó Buriti (old palm, hub guide) | Ally | 800-3,000 triangles | user | 5 | todo |
| Pump drone | Enemy | 800-2,000 triangles, 6-8 clips | user | 4 | todo |
| Dry-thorn critter | Enemy | 800-2,000 triangles, 6-8 clips | user | 4 | todo |
| Robo-armadillo | Enemy | 800-2,000 triangles, 6-8 clips | user | 4 | todo |
| Steam turret | Enemy | 800-2,000 triangles, 6-8 clips | user | 4 | todo |
| Giant Mosquito Pump | Mini boss | 4,000-8,000 triangles, 10-15 clips | user | 7 | todo |
| Wind Kite Machine | Mini boss | 4,000-8,000 triangles, 10-15 clips | user | 7 | todo |
| Piston Golem | Mini boss | 4,000-8,000 triangles, 10-15 clips | user | 7 | todo |
| Baron Ressequido in his pump-walker | Final boss | 4,000-8,000 triangles, 10-15 clips | user | 10 | todo |
| Hub Nascente (dry and green versions) | Level | Strip-friendly kit; dry and green via vertex colour and CLUT8 sets | user | 5 | todo |
| Vereda dos Buritis | Level | Modular kit, several CLUT8 texture sets | user | 6 | todo |
| Chapada dos Ventos | Level | Modular kit, several CLUT8 texture sets | user | 6 | todo |
| Fábrica Seca | Level | Modular kit, several CLUT8 texture sets | user | 7 | todo |
| Crates | Prop | 20-600 triangles | user | 1 | todo |
| Water drops, rain jars, spring stones | Prop | 20-600 triangles | user | 5 | todo |
| Moving platforms, wind fans | Prop | 20-600 triangles | user | 6 | todo |
| Conveyors, pistons | Prop | 20-600 triangles | user | 7 | todo |
| Checkpoints (seed totems) | Prop | 20-600 triangles | user | 5 | todo |
| Pequi clips (17): idle, fidget, walk, run, jump, double jump, fall, land, glide, spin, ground pound, roll, wall kick, hurt, death, victory dance, collect | Animation | Skinned, 30 fps | user | 3 | todo |
| Enemy clips (4 types) | Animation | 6-8 clips each, 30 fps | user | 4 | todo |
| Boss clips (4 bosses) | Animation | 10-15 clips each, 30 fps | user | 7 | todo |
| Water splash, steam, dust | VFX | CPU particles | user | 6 | todo |
| Rain return (hub turning green), collectible sparkle | VFX | CPU particles | user | 5 | todo |
| Spin trail | VFX | CPU particles | user | 3 | todo |
| Blob shadow | VFX | Quad under the character | user | 3 | todo |
| Title, hub map, level select | UI | CLUT8 | user | 8 | todo |
| Collectible counters, HUD | UI | CLUT8 | user | 8 | todo |
| Pause menu, concept art gallery | UI | CLUT8 | user | 8 | todo |
| Save icon (memory card icon.sys and 3D icon) | UI | icon.sys plus a small 3D icon model | user | 8 | todo |
| Hub theme, dry version | Audio | Streamed from disc | user | 9 | todo |
| Hub theme, green version | Audio | Streamed from disc | user | 9 | todo |
| Level themes (3) | Audio | Streamed from disc | user | 9 | todo |
| Boss theme | Audio | Streamed from disc | user | 9 | todo |
| About 80 effects (jumps, spins, drops, robots, wind, water) | Audio | 16-voice mixer | user | 9 | todo |

## Open questions

- Real PS2 hardware pass for the release build belongs to engine gates F8.G3 and F10.G4, not to this list.
- Ad hoc or multiplayer features are out of scope for Pequi.
