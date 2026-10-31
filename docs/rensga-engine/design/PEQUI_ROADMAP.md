# Pequi roadmap

A roadmap for making **Pequi**, the PS2 3D mascot action platformer from [DEMO_GAMES.md](DEMO_GAMES.md), with Rensga Engine. This is game work, not engine work: it uses the engine and editor as they come out of the engine milestones. The weeks are human part-time effort (about 15 hours a week) and are used only as relative weights. Companion file: [PEQUI_TASKLIST.md](PEQUI_TASKLIST.md).

**Summary.** Build Pequi, a small stubborn fruit spirit, through a dry hub and three levels, with seven rains to free and the Baron's pump-walker at the end. The game is one Rensga project that ships as a PS2 ISO (run in PCSX2 with the user's own BIOS) plus Windows, Linux and Android builds. It starts with a vertical slice that proves the PS2 budgets, then adds the player, enemies and systems, then the content, then menus, audio, the final boss, the builds and the checks in [END_RESULTS.md](END_RESULTS.md).

**Stats.** total ~33 weeks · phases 14 · starts after: F8 (PS2 track: Engine 7 ps2gl 3D renderer, Engine 8 3D gameplay and performance, PS2 Template 0-4), F6 (full editor and API: animation, physics, UI, prefabs), F10 (packaging).

**Legend.**
- Group 1: pre-production and content (6 phases)
- Group 2: gameplay systems (5 phases)
- Group 3: polish and ship (3 phases)

**Console limits used throughout** (from [MODELLING.md](MODELLING.md) and the PS2 ConsoleTarget): 640x448, 32 MB RAM, 4 MB VRAM with about 2.2 MB taken by the framebuffers, textures streamed per frame, CLUT8 (RGBA over 256 px is refused by the GS upload today), 100,000 triangles per frame, CPU skinning with 20-40 bones, triangle strips, vertex lighting with several lights, fog, a blob shadow, dual analog, rumble, a memory card with icon.sys, 30 fps target. PS2 cannot boot in public CI, so PS2 checks run locally in PCSX2.

---

## Phase 0 · Pre-production and style bible

*Scope, pillars, level plans, budgets on paper*

- **Group:** 1 · pre-production and content
- **Estimate:** 1 wk
- **Starts after:** Engine milestone F4 done (2D slice, UI and font trial); F8.05 PS2 primitive test level
- **Engine tasks used:** F4.01, F8.05

Turn the pitch in DEMO_GAMES.md into a one-page design: the 60-90 minute route (hub, three levels, three mini bosses, the Baron), the move set, the seven rains and the collectible counts. Write the per-level budget sheet from MODELLING.md and the PS2 ConsoleTarget: at most 100,000 triangles per frame, about 1.8 MB of VRAM for textures and palettes after the framebuffers, 32 MB of RAM, 20-40 bones per skinned character. Pick the camera rules and the 30 fps target. Create the Rensga project from the PS2 3D template and write the asset list into its README. No engine work here; it only reads what the engine promises.

**Topics**

- **pillars and scope.** I am making Pequi, a 3D mascot action platformer for the PS2 with Rensga Engine (hub plus 3 levels and a boss, 60-90 minutes, a small fruit spirit with a spiky seed). Help me write a one-page design with 3 pillars, the move set, the 7 rains plus 300 water drops and 5 sprouts per level, and what to cut first if time runs short.
- **PS2 budget sheet.** Design a per-level budget sheet for a Rensga Engine PS2 game: 640x448, 32 MB RAM, 4 MB VRAM with about 2.2 MB used by framebuffers, CLUT8 textures, 100,000 triangles per frame, CPU skinning. List what each of the hub, three levels and four bosses may spend on meshes, textures, audio and scripts.
- **level flow maps.** Draw paper level flow maps for a PS2 3D platformer called Pequi: Nascente hub, Vereda dos Buritis (swamp, rising water), Chapada dos Ventos (cliffs, wind currents), Fabrica Seca (conveyors, pistons). For each, give the route, the two rains, set pieces and a mini boss arena.

**Exercises**

- **project setup.** Guide me through an exercise: create a Rensga Engine PS2 3D project named Pequi, add a README that lists every asset from a table I paste, and add a folder layout (Assets/Characters, Levels, Audio, UI, Scripts) that keeps cooked budgets easy to track.

**Done when:** A one-page design, the budget sheet and four level maps are committed under the project; `rensga cook` of the empty PS2 project prints the budget table with nothing over; the asset register below is copied into the project README.

## Phase 1 · Tools and data assets

*Blender kit, cook settings, data assets, import checks*

- **Group:** 1 · pre-production and content
- **Estimate:** 2 wk
- **Starts after:** Phase 0; F8.01, F6.02 (3D toolset), F6.07 (data assets)
- **Engine tasks used:** F6.02, F6.07, F8.01, F3.02

Build the content pipeline before the content. A Blender checklist and export preset from MODELLING.md (apply scale, triangulate, glb with vertex colour, no Draco). PS2 import settings in the Rensga asset inspector: CLUT8 as default, CLUT4 for small sprites, a refusal for RGBA over 256 px. Data assets for what designers tune: a MoveSet (speeds, jump heights, glide drag), a LevelInfo (rain ids, drop counts, sprout ids), an EnemyStats and a BossPhase table, so behaviour is configured and not hard-coded. Modular kit pieces for levels are cut into grids so no triangle crosses the near plane, and the cooker turns them into strips. Use the VRAM Map window to see textures against the budget.

**Topics**

- **Blender export preset.** Write a Blender 4.x export checklist and a Python script for a Rensga Engine PS2 game: apply modifiers and scale, triangulate, limit bone weights to 4, export glb with vertex colour and without Draco, origin at the feet, 1 unit = 1 metre.
- **CLUT8 texture workflow.** Explain how to prepare 256x256 CLUT8 textures for Rensga Engine on the PS2 when RGBA over 256 px is refused by the GS upload: palette reduction, sharing palettes between atlases, dithering, and checking the result in the editor's VRAM Map window.
- **data assets for tuning.** Design Rensga Engine data assets (MoveSet, LevelInfo, EnemyStats, BossPhase) for a PS2 3D platformer using the engine's reflection macros, so designers edit values in the inspector and the cooker packs them as binary.
- **modular level kit.** Describe a modular level kit for a PS2 3D platformer in Rensga Engine: tile sizes, grid subdivision so no polygon crosses the near plane, strip-friendly edge flow, and how kit pieces stay under the 100,000 triangle frame budget.

**Exercises**

- **round-trip one prop.** Give me an exercise: model a 300-triangle crate in Blender, export it as glb for Rensga Engine, import it with CLUT8 PS2 settings, cook it, and read the budget line the cooker prints for the asset.

**Done when:** A crate, a kit floor piece and the hero test mesh go Blender to cook to PCSX2 with the cooker budget lines in the log; MoveSet and LevelInfo data assets edit in the inspector and undo/redo; the importer rejects a 512x512 RGBA test texture with an error naming the asset.

## Phase 2 · Vertical slice: Vereda blockout

*One level that proves the budgets on PCSX2*

- **Group:** 1 · pre-production and content
- **Estimate:** 2.5 wk
- **Starts after:** Phase 1; F8.01, F8.02, F8.04, F8.05
- **Engine tasks used:** F8.01, F8.02, F8.04, F8.05

One playable slice of Vereda dos Buritis: a blockout level with the placeholder hero, one moving platform, fog, a blob shadow, vertex lighting with several lights and about 15 textured CLUT8 materials. The point is numbers, not looks: the scene must stay under 100,000 triangles, hold 30 fps in PCSX2 from the `rensga: prof` log, and stream textures through the 4 MB VRAM with no missing texture (`rensga: vram` lines, evictions counted). Test CPU skinning cost with 8 skinned enemies at 20-40 bones. If a budget fails here, change the plan in phase 0 before building more.

**Topics**

- **slice scene setup.** Design the vertical slice scene for Pequi in Rensga Engine on the PS2: a Vereda swamp blockout using modular kit pieces, fog, vertex-lit lights and a blob shadow, with a stats overlay for triangles, draw calls and VRAM.
- **texture streaming plan.** Explain how Rensga Engine streams textures into the PS2's 4 MB VRAM (about 2.2 MB framebuffers, CLUT area, LRU eviction) and how to group a level's materials into texture sets so no frame shows a missing or stale texture.
- **reading the prof log.** Explain how to read Rensga Engine's `rensga: prof` and `rensga: vram` log lines from a PCSX2 run to find whether a PS2 scene is limited by triangles, VU1 time, CPU skinning or texture uploads.

**Exercises**

- **find the budget wall.** Guide me through an exercise: add enemies one by one to my Rensga Engine PS2 slice until it drops under 30 fps in PCSX2, record triangles, bones and VRAM evictions at each step, and write the limits into my budget sheet.

**Done when:** `rensga cook --target ps2` passes with no override; PCSX2 run of 1,800 frames logs `rensga: prof` at or above 30 fps, `rensga: alloc-check ... allocs=0` and zero missing-texture warnings; a screenshot of the slice is committed; the budget sheet is updated with measured numbers.

## Phase 3 · Player and camera

*Moves, character physics, right-stick camera, rumble*

- **Group:** 2 · gameplay systems
- **Estimate:** 3 wk
- **Starts after:** Phase 2; F8.02, F8.06, F6.03, F6.08, F3.06
- **Engine tasks used:** F8.02, F8.06, F6.03, F6.08, F6.10, F3.06

Pequi's full move set as a Rensga script on the character controller: walk and run, roll for speed, jump, double jump, leaf glide, ground pound, spin attack (breaks crates, stuns), wall kick, coyote time and jump buffering. Everything tunable sits in the MoveSet data asset. The Animator graph drives 17 clips with transitions and events (footstep, spin hit frame). The camera follows on the right stick with collision pull-in, wall avoidance and a lock for tight spots. Rumble fires on landing and hit through the DualShock 2 mapping. Physics uses Rigidbody and character queries from the API; moving platforms carry the player. Skinned hero at 5,000 triangles and 30 bones, CPU skinning.

**Topics**

- **move set controller.** Implement a Pequi player controller as a Rensga Engine C++ script for a PS2 3D platformer: run, roll, double jump, glide, ground pound, spin attack and wall kick, with coyote time, jump buffering and all numbers in a MoveSet data asset. All math uses Scalar.
- **Animator graph.** Design the Rensga Engine Animator graph for Pequi: idle, fidget, walk, run, jump, double jump, fall, land, glide, spin, ground pound, roll, wall kick, hurt, death, victory, collect. List parameters, transitions, blend times and animation events.
- **right-stick camera.** Design a third-person right-stick camera for a PS2 platformer in Rensga Engine: orbit limits, auto-recentre, collision pull-in via raycasts, lock-on to tight spaces, and smoothing that stays deterministic.
- **rumble and feel.** Explain how to add DualShock 2 rumble and game feel (screen shake, hit stop, squash and stretch) to a Rensga Engine PS2 platformer using the Input API, and how to keep it identical in the PC and Android builds.

**Exercises**

- **tune the jump.** Give me an exercise: tune Pequi's jump in Rensga Engine so a double jump clears a 4-metre gap but not 5, using the MoveSet data asset, a test scene with markers, and a bot input file that checks it.
- **camera torture test.** Guide me through an exercise: build a test room in Rensga Engine with a narrow corridor, a ceiling and a wall, and tune the Pequi follow camera until it never clips or snaps.

**Done when:** A test room in PCSX2 shows every move working with the pad; a bot input file repeats the jump-gap test and logs `peq: jump gap ok`; the camera test room logs zero clip frames; rumble events appear in the log; `alloc-check allocs=0` over 3,600 frames.

## Phase 4 · Enemies and combat

*Drones, critters, armadillos, turrets and AI*

- **Group:** 2 · gameplay systems
- **Estimate:** 2.5 wk
- **Starts after:** Phase 3; F6.01 (prefabs), F6.08
- **Engine tasks used:** F6.01, F6.08, F8.02

Four enemy types as prefabs with a shared state machine in script and values in EnemyStats: pump drone (hovers and charges), dry-thorn critter (patrols, rolls when hit), robo-armadillo (armoured, only the ground pound breaks it) and steam turret (timed bursts). Hit detection uses trigger volumes and the spin hit frame from the Animator. Enemies are pooled, so no per-frame allocation. Each is 800-2,000 triangles with 6-8 clips, and they use the 20-40 bone limit modestly (CPU skinning cost is the limit). Add an enemy budget per room so skinning stays inside the EE time measured in phase 2. Damage, knockback, invulnerability frames, death puffs and drops.

**Topics**

- **enemy state machines.** Implement the AI of a pump drone, a dry-thorn critter, a robo-armadillo and a steam turret as Rensga Engine C++ scripts for a PS2 3D platformer, with a shared state machine, EnemyStats data asset, and no per-frame allocation.
- **combat hit detection.** Design the combat system for Pequi in Rensga Engine: the spin attack's active frames from an Animator event, trigger volumes, knockback, stun, invulnerability frames, and breakable crates.
- **enemy pooling and skinning cost.** Explain how to pool enemy prefabs in Rensga Engine on the PS2 and cap skinned enemies per room so CPU skinning (20-40 bones each) stays inside a 30 fps frame.
- **enemy prefabs.** Design prefabs and prefab variants for four enemy types in Rensga Engine so the shared parts (health, hit reaction, pickup drop) are one prefab and each type overrides only its model and stats.

**Exercises**

- **armadillo puzzle.** Give me an exercise: build a Rensga Engine PS2 room where a robo-armadillo can only be defeated by a ground pound after a spin stuns it, and write a bot input that proves the fight can be won.

**Done when:** All four enemy types fight in one PCSX2 test room; the bot input defeats each and logs `peq: enemy <type> down`; prof stays at 30 fps with the room's maximum enemy count; `alloc-check allocs=0`.

## Phase 5 · Collectibles, hub and progress

*Rains, drops, sprouts, hub that turns green, level doors*

- **Group:** 2 · gameplay systems
- **Estimate:** 2 wk
- **Starts after:** Phase 4; F6.01, F6.09, F6.11
- **Engine tasks used:** F6.01, F6.09, F6.11, F8.02

The progress model: 7 rains, 300 water drops and 5 hidden seed sprouts per level, kept in a save-state data object that every system reads. The Nascente hub exists in a dry and a green version of the same mesh, switched by vertex colour and texture sets, with the green steps tied to the number of rains. Doors open on rain counts; the seed totems are checkpoints. The rain-return time-lapse (video beat 1) is a scripted sequence with particles and a light colour animation. Collectible counters feed the HUD. Vovó Buriti, the hub guide, is a simple dialogue prop.

**Topics**

- **progress state.** Design a save-state object for Pequi in Rensga Engine on the PS2: rains collected per level, water drop counts, sprout flags, unlocked concept art and checkpoints, serialised small enough for one memory card block.
- **dry-to-green hub.** Explain how to make one hub mesh look dry or green in Rensga Engine on the PS2 using vertex colour and swapped CLUT8 texture sets, with a scripted time-lapse when a rain returns, while staying inside the VRAM budget.
- **doors and checkpoints.** Design the door, seed-totem checkpoint and respawn logic for a Rensga Engine PS2 3D platformer so the whole progress state can be rebuilt from the save data.

**Exercises**

- **green hub sequence.** Guide me through an exercise: build the rain-return sequence in Rensga Engine, with a particle burst, a light colour change and a texture swap, that plays when the first rain is collected and can be skipped.

**Done when:** In PCSX2 the hub is dry at 0 rains and visibly greener at 1, 4 and 7 (three screenshots); a bot collects rains and logs `peq: hub stage 1/2/3`; the progress object round-trips through save and load with identical hash; `alloc-check allocs=0`.

## Phase 6 · Levels 1 and 2: Vereda and Chapada

*Swamp with rising water, cliffs with wind currents*

- **Group:** 1 · pre-production and content
- **Estimate:** 3 wk
- **Starts after:** Phase 5 (the slice from phase 2 becomes Vereda); F6.01, F6.09, F8.05
- **Engine tasks used:** F6.01, F6.09, F8.05, F8.02

Build the two levels from the kits. Vereda dos Buritis: palm platforms, a water level that rises and falls on a script track (water is a mesh with scrolling UVs, no real fluid), splash particles, 2 rains, 300 drops, 5 sprouts. Chapada dos Ventos: cliffs, moving platforms, wind currents that push and lift the glide (a trigger volume that adds force), 2 rains, drops, sprouts. Keep each level in strip-friendly modules with several CLUT8 texture sets and one texture set per area so streaming stays within VRAM. Use prefabs for repeated props. Check the cooker budget after each module, not only at the end.

**Topics**

- **Vereda water.** Design the rising-water mechanic for Vereda dos Buritis in Rensga Engine on the PS2: a scripted water level, scrolling UV water mesh, splash particles, and a safe-ground rule so the player is never stuck underwater.
- **wind currents.** Implement wind currents for Chapada dos Ventos in a Rensga Engine PS2 3D platformer: trigger volumes that add upward force while Pequi is gliding, visual wind streak particles, and a debug gizmo.
- **level texture sets.** Explain how to split a PS2 level in Rensga Engine into areas with their own CLUT8 texture sets so the VRAM streamer never thrashes, and how to read the VRAM Map to prove it.
- **moving platforms.** Design moving platforms for a Rensga Engine PS2 platformer that carry the player deterministically, using a platform track data asset and the physics API.

**Exercises**

- **level budget pass.** Give me an exercise: take a Rensga Engine PS2 level of 80,000 triangles and cut it to 50,000 with no visible change, by merging kit pieces, culling hidden faces and subdividing only near-plane crossers.

**Done when:** Both levels are playable start to finish with the bot; PCSX2 logs `peq: level vereda complete` and `peq: level chapada complete` with 2 rains each, prof at or above 30 fps, no missing textures, `alloc-check allocs=0`; each level passes the cooker budget with no override.

## Phase 7 · Level 3 and mini bosses

*Fabrica Seca and three mini bosses*

- **Group:** 1 · pre-production and content
- **Estimate:** 3 wk
- **Starts after:** Phase 6; F6.01, F6.03
- **Engine tasks used:** F6.01, F6.03, F8.02

Fábrica Seca: conveyors that move the player, pistons on timed tracks, steam turrets, crushers, 2 rains. Then the three mini bosses: Giant Mosquito Pump (vereda), Wind Kite Machine (chapada), Piston Golem (factory), each 4,000-8,000 triangles, 10-15 clips, three phases driven by a BossPhase data asset, with arena hazards and a rain as the reward. Bosses share a boss controller prefab (health bar, phase switch, intro camera). The enemy skinning budget from phase 4 is revisited: a boss counts for several enemies in EE time.

**Topics**

- **conveyors and pistons.** Design the Fabrica Seca machinery for a Rensga Engine PS2 platformer: conveyors that carry the player, pistons and crushers on timed tracks, steam bursts, all deterministic and driven by data assets.
- **boss controller.** Implement a reusable boss controller prefab in Rensga Engine for Pequi: health, a BossPhase data asset, phase switching, hit reactions, intro camera and arena reset on death.
- **three mini bosses.** Design the Giant Mosquito Pump, the Wind Kite Machine and the Piston Golem as three-phase bosses for a PS2 platformer in Rensga Engine, each teaching one move (ground pound, glide, spin) and fitting 4,000-8,000 triangles and 10-15 clips.
- **boss skinning budget.** Explain how to budget CPU skinning time on the PS2 in Rensga Engine for a boss with 40 bones plus hazards, and what to do (lower bone count, split meshes, skip far animation) if the 30 fps target breaks.

**Exercises**

- **Piston Golem arena.** Guide me through an exercise: build the Piston Golem arena in Rensga Engine, with three phases from a BossPhase data asset and a bot input that beats it, and log each phase change.

**Done when:** Fábrica Seca and the three mini bosses are beaten by the bot in PCSX2 and log `peq: boss mosquito down`, `peq: boss kite down`, `peq: boss golem down`; 6 of 7 rains collectible; prof at or above 30 fps in each boss arena; cooker budgets pass.

## Phase 8 · UI, menus and saves

*HUD, pause, level select, gallery, memory card with icon*

- **Group:** 2 · gameplay systems
- **Estimate:** 2.5 wk
- **Starts after:** Phase 7; F6.04, F6.11, F6.12
- **Engine tasks used:** F6.04, F6.11, F6.12, F2.04

Build the interface with the editor's canvas UI at 640x448, in CLUT8 art with one bitmap font: title, hub map, level select, HUD (rains, drops, sprout pips, health), pause menu, options (progressive scan 480p, rumble, camera invert, volume), concept art gallery unlocked by sprouts. Menus are navigated with the pad through the UI API. Saves: write the progress object to the memory card with icon.sys and the 3D icon, with a corrupt-save fallback and a confirm dialog. The same code uses the save folder on PC and Android. Menus work with touch overlay on Android.

**Topics**

- **canvas layout at 640x448.** Design the Pequi UI in Rensga Engine for a 640x448 PS2 screen: title, hub map, HUD, pause, options and gallery, all CLUT8 art sharing one bitmap font and a texture budget under 300 KB of VRAM.
- **memory card save.** Explain how to save Pequi's progress in Rensga Engine on the PS2 memory card with icon.sys and a 3D icon, and how the same save API writes to the save folder on Windows, Linux and Android.
- **progressive scan option.** Explain how to offer a 480p progressive scan option in a Rensga Engine PS2 game, which settings change (framebuffer, VRAM budget, fps), and how to save and apply the choice.
- **pad menu navigation.** Design pad navigation for the Pequi menus in Rensga Engine's UI API: focus order, repeat on hold, back button, and mouse/touch parity on PC and Android.

**Exercises**

- **save, quit, continue.** Give me an exercise: implement title, Continue and New Game in Rensga Engine so a save made after collecting 3 rains reloads with the same hash, and a corrupted file falls back with a message.

**Done when:** PCSX2 run: title to hub to pause to options to gallery by bot; `peq: saved rains=3` then `peq: loaded rains=3` with the same state hash after restart; the 480p option works; the save has the icon in the memory card image; `alloc-check allocs=0`.

## Phase 9 · Audio

*Themes, effects, mixing on 16 voices*

- **Group:** 1 · pre-production and content
- **Estimate:** 2 wk
- **Starts after:** Phase 8 (UI sounds); F6.10, F2.04
- **Engine tasks used:** F6.10, F2.04

Music streamed from disc: hub theme in a dry and a green version that cross-fade as rains return, three level themes, a boss theme, and the final-boss variation. About 80 effects (jumps, spins, drops, robots, wind, water, UI). All pass through the 16-voice mixer with priorities (player and boss first, ambient last) and distance fades. The mixer and the music streaming run on the PS2 and in the PC/Android builds through the same Audio API. Check CPU time of the mixer in the prof log; keep the SPU2 RAM and disc stream budgets from the cooker.

**Topics**

- **voice priority.** Design a voice priority and ducking scheme for Pequi on Rensga Engine's 16-voice mixer: which sounds steal voices, how ambient wind and water fade by distance, and how boss music ducks effects.
- **dry and green music.** Explain how to make the Pequi hub theme exist as a dry and a green version that cross-fade in Rensga Engine on the PS2 when streaming music from disc, and what it costs in disc bandwidth and memory.
- **effect list and formats.** Write the list of about 80 sound effects for a PS2 3D platformer in Rensga Engine, with the sample rate, ADPCM format and length target of each category, and how the cooker reports audio memory.

**Exercises**

- **mixer stress.** Give me an exercise: run 16 voices plus music in Rensga Engine on PCSX2 with the audio-stress option, read the mixer time and underruns from the log, and cut effect length until there are no underruns.

**Done when:** PCSX2 run with sound: every level and boss plays its theme, the hub cross-fades at each rain, no underruns in `rensga: audio` lines over a 10-minute bot run; cooker audio budgets pass; `alloc-check allocs=0`.

## Phase 10 · Final boss and ending

*The Baron's pump-walker, finale, credits*

- **Group:** 2 · gameplay systems
- **Estimate:** 3 wk
- **Starts after:** Phases 7, 8, 9
- **Engine tasks used:** F6.01, F6.03, F8.02

Baron Ressequido in his pump-walker: 6,000-8,000 triangles, 10-15 clips, four phases (stomp and shock waves, pumps that must be hit, drained arena, wet finale). The arena uses the dry-to-wet hub effect in reverse. A scripted ending: the last rain, the valley fully green, victory dance, credits scroll with THIRD_PARTY credits, return to title with a completion mark in the save. Uses the boss controller from phase 7, the prefab system and camera cuts for the intro. This is the beat shown in trailer shot 16f.

**Topics**

- **pump-walker phases.** Design the four phases of the Baron Ressequido pump-walker boss fight for Pequi in Rensga Engine on the PS2: attacks, weak points, arena changes, a 4-minute target length, and the BossPhase data asset values.
- **ending sequence.** Design the ending of Pequi in Rensga Engine: the last rain, the hub going fully green, a camera cutscene driven by a timeline of events, the credits scroll, and the save completion flag.
- **boss fairness.** Explain how to make the Baron fight fair and readable on the PS2 at 30 fps in Rensga Engine: telegraphs, camera framing, invulnerability windows, checkpoint before the fight, and how a bot input checks it.

**Exercises**

- **beat the Baron.** Guide me through an exercise: build the Baron's first two phases in Rensga Engine and a bot input that beats them, logging each phase change and the final `peq: game complete`.

**Done when:** The bot runs from a fresh save through hub, three levels, three mini bosses and the Baron to the credits in PCSX2 and logs `peq: game complete` with all 7 rains; the save shows completion; prof at or above 30 fps in the arena; `alloc-check allocs=0`.

## Phase 11 · PC and Android builds with input

*Windows, Linux, Android from the same project*

- **Group:** 3 · polish and ship
- **Estimate:** 1.5 wk
- **Starts after:** Phase 10 (game complete on PS2); F6.05, F6.04, F10.01
- **Engine tasks used:** F6.05, F10.01, F6.04, F3.06

Build the same project with `rensga build` for Windows, Linux (AppImage) and Android (APK). The input map covers the PS2 pad, an Xbox-style gamepad on PC, a gamepad on Android, plus the Android touch overlay (left stick, right camera area, buttons). Desktop builds run at 640x448 scaled up and offer 1080p and 4K output with the same game state. Check that the saves go to the save folder, that rumble maps to the gamepad, and that the UI follows the focus on touch. No game script names a console.

**Topics**

- **input mapping.** Design the Rensga Engine input map for Pequi: DualShock 2 pad, Xbox-style gamepad, Android gamepad and Android touch overlay, with rumble, a right-stick camera, and the same action names on every platform.
- **desktop resolution scaling.** Explain how a Rensga Engine PS2 game at 640x448 scales to 1080p and 4K on Windows and Linux without changing game state, and how to keep the result deterministic.
- **Android touch overlay.** Design the Android touch overlay for Pequi in Rensga Engine: left stick, camera drag area, jump, spin, glide and roll buttons, size and placement, and how it coexists with a connected gamepad.

**Exercises**

- **one project, three exes.** Give me an exercise: build the Pequi project for Windows, Linux and Android with Rensga Engine, boot each to the title screen, and compare the first 600 frames' state hash with the PS2 build.

**Done when:** Windows .zip, Linux AppImage and Android APK are built by `rensga build`, each plays the title and the first level with a gamepad; Android also with touch; saves work on all three; the first-600-frame hashes match PS2.

## Phase 12 · Performance, budget pass and bot run

*30 fps everywhere, alloc-check, state-hash determinism*

- **Group:** 3 · polish and ship
- **Estimate:** 3 wk
- **Starts after:** Phases 10, 11; F6.06, F6.13, F2.G2
- **Engine tasks used:** F6.06, F6.13, F2.G2, F8.G1, F8.G2

Profile every level and boss in PCSX2 with the Rensga profiler and the `rensga: prof` log. Fix the worst scenes: reduce triangle counts, merge draw calls, cut skinned bones or enemy counts, shorten fog distance, simplify particles, repack texture sets so VRAM evictions fall. The cooker and validator must pass every scene with no override. Then write the full bot run (one input file for the whole game, about 70 minutes of frames at 30 fps), run it on PS2 (PCSX2, local) and on Windows, Linux and Android, and compare the final state hashes and the per-level checkpoints. Fix every divergence at its source: floats are a profile choice, so avoid undefined order and uninitialised data.

**Topics**

- **profiling pass.** Explain how to run a PS2 performance pass in Rensga Engine for a 3D platformer: read the profiler for EE time, VU1 time, skinning and texture uploads, find the worst room in each level, and the order to apply fixes.
- **bot run design.** Design a full-game bot input file for Pequi in Rensga Engine: recorded pad inputs per level with checkpoints, hash logging every 600 frames, and recovery rules, so the run finishes with `peq: game complete` and a final state hash.
- **determinism hunting.** Explain how to find why a Rensga Engine game's state hash differs between PS2 and PC after frame N: per-frame trace compare, bisecting scenes, and the usual causes (uninitialised memory, iteration order, physics step, time source).
- **alloc-check zero.** Explain how to make a Rensga Engine PS2 game hold `rensga: alloc-check ... allocs=0` for a whole 70-minute run: pools for enemies and particles, pre-sized arrays, and tools that find the allocation.

**Exercises**

- **first full run.** Guide me through an exercise: record the first bot input for my Pequi level in Rensga Engine and compare the checkpoint hashes between PCSX2 and the Linux build, then fix the first divergence.
- **budget pass checklist.** Give me an exercise: run the cooker budget report for every Pequi scene in Rensga Engine and fix the three scenes closest to the PS2 limits, writing the before and after numbers.

**Done when:** Every scene passes cook and validate with no override; PCSX2 prof is at or above 30 fps in every level and boss; the full bot run ends with `peq: game complete` on PS2, Windows, Linux and Android with the same final state hash; `rensga: alloc-check ... allocs=0` on all four.

## Phase 13 · Trailer footage, packaging and END_RESULTS row

*Capture shots 16a-16g, legal pass, close the Pequi row*

- **Group:** 3 · polish and ship
- **Estimate:** 2 wk
- **Starts after:** Phases 11, 12; F10.01, F10.02
- **Engine tasks used:** F10.01, F10.02, F8.G1

Capture the shots in docs/rensga-engine/design/TRAILER.md for Pequi: 16a title card, 16b hub time-lapse, 16c platforming montage, 16d glide in wind currents, 16e spin attack on three drones with the counter inset, 16f the pump-walker, 16g three-way split of PS2 emulator, PC at 4K and Android with a gamepad. Label each clip with its target and source (emulator or real hardware), as the brand guide asks. Package: THIRD_PARTY.md with every credit, license files for the engine and SDKs, no BIOS and no Sony marks, release notes. Tick the Pequi row in END_RESULTS.md only when all 10 checks plus the Pequi extras pass on every cell.

**Topics**

- **capture plan.** Write a capture plan for Pequi's trailer shots 16a to 16g from Rensga Engine on PCSX2, PC at 4K and Android: how to script the camera and bot input for each shot, frame rate, resolution, and labels for target and source.
- **legal checklist.** Write a release legal checklist for a Rensga Engine PS2 game: THIRD_PARTY.md credits, SDK licence files, no BIOS shipped, no Sony marks or logos, saying the game runs on the PS2, and how to document emulator use.
- **END_RESULTS row.** Explain how to verify the 10 checks and the Pequi extras (hub turns green, texture streaming with no missing textures, DualShock 2 rumble, 480p option) before ticking the Pequi row in Rensga Engine's END_RESULTS.md.

**Exercises**

- **shot 16e.** Guide me through an exercise: record shot 16e for the Pequi trailer in Rensga Engine on PCSX2, a spin attack through three drones with the collectible counter in a zoomed inset, by scripting a bot input and a fixed camera.

**Done when:** All seven trailer shots exist as labelled clips; THIRD_PARTY.md and licence files are in each package; the four END_RESULTS cells for Pequi are ticked with a dated note and links to the log lines and hashes.

---

## Risks

| Risk | Effect | Mitigation |
| --- | --- | --- |
| Texture streaming through 4 MB of VRAM stalls or shows missing textures | Frame drops, flicker, fails the Pequi extra check | Slice in phase 2 measures it; one texture set per level area; CLUT8 and CLUT4 only; watch evictions in the `rensga: vram` line |
| CPU skinning of many 30-bone characters is too slow | Under 30 fps in boss rooms | Enemy count caps per room; lower bone counts; skip far animation; measure in phase 2 and again in phase 7 |
| The engine's PS2 3D phases (F8.01, F8.02) slip | The whole game waits | Phases 0 and 1 need no 3D engine; the slice (phase 2) is the first one that does |
| Art volume: 30-bone hero, 4 bosses, 4 levels, 80 effects, all by one owner | Content phases run long | Modular kits, shared enemy prefabs, cut sprouts and skins first; the asset register tracks it |
| PS2 and PC state hashes diverge | Fails check 5 | Hash every 600 frames from the first level on; fix divergences when they appear, not at the end |
| PS2 cannot boot in public CI and needs the user's BIOS | Regressions seen late | Local PCSX2 runs every phase; self-hosted runner (F8.G2) when it exists; never commit a BIOS |
| Android phone run is deferred | Check 9 on Android unproven | Android build and bot hash are required for the final gate; schedule the phone run before PEQ5 closes |
| Third-party assets with unclear licences | Check 10 fails | Only own, commissioned, CC0 or CC-BY assets; credit in THIRD_PARTY.md; no ripped assets |
