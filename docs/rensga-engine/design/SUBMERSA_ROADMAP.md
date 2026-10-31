# Submersa roadmap

Roadmap for making **Submersa**, the PSX survival horror demo of Rensga Engine, with Rensga Engine. This is game work, not engine work. It follows the format of the engine roadmaps (phases, estimates, Claude prompts) and is written to be read next to [DEMO_GAMES.md](DEMO_GAMES.md), [END_RESULTS.md](END_RESULTS.md), [MODELLING.md](MODELLING.md) and [TRAILER.md](TRAILER.md). Its tasks and weights are in [SUBMERSA_TASKLIST.md](SUBMERSA_TASKLIST.md).

## Summary

A 40-60 minute fixed-camera survival horror game for PSX (320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), set in the drowned town of Santa Luzia do Vau. It is built as one Rensga Engine project and ships as a PSX BIN/CUE plus Windows, Linux and Android builds. The roadmap goes from one measured room to the packaged release and the closed END_RESULTS row.

## Stats

- **Total:** ~30.5 weeks (human part-time, about 15 hours a week; used only as relative weights, not a schedule)
- **Phases:** 14 (numbered 0 to 13)
- **Starts after:** F9 (PSX track, at least F9.01-F9.05, F9.08, F9.12-F9.14, F9.16), F6 (full editor and API) and F10 (packaging) for the last phase. Each phase lists the exact engine tasks.
- **Target:** PSX, plus Windows, Linux and Android

## Legend

- **Group 1:** pre-production and content
- **Group 2:** gameplay systems
- **Group 3:** polish and ship

## Phases at a glance

| # | Group | Phase | Estimate | Starts after |
| --- | --- | --- | --- | --- |
| 0 | 1 | Vertical slice room | 2 wk | F9.08, F9.12, F9.13, F9.14, F9.16, F9.28 |
| 1 | 1 | Art kit and Inês | 2.5 wk | Submersa 0; F9.05, F9.08, F9.13, F9.21, F9.27, F6.01, F6.02 |
| 2 | 2 | Player and fixed cameras | 2 wk | Submersa 1; F6.03, F6.10, F9.19, F9.20, F9.22, F9.29 |
| 3 | 2 | Items, inventory and weapons | 2 wk | Submersa 2; F6.08, F6.11, F9.22 |
| 4 | 2 | Enemies and AI | 2.5 wk | Submersa 3; F6.03, F9.08, F9.22 |
| 5 | 2 | Locks, puzzles and notes | 2 wk | Submersa 3; F6.04, F6.11 |
| 6 | 1 | Main street and church | 2.5 wk | Submersa 5; F9.09, F9.24 |
| 7 | 1 | School and pumping station | 2.5 wk | Submersa 6; F9.09 |
| 8 | 2 | UI, menus and saves | 2 wk | Submersa 5, Submersa 6; F6.04, F6.12, F9.03, F9.24 |
| 9 | 1 | Audio | 1.5 wk | Submersa 2; F9.06, F9.23 |
| 10 | 2 | Dam control room and the Caretaker | 2.5 wk | Submersa 4, Submersa 7, Submersa 8, Submersa 9; F6.03, F9.08 |
| 11 | 3 | Performance and budget pass | 2 wk | Submersa 10; F9.09, F9.17, F6.06, F6.13 |
| 12 | 3 | PC and Android builds, bot run and determinism | 2.5 wk | Submersa 11; F4.01, F6.05, F9.G3, F10.01 |
| 13 | 3 | Trailer footage and packaging | 2 wk | Submersa 12; F9.10, F10.01, F10.02 |
| | | **Total** | **30.5 wk** | |

Prompts below can be pasted into a fresh chat. Each one names the game, the console and the engine.

## Submersa 0 · Vertical slice room

*one room, two cameras, budgets measured*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2 wk
- **Starts after:** F9.08, F9.12, F9.13, F9.14, F9.16, F9.28

Create the Submersa project from the PSX 3D template and build one room: a corner of the main street, made from grid tiles, with two fixed cameras, the template's segmented capsule as Inês and one placeholder mud walker. The point is to measure, not to polish. Use the VRAM map window for the texture pages, the PSX preview mode for wobble, affine warp and sorting, and Build and Run in PCSX-Redux. Set the draft budgets in ConsoleTarget.json (RAM split, VRAM pages, triangles per frame, ordering table size) from what the room actually costs, and write them into the project README next to the asset list. Build the same project for Windows or Linux from day one and compare the state hash, so fixed-point drift is caught while it is cheap. Limits from MODELLING.md: 200-500 triangles for the hero, 100-300 for enemies, 64-128 px CLUT4 textures, subdivided floors and walls.

**Topics**

- **project and budget sheet.** Design the folder layout, README asset list and first budget sheet for a Rensga Engine PSX project: how to split 2 MB of RAM between code, the resident area, characters and audio buffers, and how to split the 1 MB of VRAM after two 320x240 framebuffers. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **room from grid tiles.** Explain how to build a small PSX room from grid tiles in the Rensga Engine editor so that floors and walls are subdivided enough to avoid affine texture warping, and how to check it in the PSX preview mode. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **fixed camera cuts.** Design a data-driven fixed camera setup for one PSX room in Rensga Engine: two camera objects, a trigger volume at the doorway that switches between them, and how the cut is kept deterministic in fixed-point. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **measure the slice.** List what I should measure in the first PSX room built with Rensga Engine (triangles per frame, ordering table usage, VRAM pages, RAM, frame time in the rensga prof log) and which numbers should go into ConsoleTarget.json as budgets. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **slice budget run.** Give me an exercise: build one room with two fixed cameras in a PSX project in Rensga Engine, cook it, read the budget report, and write down the first draft budgets. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **PC versus PSX hash.** Guide me through an exercise: build the same one-room PSX scene for the PSX target and for Linux in Rensga Engine, run 600 frames on each, and compare the state hashes. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 1 · Art kit and Inês

*tile textures, prop kit, segmented character*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 0; F9.05, F9.08, F9.13, F9.21, F9.27, F6.01, F6.02

Build the shared art kit. Four 4-bit tiled textures (mud, plaster, tile, rust) are packed into VRAM pages with shared CLUT groups; the VRAM map window shows what each page holds. Make the first prop kit (doors, crates, pews, desks, lamp posts) at 20-150 triangles each, vertex coloured where possible. Model Inês Costa as 11 rigid pieces (400 triangles, one 128x128 CLUT4 atlas) in Blender, export glTF 2.0 per MODELLING.md and import her as a SegmentedMeshRenderer prefab. Meshes with UVs and textures arrive with PSX Engine 7, so this phase starts when the GTE 3D backend and the import settings exist. Remaining props, enemies and area-specific art are made in the area phases.

**Topics**

- **VRAM page plan.** Design a VRAM page plan for a PSX game made with Rensga Engine: four tiled 4-bit environment textures, one character atlas per enemy, a font and UI sprites, with CLUT sharing groups, inside 1024x512 16-bit units shared with two framebuffers. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **segmented character from Blender.** Walk me through modelling an 11-piece segmented PSX character of about 400 triangles in Blender, exporting glTF 2.0 and importing it as a SegmentedMeshRenderer prefab in Rensga Engine, with no skin weights. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **tileable 4-bit textures.** Explain how to paint tileable 16-colour textures for mud, plaster, tile and rust for a PSX game, so they read well at 64x64 with affine mapping and share palettes. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **prop kit rules.** Give me rules for a PSX prop kit (doors, crates, pews, desks, lamp posts) in Rensga Engine: triangle counts, when to use vertex colour instead of a texture, origins and scale in metres. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **pack the kit.** Give me an exercise: take four CLUT4 tile textures and a character atlas for a PSX project in Rensga Engine, place them with the VRAM packer, and read the VRAM map to find wasted space. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **Inês rig.** Guide me through an exercise: build the 11-piece segmented rig for a PSX character in Blender, import it into Rensga Engine, and check the pieces in the PSX preview mode. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 2 · Player and fixed cameras

*camera-relative control, tank option, Inês's clips*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Submersa 1; F6.03, F6.10, F9.19, F9.20, F9.22, F9.29

Build Inês's controller on the fixed-point character controller from the template: camera-relative movement by default, a tank-control option, walk and run, turn, push, pick up and open door. A CameraZone component (data, not code per room) holds the camera pose and the trigger volumes at doorways. When a cut happens, the held stick direction stays valid until the player releases it, so the character does not turn back. Add the 11 rigid-transform clips (idle, walk, run, turn, aim, shoot, swing, hurt, death, push, pick up) and wire them into an Animator graph. All maths use Scalar (20.12). Using float in game code is a compile error on PSX, so this phase also proves the hint works.

**Topics**

- **camera-relative control.** Design camera-relative character control with a tank-control option for a fixed-camera PSX game in Rensga Engine, in 20.12 fixed-point, including what happens to the held stick direction when the camera cuts. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **CameraZone component.** Design a CameraZone component for Rensga Engine: a serialized camera pose per room, trigger volumes at doorways, and how the editor shows the camera frustum so level designers can place cuts without code. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **Animator for rigid clips.** Explain how to build an Animator graph in Rensga Engine for a segmented PSX character: 11 rigid-transform clips, transitions driven by parameters, and how health state selects a limping or hurt walk. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **fixed-point movement.** Explain pitfalls of 20.12 fixed-point movement and collision for a PSX game in Rensga Engine: overflow in room coordinates, rounding of speeds, and how to keep the result identical on PC. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **tank and relative toggle.** Give me an exercise: implement a runtime toggle between camera-relative and tank controls for a character in Rensga Engine and test it across a camera cut. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **clip set for Inês.** Guide me through an exercise: author the 11 clips for a segmented PSX character as rigid transform keys in the Rensga Engine Animation window and hook them to an Animator graph. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 3 · Items, inventory and weapons

*item data, ammo, revolver, flare gun, machete*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Submersa 2; F6.08, F6.11, F9.22

Items are data assets (ItemData): keys, land deeds, ammo, flares, bandages, documents. The inventory is a fixed-size grid held in plain arrays so it saves in one block. Add the three weapons. The revolver has few bullets and aims with a raycast against enemy hit volumes. The flare gun lights a fixed-point light that the enemies react to. The machete is a short melee arc. Health has no HUD bar: a health state (fine, caution, danger) selects Inês's pose and walk clip. Use physics queries from PSX API 3 (kinematic shapes and rays). The inventory screen itself is built in the UI phase; here a debug list is enough.

**Topics**

- **ItemData assets.** Design ItemData assets for Rensga Engine for a PSX survival horror game: keys, deeds, ammo, flares and bandages as GUID-referenced assets, with fields for stack size, usable-on targets and combine rules. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **weapons with raycasts.** Design a revolver, a flare gun and a machete for Inês in a PSX game built with Rensga Engine, using fixed-point raycasts against enemy hit volumes, with ammo scarcity and no per-frame allocation. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **health without a HUD.** Explain how to show player health without a HUD bar in a PSX survival horror game made with Rensga Engine, using a three-state health value that selects Inês's pose and walk animation. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **inventory as a fixed grid.** Design an inventory stored as a fixed-capacity grid of item ids and counts for a PSX game in Rensga Engine, small enough to save in one memory card block, with deterministic ordering. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **ammo economy.** Give me an exercise: work out how much ammo, flares and bandages to place across three areas of a PSX survival horror demo so that running is often better than fighting, and write it as a data table. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **flare light.** Guide me through an exercise: make a flare gun that spawns a short-lived light in Rensga Engine on PSX, within the 3-lights-per-object limit, and make an enemy react to it. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 4 · Enemies and AI

*mud walker, leech swarm, bell ringer*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 3; F6.03, F9.08, F9.22

Three enemy types, each with one prefab, an Animator graph and a small state machine written as a behaviour with exposed fields. The mud walker (250 triangles) lurches toward sound and sight and can be outrun. The leech swarm (80 triangles) is one mesh instanced many times and lives in flooded floors. The bell ringer (350 triangles) is tall and slow and guards the church. Each enemy has idle, walk, attack, hurt and death clips (4-6). Sight and hearing use fixed-point distance checks and room zones. A cap of simultaneous enemies per room comes from the budget sheet. Enemies follow the player through a door only if the room data says so, which also keeps area streaming simple.

**Topics**

- **enemy state machine.** Design a data-driven enemy state machine (idle, investigate, chase, attack, hurt, death) for a PSX game in Rensga Engine, with exposed fields for speed and sight, running in 20.12 fixed-point. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **instanced leech swarm.** Explain how to draw an 80-triangle leech mesh many times as a swarm on PSX with Rensga Engine without per-frame allocation, and how to keep the ordering table cost low. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **sight and hearing in fixed-point.** Design cheap sight and hearing checks for enemies on PSX in Rensga Engine using room zones and squared fixed-point distances, avoiding overflow. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **enemy budgets per room.** Explain how to set an enemy cap per room from the triangle and ordering table budget in a Rensga Engine PSX project, and how the validator should refuse a scene that exceeds it. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **outrunnable walker.** Give me an exercise: tune a mud walker so that the player can outrun it but not stand still, using exposed fields in Rensga Engine, and record a replay that proves it. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **swarm stress test.** Guide me through an exercise: stress test a leech swarm of 30 instances in one PSX room in Rensga Engine and read the ordering table and frame time numbers. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 5 · Locks, puzzles and notes

*keys, gauge puzzles, deeds, documents, ferryman*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Submersa 3; F6.04, F6.11

The key-and-lock system is data: each lock names the key item and sets a world flag when opened. World flags live in one bit array that also holds puzzle and boss state, so saving is one block. Build the three puzzles from the survey work: line up the old water gauges, read the dam's pressure dial, and match land deed numbers. Each puzzle is a prefab with exposed fields and a serialized event that fires when it is solved. Add 6-8 note and document assets (plain text) and the old ferryman, the only survivor, who gives hints in short dialogue. Use serialized events and the Animator instead of new code paths where you can.

**Topics**

- **lock and key data.** Design a lock-and-key system as data for a PSX survival horror game in Rensga Engine: Lock components that name an ItemData key, a world flag bit and a serialized event on open. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **world flags and save state.** Design a world flag bit array for a PSX game in Rensga Engine that holds doors, puzzles, pickups and boss state, and explain how to keep it deterministic and small enough for one memory card block. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **gauge and dial puzzles.** Design three puzzles for a PSX survival horror game (aligning water gauges, reading a pressure dial, matching land deed numbers) as Rensga Engine prefabs with exposed fields and a solved event. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **notes and dialogue as assets.** Explain how to store notes, documents and short dialogue as plain text assets in a Rensga Engine PSX project and fit them in the bitmap font and VRAM budget. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **solve path.** Give me an exercise: draw the dependency graph of keys, locks and puzzles for the church area of a PSX survival horror demo and check it has no dead end. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **gauge puzzle prefab.** Guide me through an exercise: build the water-gauge puzzle as a prefab in Rensga Engine with a solved UnityEvent that opens a door. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 6 · Main street and church

*first two areas, streaming from disc*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 5; F9.09, F9.24

Build the main street (the hub) and the church (nave and bell tower) from grid tiles: 8-12 rooms per area with fixed cameras and fog. Each area is a pack streamed from disc; the doorway transition is hidden by the door animation while the next area's pack loads in the background. Place the first enemies, items, locks and the first puzzle chain, and the old ferryman's boat on the main street. Use the PSX preview mode and the budget report on every room before moving on. Light each room with at most 3 lights per object and depth-cue fog, and bake the rest into vertex colour. Keep an eye on resident RAM when two area packs overlap.

**Topics**

- **area pack layout.** Design the area pack layout and streaming plan for a PSX game made with Rensga Engine: main street as the hub, a church area of 8-12 rooms, loading the next pack behind a door animation in a 2 MB RAM budget. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **room design with fixed cameras.** Give me a method to design a fixed-camera room for a PSX survival horror game: camera placement so geometry outside the view can be culled, fog distance, light placement and enemy sight lines. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **church set.** Plan the church (nave and bell tower) as grid tiles for a PSX game at 8-12 rooms: room list, key items, enemy placement and the bell ringer encounter, with polygon budgets per room. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **budget report workflow.** Explain how to use the Rensga Engine budget report and PSX preview mode after each room is built, and what to do when a room is over its triangle or sorting budget. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **hide the loading.** Give me an exercise: create a door transition between two area packs in a PSX project in Rensga Engine and measure that no frame takes longer than 100 ms during the load. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **room checklist.** Guide me through an exercise: take one finished room and run my checklist (triangles, VRAM page, lights, fog, camera cuts, sort glitches) in the PSX preview mode of Rensga Engine. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 7 · School and pumping station

*areas 3 and 4, flooded rooms*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 6; F9.09

Build the school (two classrooms and a yard) and the pumping station (three rooms and its tunnels) on the same rules. The school teaches the deed-matching puzzle and uses desks as cover. The pumping station holds the pressure dial puzzle, flooded floors with leech swarms, rust textures and the route to the dam. Reuse tile pages from the church so the VRAM plan does not grow, and add only what these areas need. Check the whole route: every key found before its lock, ammo and flares enough for a careful player, the hint from the ferryman on the way.

**Topics**

- **school area plan.** Plan the school area (two classrooms and a yard) for a PSX survival horror demo built with Rensga Engine: rooms, cameras, deed-matching puzzle, enemies and items, within one area pack. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **flooded rooms on PSX.** Explain how to draw a flooded floor on PSX in Rensga Engine: a semi-transparent animated polygon layer, sorting with the ordering table, and keeping it cheap. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **reuse tile pages.** Explain how to design a second and third area in a PSX game so they reuse the VRAM tile pages from the first area, and how CLUT sharing helps in Rensga Engine. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **route and pacing.** Design the route and pacing of a 40-60 minute PSX survival horror demo across a church, a school and a pumping station: where saves, ammo, fights and chases go. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **shared palette.** Give me an exercise: change two area textures in a PSX project in Rensga Engine so they share one CLUT, and check the VRAM map before and after. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **pressure dial puzzle.** Guide me through an exercise: build the dam pressure dial puzzle in the pumping station of a Rensga Engine PSX project and test it with a scripted run. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 8 · UI, menus and saves

*title, inventory, map, save radios, memory card*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Submersa 5, Submersa 6; F6.04, F6.12, F9.03, F9.24

Build the UI as canvas assets in 4 bits with the one bitmap font: title, inventory grid, item close-up (a rotating mesh), map, save radio dialog, options (tank control, volume) and the memory card icon. There is no health bar. Save at transistor radios: one memory card block with an icon, holding world flags, inventory, position, area and a checksum. Save, quit and continue must work, and an unreadable or full card gives a clear message. UI navigation uses the pad only. The memory card is emulated by PCSX-Redux, so test with a fresh card file per run.

**Topics**

- **menu flow.** Design the menu flow (title, new game, continue, inventory, item close-up, map, save radio dialog, options) for a PSX game in Rensga Engine, navigable by pad only and drawn with one 4-bit bitmap font. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **memory card save format.** Design a save format for one memory card block with an icon for a PSX game in Rensga Engine: world flags, inventory, position, area, a version byte and a checksum, plus handling of missing, full and corrupt cards. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **map screen.** Design a map screen for a PSX survival horror game in Rensga Engine that is drawn from room data (visited flags, doors, current room) with few polygons. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **item close-up.** Explain how to show a rotating 3D item close-up in a PSX inventory screen using the GTE in Rensga Engine, with its own camera and lighting. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **save round trip.** Give me an exercise: save in a PSX project in Rensga Engine, quit PCSX-Redux, restart with the same card file, continue, and check that the state hash equals the hash before quitting. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **corrupt card test.** Guide me through an exercise: damage a save block on purpose and make the PSX game in Rensga Engine report it instead of loading broken state. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 9 · Audio

*XA music, VAG effects, ambience, SPU budget*

- **Group:** 1 (pre-production and content)
- **Estimate:** 1.5 wk
- **Starts after:** Submersa 2; F9.06, F9.23

Three XA music tracks (title, safe room, boss), ambience loops (wind, dripping, bell) and about 30 VAG effects (steps on mud and tile, doors, gun, flare, enemy groans). All VAG data must fit the SPU RAM budget (512 KB on PSX, minus reverb), so decide which sounds stay resident per area and which are loaded with the area pack. XA music and area streaming share the CD drive, so the plan schedules reads: music keeps playing while the next area loads, and the load must not starve it. Use the audio API for voices and residency, and test stress cases with many sounds at once.

**Topics**

- **SPU RAM plan.** Design an SPU RAM plan for a PSX game made with Rensga Engine: about 30 VAG effects and 3 looping ambiences in 512 KB with a reverb area, split between always-resident sounds and per-area sounds. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **XA and streaming together.** Explain how to schedule CD reads so XA music keeps playing while an area pack streams in on PSX, and how Rensga Engine can log a stall instead of hiding it. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **sound design for horror.** Give me a sound design plan for a PSX survival horror demo: which effects need variations, how to use silence, and how to keep the effect count near 30. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **audio authoring.** Explain how to prepare VAG effects and XA music tracks for a Rensga Engine PSX project: sample rates, looping, mono versus stereo, and import settings. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **voice limit test.** Give me an exercise: trigger enemy groans, steps, ambience and a gunshot together in a PSX scene in Rensga Engine and check the voice count against the 24-voice limit. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **music during loading.** Guide me through an exercise: play an XA track while loading a second area pack in a Rensga Engine PSX project and measure underruns. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 10 · Dam control room and the Caretaker

*boss fight, ending, credits*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 4, Submersa 7, Submersa 8, Submersa 9; F6.03, F9.08

Build the dam control room as the last area: a flooded room with the pressure dial as part of the fight. The Caretaker (700 triangles, 128 px CLUT4, 8 clips) is the one boss. The fight has phases driven by the Animator and world flags, uses flares, ammo and the room itself, and always leaves a way to win without a full inventory. Then the ending: a short scene using the same engine tools, the floodgate sequence, credits and the final `game complete` line. Make sure the finale can be reached from any valid save and cannot soft lock.

**Topics**

- **boss phases.** Design a three-phase boss fight for the Caretaker in a PSX survival horror game built with Rensga Engine: Animator parameters, world flag triggers, weak points and how the room is used, with 8 clips. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **anti soft lock.** Explain how to design a PSX survival horror finale so the player cannot soft lock (no ammo, no key, no health) and how a validator script in a Rensga Engine project can check it. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **flooded control room.** Plan the dam control room as a flooded area of a PSX game in Rensga Engine: layout, cameras, water effect, the pressure dial and the boss entrance. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **ending and credits.** Design the ending sequence and credits for a PSX game in Rensga Engine using the Animator and a camera path, ending in a log line that a bot test can read. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **boss with minimal ammo.** Give me an exercise: test the Caretaker fight in a PSX project in Rensga Engine with a bot that has one bandage and three bullets, and tune it until it is winnable. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **finale from any save.** Guide me through an exercise: load saves from different points before the finale in a Rensga Engine PSX game and check each one can reach the credits. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 11 · Performance and budget pass

*30 fps, RAM, VRAM, sorting, streaming*

- **Group:** 3 (polish and ship)
- **Estimate:** 2 wk
- **Starts after:** Submersa 10; F9.09, F9.17, F6.06, F6.13

Run the full game under the profiler and fix what is over. Check frame time per room from the `rensga: prof` log, ordering table depth, polygon sort errors (sort bias on props and characters), scratchpad use, RAM peak during streaming, VRAM page use and SPU RAM. Remove every budget override. Lower triangle counts, subdivide big polygons, move hot data to the scratchpad and trim fog distance where it helps. Keep a table of rooms with their numbers. Re-run every earlier done-when check at the end, because optimisation breaks things.

**Topics**

- **profiling by room.** Explain how to read the Rensga Engine prof log on PSX in PCSX-Redux and build a per-room table of frame time, triangles, ordering table use and RAM, to find the worst five rooms. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **sort bias fixes.** Explain how to fix ordering-table sorting glitches on PSX (no z-buffer) with sort bias on props and characters in Rensga Engine, including doors, pews and the player standing next to walls. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **RAM and streaming peaks.** Explain how to find and reduce RAM peaks during area streaming in a 2 MB PSX game made with Rensga Engine, including arena sizes and scratchpad use. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **budgets with no override.** Explain how to remove all budget overrides from a Rensga Engine PSX project one by one and keep a regression check in CI for each number. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **worst five rooms.** Give me an exercise: find the five slowest rooms in a PSX game from its prof log in Rensga Engine and cut each one by 15 percent without changing how it looks. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **regression test.** Guide me through an exercise: write a script that fails when any room exceeds its frame time or triangle budget in a Rensga Engine PSX project. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 12 · PC and Android builds, bot run and determinism

*same game everywhere, state hash*

- **Group:** 3 (polish and ship)
- **Estimate:** 2.5 wk
- **Starts after:** Submersa 11; F4.01, F6.05, F9.G3, F10.01

Build the Windows .zip, the Linux AppImage and the Android APK from the same project with the editor's Build button or `rensga build`. PC and Android builds use the fixed-point path too, so the game is the same. Map the input: PSX pad, a gamepad on PC, and a touch overlay plus gamepad on Android with the same actions. Record a bot input file that plays the whole demo from the title to the credits, including saves and the boss. Run it on every target and compare the final state hash and the periodic hashes to find the first frame where one differs. The Android phone run waits for a phone (tools/ci/android_device.sh); until then the APK is built and the Android bot run uses the same player code on the desktop GLES path.

**Topics**

- **input mapping.** Design one input action map in Rensga Engine for a fixed-camera PSX game that works with the PSX pad, a gamepad on Windows and Linux, and a touch overlay on Android, including the tank control option. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **bot input recording.** Design a bot input recording for a Rensga Engine game: a file of per-frame pad states that plays a whole 40-60 minute demo including saves, with a state hash logged every 60 frames. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **finding a hash divergence.** Explain how to find the first frame where the state hash differs between a PSX build in PCSX-Redux and a Linux build of the same Rensga Engine project, and the usual causes (uninitialised memory, float, ordering). Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **three packages.** Explain what goes into the Windows zip, Linux AppImage and Android APK of a Rensga Engine game that is also a PSX game, and what must be identical across them. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **hash every 60.** Give me an exercise: log the state hash every 60 frames in a game made with Rensga Engine and diff the logs from two targets to find a divergence. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **touch layout.** Guide me through an exercise: design a touch overlay for a fixed-camera horror game with tank and camera-relative options in Rensga Engine. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Submersa 13 · Trailer footage and packaging

*TRAILER shots 14a-14g, release, END_RESULTS row*

- **Group:** 3 (polish and ship)
- **Estimate:** 2 wk
- **Starts after:** Submersa 12; F9.10, F10.01, F10.02

Capture the Submersa section of TRAILER.md (shots 14a-14g) in PCSX-Redux at whole-number scale with no filtering, plus PC and Android footage of the same room for the three-way split, and label every clip with its target and source. Then package: BIN/CUE with the memory card icon, Windows zip, Linux AppImage, Android APK, with engine and SDK license files and a `THIRD_PARTY.md` that credits every third-party asset. No BIOS and no Sony marks. Run the whole END_RESULTS checklist for each of the four cells and tick the Submersa row.

**Topics**

- **trailer capture plan.** Plan the capture of the Submersa section (shots 14a-14g) of a 3-minute trailer for a Rensga Engine PSX game: emulator settings, 60 fps recording, whole-number scaling, labelling each clip with its source. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **three-way split shot.** Explain how to record the same room on PCSX-Redux, a PC build and an Android build of a Rensga Engine game from the same bot input so the three-way split shot lines up. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **release package checks.** List the checks for packaging a PSX BIN/CUE game made with Rensga Engine, plus its Windows, Linux and Android builds: license files, THIRD_PARTY.md, no BIOS, no Sony marks, memory card icon. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **credits and attribution.** Explain how to write THIRD_PARTY.md and the in-game credits for a Rensga Engine game with original art and some CC-BY audio, naming each license. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

**Exercises**

- **clip log.** Give me an exercise: build a table that maps every trailer clip for a PSX game to its source, target tag and file name, and check no clip is missing a label. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.
- **end results check.** Guide me through an exercise: go through ten release checks for a Rensga Engine game on one build and record the evidence (log lines, hashes, screenshots) for each. Context: Submersa is my 40-60 minute PSX survival horror demo (fixed cameras, 320x240, 20.12 fixed-point, 2 MB RAM, 1 MB VRAM), made with Rensga Engine, an open-source Unity-style C++17 engine and editor.

## Risks

| Risk | Likelihood | Mitigation |
| --- | --- | --- |
| 2 MB RAM overflows when two area packs overlap | Medium | Measure in Submersa 0 and again in 6; keep packs small; unload the old area before the new one finishes; the validator names the pack. |
| XA music starves while an area streams from the CD | Medium | Plan the read schedule in Submersa 9; log underruns; shorten area packs or pause the music for one beat. |
| Ordering table sorting glitches in fixed-camera rooms | High | Sort bias on props and characters; test every room in the PSX preview mode; fix by splitting meshes, not by hiding. |
| VRAM pages fill up with area textures | Medium | Four shared tile textures and CLUT sharing groups; check the VRAM map after each area. |
| Fixed-point differs between PSX and PC builds | Medium | Compare hashes from Submersa 0 on; periodic hash log; no `float` in game code (compile error). |
| Art workload: about 25 asset groups, one owner | High | Reuse the tile kit and props; cut rooms before cutting quality; keep the Asset register current; the demo length may drop toward 40 minutes. |
| Engine F9 tasks arrive late | Medium | Start from the F9.12 template objects and primitives; do not build final content until F9.08 and F9.13 are ticked. |
| PCSX-Redux differs from real hardware | Low | Real-hardware pass is tracked separately (F9.G5); keep margin on the 30 fps and RAM numbers. |
| Memory card emulation or OpenBIOS gaps | Medium | Test a fresh card file per run; keep the save code behind the F9.24 API; see Q5 in TASKLIST.md. |
| Soft lock in the finale or a puzzle chain | Medium | Key-dependency validator; load every save in the finale test; bot run in Submersa 12. |
