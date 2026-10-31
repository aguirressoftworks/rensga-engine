# Poeira roadmap

Arcade off-road racing game for the PSP, built with Rensga Engine. This page is a Markdown version of an engine roadmap page, written for a game project instead of engine work. Phases are "Poeira <number>", numbered from 0, and the engine tasks they wait for are named by their TASKLIST.md IDs (for example F6.08). The matching work queue with weights is [POEIRA_TASKLIST.md](POEIRA_TASKLIST.md).

Sources: [DEMO_GAMES.md](DEMO_GAMES.md) (pitch, gameplay, assets), [END_RESULTS.md](END_RESULTS.md) (the result row and the 10 checks), [MODELLING.md](MODELLING.md) (PSP model limits), [TRAILER.md](TRAILER.md) (shots 15a-15h).

## Summary

Poeira is a 60-minute racer: 3 cups of 4 races, Time Trial with a saved ghost, and Quick Race, on 4 tracks (each also in reverse at night) with 6 fictional cars and 6 rivals. It runs on the PSP at 480x272 and 30 fps inside the 32 MB RAM and 2 MB VRAM budgets, and the same project builds for Windows, Linux and Android. The car is a script that uses raycasts against the track collision mesh, the rivals follow spline racing lines, and engine sound is looped samples crossfaded by RPM. The roadmap goes vertical slice first, then tools and data, driving, content, menus and saves, audio, the story, the builds, and last the bot run, trailer footage and packaging that close the PSP row of END_RESULTS.md.

## Stats

| | |
| --- | --- |
| total | ~33.5 weeks |
| phases | 14 |
| starts after | F5 (PSP track), F6 (full editor and API), F10.01 (packaging) |
| optional | +2 wk ad hoc multiplayer (needs OPT-1) |

Weeks are human part-time effort at about 15 hours a week and are used only as relative weights, not as a schedule.

## Legend

- **Group 1**: pre-production and content
- **Group 2**: gameplay systems
- **Group 3**: polish and ship

## Phases

| # | Group | Phase | Estimate | Starts after |
| --- | --- | --- | --- | --- |
| 0 | 1 | Vertical slice: one track, one car, budgets proven | 2.5 wk | F5.01, F5.06, F5.07, F5.09, F6.02 |
| 1 | 1 | Project, data assets and tools: CarStats, TrackData, CupData, spline tools, asset list | 2 wk | Poeira 0, F6.07 |
| 2 | 2 | Arcade car physics: raycast suspension, grip, drift, boost, jumps | 3 wk | Poeira 1, F6.08 |
| 3 | 2 | Camera, race rules and replay: chase camera, laps, checkpoints, positions, respawn | 2 wk | Poeira 2, F6.09 |
| 4 | 2 | AI rivals: spline racing lines, brake points, rubber banding | 2.5 wk | Poeira 3 |
| 5 | 1 | Cars: six cars, wheels, three paints, low-detail versions | 2 wk | Poeira 4 |
| 6 | 1 | Tracks 1 and 2: Estrada do Buriti and Serra Dourada | 2.5 wk | Poeira 5 |
| 7 | 1 | Tracks 3 and 4: Canavial and Chapada Velha, reverse versions | 2.5 wk | Poeira 6 |
| 8 | 2 | Effects, lighting and weather: particles, night variants, headlights, rain | 2 wk | Poeira 7, F6.09 |
| 9 | 2 | UI, modes and saves: HUD, garage, cup map, time trial ghost, memory stick | 3 wk | Poeira 8, F6.04, F6.12, F6.11, F5.15 |
| 10 | 2 | Audio: RPM-crossfaded engines, effects, streamed music | 2 wk | Poeira 5, F6.10, F5.17 |
| 11 | 2 | Rivals, story and final cup: story stills, rival personalities, cups, unlocks, credits | 2 wk | Poeira 9 |
| 12 | 3 | Builds, input and performance pass: PC and Android builds, pad and touch, budget pass | 3 wk | Poeira 11, F5.02, F5.16, F6.05, F6.06 |
| 13 | 3 | Bot run, trailer footage and packaging: hash match, trailer shots, END_RESULTS row | 2.5 wk | Poeira 12, F10.01 |

### Poeira 0 · Vertical slice

*one track, one car, budgets proven*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** F5.01, F5.06, F5.07, F5.09, F6.02

One short loop of Estrada do Buriti, the rebuilt hatchback with a rough raycast drive, two placeholder rivals and a speed readout. The art is rough; the point is to measure. The slice runs in PPSSPP at 30 fps and is checked in the editor's PSP preview against the ConsoleTarget budgets: 20,000 triangles per frame, 2 MB VRAM, 32 MB RAM. It uses the GU 3D backend, the PSP import settings (swizzled CLUT8 textures) and MeshRenderer. Limits from MODELLING.md apply from day one: player car 1,500-2,000 triangles with one 256x256 CLUT8 atlas, road subdivided into a grid so polygons never cross the near plane. Everything found here is written into the project README as a budget table, and later phases are sized from it.

**Topics**

- **slice scope**
  > Define the smallest vertical slice in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: one looping track section, one car, two rivals and a speed readout. List what must be real (budgets, 30 fps, swizzled CLUT8 textures) and what may stay rough.
- **gridded road mesh**
  > Explain how to model a dirt road in Blender for Poeira, a PSP racing game made with Rensga Engine, so the GU near-plane clipping does not glitch: grid size, triangle count per metre, and how to export a .glb that the Rensga cooker accepts.
- **budget table**
  > Design a budget table in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine that splits 20,000 triangles per frame, 2 MB VRAM and 32 MB RAM between the track, scenery, up to seven cars, particles and the HUD.
- **CLUT8 texture workflow**
  > Walk me through making a 256x256 CLUT8 car atlas and a tiling dirt texture for Poeira in Rensga Engine on PSP: palette limits, power-of-two rule, swizzling at cook time and checking the VRAM map in the editor.

**Exercises**

- **build the slice**
  > Guide me through an exercise: build the vertical slice in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: a looping road section, a 1,500-triangle placeholder car with a 256x256 CLUT8 atlas, and a camera that follows it, then read the editor's PSP budget panel and the rensga: prof log to confirm 30 fps in PPSSPP.

### Poeira 1 · Project, data assets and tools

*CarStats, TrackData, CupData, spline tools, asset list*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2 wk
- **Starts after:** Poeira 0, F6.07

The data model that every later phase reads. Cars, tracks, cups and rivals become data assets with reflected fields, so the inspector, serialization and the cooker use one description: CarStats (speed, grip, drift, weight, three paint palettes), TrackData (mesh references, spline GUIDs, laps, sky, fog, night variant), CupData (races, points, unlocks) and RivalData (skill, car, line offset). A small editor tool draws the racing line, AI brake points, checkpoints and respawn points as gizmos in the Scene view and snaps them to the road. The project README gets the asset list from the assets table in DEMO_GAMES.md, with a status per asset, and THIRD_PARTY.md starts empty. Rule: game scripts never branch on a console.

**Topics**

- **data asset layout**
  > Design the data assets in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: CarStats, TrackData, CupData and RivalData, using Rensga reflection macros (SERIALIZE_FIELD, RANGE, HEADER) so the inspector and the cooker read one description. Give field lists and GUID references.
- **spline gizmo tool**
  > Implement a Rensga Engine editor tool (custom inspector with Gizmos) for editing a racing-line spline, AI brake points, checkpoints and respawn points on a road mesh, for Poeira on PSP, with snapping to the road and undo support.
- **asset tracking**
  > Explain how to track every art, audio and data asset of Poeira in a project README for a Rensga Engine PSP game: one row per asset with budget, owner and status, and how to keep it in step with the cooker's budget errors.
- **licensing rules**
  > Explain the asset licensing rules for a Rensga Engine game project (CC0, CC-BY credited in THIRD_PARTY.md, no ripped assets, no Sony marks) and how they apply to the models, textures, sounds and music of Poeira, a PSP racing game.

**Exercises**

- **first data assets**
  > Give me an exercise: create CarStats and TrackData data assets for the hatchback and Estrada do Buriti in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, edit them in the inspector, and check that undo, redo and a cook round-trip keep every field.

### Poeira 2 · Arcade car physics

*raycast suspension, grip, drift, boost, jumps*

- **Group:** 2 (gameplay systems)
- **Estimate:** 3 wk
- **Starts after:** Poeira 1, F6.08

One CarController script, used by the player and by the AI. Four raycasts per car against the track collision mesh give suspension compression, ground normal and surface type (dirt, grass, water). Forces: a throttle curve, a steering angle that shrinks with speed, lateral grip from CarStats, and a drift state entered with a brake tap that lowers grip, adds yaw and fills the boost bar. Boost raises top speed for a short time. In the air the car keeps its pitch and falls with a fixed gravity; landing is soft. Wall hits slide the car along the wall and cost some speed. No rigid-body vehicle is used. Everything runs in FixedUpdate at 60 Hz with the engine's Mathf and a seeded RNG, so the result is the same on PSP, Windows, Linux and Android. Budget: 28 raycasts per fixed step for seven cars. Wheels spin and steer from code.

**Topics**

- **raycast suspension**
  > Design a four-raycast spring and damper suspension in a Rensga Engine C++ script in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, using Physics.Raycast against a track collision mesh, and explain how to derive ground normal, compression and surface type per wheel.
- **drift and boost**
  > Design arcade drifting in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android: a brake tap enters a drift state, grip drops, yaw assist applies, the boost bar fills with drift time, and boost raises top speed. Give states, transitions and tuning values.
- **deterministic physics**
  > Explain how to keep a Rensga Engine car physics script deterministic across PSP, Windows, Linux and Android in Poeira: fixed 60 Hz FixedUpdate, Mathf only, seeded RNG, no frame-time dependence, and how the engine's state hash check catches a drift.
- **jumps and collisions**
  > Explain how to handle jumps off ramps and wall collisions for an arcade car script in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: airborne state, landing, sliding along walls and per-frame raycast cost.

**Exercises**

- **tune the hatchback**
  > Guide me through an exercise: implement the CarController for the hatchback in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, then tune throttle, grip and drift on the slice track until a clean lap is possible, and show me which CarStats fields I changed.

### Poeira 3 · Camera, race rules and replay

*chase camera, laps, checkpoints, positions, respawn*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Poeira 2, F6.09

Race flow as a small data-driven state machine: grid, countdown, racing, finish, results. Checkpoints count laps and block shortcuts; positions come from lap, checkpoint and distance along the racing line; a wrong-way check and a respawn at the last respawn point (after a fall, or after the car is stuck for 3 seconds) keep the race moving. The chase camera follows with speed-based distance and field of view, and has a look-behind and a bumper view. After a big jump the replay camera shows a side angle in slow motion. The countdown, start grid and finish line are prefabs. The camera and race state use only the public API, so they build for every target.

**Topics**

- **race state machine**
  > Design the race state machine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: grid, countdown, racing, finish, results. Show states, events, and which data assets feed it.
- **checkpoints and positions**
  > Explain how to compute race positions, lap counts, wrong-way detection and anti-shortcut checkpoints from a racing-line spline for Poeira, a racing game made with Rensga Engine for the PSP, using only integer-friendly maths that is the same on every platform.
- **chase camera**
  > Design a chase camera for an arcade racer in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: speed-based distance and field of view, smoothing, look-behind, bumper view and avoiding clipping into walls.
- **slow-motion replay camera**
  > Explain how to build a jump replay camera with slow motion in Poeira, a Rensga Engine game for the PSP (Time.timeScale, a side-angle camera, return to the chase camera) without breaking the fixed timestep or determinism.

**Exercises**

- **three-lap race**
  > Give me an exercise: wire a three-lap race with a countdown, checkpoints, a wrong-way warning and a respawn point in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, then test a car that cuts a corner and one that falls off the road.

### Poeira 4 · AI rivals

*spline racing lines, brake points, rubber banding*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2.5 wk
- **Starts after:** Poeira 3

The AI drives the same CarController through a virtual input: steer, throttle, brake and drift. It aims at a point ahead on the racing-line spline, slows for brake points, and drifts in marked corners. Each RivalData sets skill (speed target, mistake rate from a seeded RNG, line offset). Rubber banding gives a small speed change by distance to the player, capped so a good player can still win and a poor one is not helped for free. Cars avoid overlap by shifting the line offset. A stuck car respawns. The same virtual input is the base of the bot that plays the whole game in Poeira 13. The AI cost is measured per car; the budget is under 0.3 ms per car on the PSP profile.

**Topics**

- **spline following**
  > Design AI spline following in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android: look-ahead point on the racing line, steering from the angle error, brake points, and sharing the CarController with the player through a virtual input.
- **rubber banding**
  > Explain how to tune rubber banding in Poeira, an arcade racer made with Rensga Engine for the PSP, so it stays fair: speed scale by distance to the player, caps, per-rival skill and a seeded RNG for mistakes.
- **overtaking and avoidance**
  > Design simple overtaking and car avoidance for AI racers in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, using line offsets on the spline and no heavy physics queries.
- **AI cost budget**
  > Explain how to measure and budget the CPU cost of six AI racers in Poeira, a Rensga Engine game on the PSP, using the rensga: prof log, and how to cut cost with update rates or distance LOD.

**Exercises**

- **six-rival race**
  > Guide me through an exercise: add six AI rivals with different skills to the slice track in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, run 10 races in a headless sim, and report finish order, lap times and the average AI cost per car.

### Poeira 5 · Cars

*six cars, wheels, three paints, low-detail versions*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2 wk
- **Starts after:** Poeira 4

The six fictional cars: the rebuilt hatchback, a pickup, a light buggy, a rally sedan, a vintage coupe and the rancher's truck. Each has a body, four separate wheels, a design without damage, three paint jobs as palette swaps and a 600-triangle low-detail version for distant cars. Budgets from MODELLING.md and DEMO_GAMES.md: player car 1,500-2,000 triangles, rivals 1,000-1,500, one 256x256 CLUT8 atlas per car, at most 2 materials, vertex colour for baked shading, origin at the car's centre on the ground. CarStats are balanced so every car has a role (speed, grip, drift, weight). A garage scene shows each car with its paints under the 4 hardware lights. No real brands.

**Topics**

- **car modelling rules**
  > Give me a Blender modelling checklist for the six cars in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: triangle counts, wheels as separate objects, origin and scale, vertex colour baking, one 256x256 CLUT8 atlas, glTF 2.0 export settings for Rensga Engine.
- **paint palettes**
  > Design three paint jobs per car as palette swaps for CLUT8 textures in Poeira, a Rensga Engine game on the PSP: how to author one atlas with swappable palettes, how the cooker stores them and how a script picks one at runtime.
- **car balance**
  > Design the CarStats balance for six cars in in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android (hatchback, pickup, buggy, rally sedan, vintage coupe, rancher's truck): speed, grip, drift and weight, with a table and a rule that no car is best on every track.
- **level of detail for cars**
  > Explain how to set up a 600-triangle low-detail car mesh and switch to it by distance in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, so seven cars fit in the 20,000-triangle frame budget.

**Exercises**

- **first finished car**
  > Guide me through an exercise: model, texture and import the pickup truck in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine with separate wheels, three paints and a low-detail version, then check its triangle count and VRAM cost in the editor.

### Poeira 6 · Tracks 1 and 2

*Estrada do Buriti and Serra Dourada*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** Poeira 5

Estrada do Buriti is palm wetlands with water splashes; Serra Dourada is mountain switchbacks with cliffs. Each track is a gridded road mesh, a separate collision mesh, scenery props in LOD groups, billboards for distant trees, a sky dome, fog, spline data and a prefab that holds it all. Scenery is split into sectors so only the sectors near the car draw, which keeps the frame under 20,000 triangles. Textures are CLUT8 tiling sets (dirt, grass, rock, water) sharing palettes, so the track fits the VRAM budget. Water zones and cliff walls carry surface types read by CarController. A track is not done until it passes the cook with no override and holds 30 fps with seven cars at its busiest point.

**Topics**

- **sector-based scenery**
  > Design a sector and LOD scheme for track scenery in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: sector size, which sectors draw, billboards for distant trees, and how this keeps a track under 20,000 triangles per frame.
- **collision meshes**
  > Explain how to build a simple track collision mesh separate from the render mesh for raycast car physics in Poeira, a Rensga Engine game on the PSP: triangle budget, surface type tagging, walls and cliff edges.
- **shared palettes**
  > Explain how to lay out CLUT8 tiling textures (dirt, grass, rock, water, palm) for two Poeira tracks in Rensga Engine so they share palettes and fit 2 MB of PSP VRAM, using the editor's VRAM map.
- **sky dome and fog**
  > Design a sky dome and distance fog for Estrada do Buriti and Serra Dourada in Poeira, a Rensga Engine game on the PSP: dome triangle count, vertex-colour gradient, fog range and how fog hides pop-in.

**Exercises**

- **build Estrada do Buriti**
  > Guide me through an exercise: build Estrada do Buriti in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: gridded road, collision mesh, palm LOD groups, billboards, sky dome and splines, then measure the worst frame with seven cars.

### Poeira 7 · Tracks 3 and 4

*Canavial and Chapada Velha, reverse versions*

- **Group:** 1 (pre-production and content)
- **Estimate:** 2.5 wk
- **Starts after:** Poeira 6

Canavial is sugar cane fields: narrow corridors, dust clouds, and cane that must be cheap (billboard cards and a few low meshes). Chapada Velha is a rock plateau with big jumps and a final sunset run; its ramps need collision that gives a clean launch. This phase adds the reverse direction for all four tracks: the spline tool reverses racing line, brake points and checkpoints, and a designer corrects what does not work in reverse (blind jumps, one-way ramps). Banners, crowd sprites, checkpoint arches and a start-line flag waver complete the scenery.

**Topics**

- **cheap sugar cane**
  > Explain how to draw dense sugar cane corridors cheaply in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: billboard cards, alpha test cost on the GU, LOD distance and keeping dust sprites from overdrawing.
- **jumps and ramps**
  > Design big jumps for Chapada Velha in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android: ramp collision shape, launch speed ranges, landing zones, and how the replay camera frames them.
- **reverse tracks**
  > Explain how to support reverse versions of a racing track in Poeira, a Rensga Engine racing game for the PSP: reversing the racing-line spline, brake points, checkpoints and respawns with the spline tool, and fixing blind jumps and one-way ramps by hand.
- **crowds and banners**
  > Design cheap crowd sprites, banners and checkpoint arches for a Poeira track on PSP in Rensga Engine: sprite sheets with small CLUT4 textures, loop animation by UV and their triangle and fill-rate cost.

**Exercises**

- **jump and reverse test**
  > Give me an exercise: build one big ramp on Chapada Velha in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, tune it so every car lands inside the landing zone, then reverse the track and fix any broken checkpoint or blind jump.

### Poeira 8 · Effects, lighting and weather

*particles, night variants, headlights, rain*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Poeira 7, F6.09

CPU particles from pools with a fixed cap (about 300 live particles on the PSP profile): dust trail, water splash, boost flame, drift smoke, wall sparks and rain on one night race. Sprites are small CLUT4 textures. Each track gets a night lighting variant that is a second vertex-colour set plus its own fog and sky colour, selected by TrackData; headlights are 2 hardware lights on the cars (the GU allows 4 lights per object, so the 4 most important lights are picked). The sunset run on Chapada Velha is a third lighting set. Particle emitters are prefabs; none allocates at runtime.

**Topics**

- **particle pools**
  > Design CPU particle pools for dust, splash, boost flame, drift smoke, sparks and rain in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: fixed caps, per-effect cost, sprite sheet sizes and how to avoid any per-frame allocation.
- **night variants**
  > Explain how to author a night lighting variant of a Poeira track in Rensga Engine on the PSP with a second vertex-colour set, fog and sky colours chosen by TrackData, without duplicating the meshes.
- **headlights with 4 lights**
  > Design headlights for Poeira cars at night using the PSP GU's 4 hardware lights per object in Rensga Engine: which lights are picked, light ranges, and how the other cars and the track are lit.
- **fill-rate budget**
  > Explain how to measure and limit overdraw from particles and alpha sprites on the PSP in a Rensga Engine game, using the profiler and a worst-case frame in Poeira.

**Exercises**

- **dust and boost**
  > Guide me through an exercise: add a dust trail, drift smoke and boost flame to the hatchback in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, cap them at 300 live particles in total and verify 30 fps with seven cars in PPSSPP.

### Poeira 9 · UI, modes and saves

*HUD, garage, cup map, time trial ghost, memory stick*

- **Group:** 2 (gameplay systems)
- **Estimate:** 3 wk
- **Starts after:** Poeira 8, F6.04, F6.12, F6.11, F5.15

All screens are canvas UI with navigation by the pad: title, garage (car select and paint), cup map, HUD (position, lap, speed, boost bar, mini map), results and podium. Modes: Cup (Bronze, Silver, Gold, 4 races each, points per race), Time Trial with a ghost, Quick Race. The ghost records the car's position and rotation every 6 fixed steps in a fixed buffer and is saved to the memory stick; a loaded ghost plays back as a translucent car. Progress (cups won, unlocked cars, paints, night tracks, best times) is saved through the system save dialog with ICON0 and PIC1; PC and Android write to the save folder. One bitmap font and a large number font, CLUT8 UI atlases. UI uses the API that works on all targets.

**Topics**

- **HUD design**
  > Design the Poeira HUD in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: position, lap, speed, boost bar and mini map in 480x272, a bitmap font and a large number font, atlas size in CLUT8 and update cost.
- **time trial ghost**
  > Design the ghost for Time Trial in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: what to record and how often, a fixed buffer size, file format, saving to the memory stick through the save API, and playback as a translucent car.
- **save data and icons**
  > Explain how to save progress for a Rensga Engine PSP game through the system save dialog with ICON0 and PIC1, how the same save data is written to a save folder on Windows, Linux and Android, and how to version the save format in Poeira.
- **menu navigation**
  > Design pad-driven menu navigation for the Poeira garage, cup map and results screens in Poeira, a Rensga Engine game for the PSP, Windows, Linux and Android: focus, back, repeat on hold, and touch on Android.

**Exercises**

- **cup and ghost**
  > Guide me through an exercise: build the cup map and a Time Trial mode with a saved ghost in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, then save, quit, reload and race against my ghost, checking the ghost file size.

### Poeira 10 · Audio

*RPM-crossfaded engines, effects, streamed music*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Poeira 5, F6.10, F5.17

Engine sound from three looped samples per car (low, mid and high RPM bands) crossfaded by RPM and throttle, with pitch scaled inside each band. The 16-voice mixer is budgeted: the player engine uses 2 voices, up to 3 nearest rival engines 1 voice each, the rest go to effects (tyre skid, gravel, impacts, boost, countdown beeps, crowd). Priorities decide what is dropped. Music is streamed from the memory stick: title theme, 4 track themes and a results jingle, with half-time on the jump replay. Samples are short mono at a low sample rate to fit the 32 MB RAM budget. Audio is data: AudioSource and mixer settings, no code per console.

**Topics**

- **RPM crossfade**
  > Design engine sound for Poeira cars in Rensga Engine in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine: 3 looped samples per car crossfaded by RPM band, pitch scaling, throttle layer and loop-point rules for clean loops.
- **16-voice budget**
  > Explain how to budget the PSP's 16-voice mixer in Poeira with Rensga Engine: voices for the player engine, nearest rivals, tyre skid, gravel, impacts, boost, countdown and crowd, plus priorities and voice stealing.
- **streamed music**
  > Explain how to stream music from the memory stick in a Rensga Engine PSP game: file format, buffer size, how a track change is handled and what the 32 MB RAM budget allows in Poeira.
- **sample sizes**
  > Design the sample list in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android (6 cars x 3 RPM loops, skid, gravel, impacts, boost, countdown beeps, crowd): sample rate, mono, loop length and total RAM used.

**Exercises**

- **engine loop test**
  > Give me an exercise: record or synthesize three RPM loops for one car in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, set up the crossfade, and check in PPSSPP that there are no clicks at loop points and that voices stay at or below 16.

### Poeira 11 · Rivals, story and final cup

*story stills, rival personalities, cups, unlocks, credits*

- **Group:** 2 (gameplay systems)
- **Estimate:** 2 wk
- **Starts after:** Poeira 9

The story told with stills and short text: the garage and the old car, the six rivals (from the rancher's son in the new truck to the retired champion), the old champion's advice, and the ending when Lia wins or loses the final cup. Rival data sets personality: line style, drift use and a short line of text before each race. Cups carry points and unlock cars, paints and the night tracks. The final cup is Gold: four races ending with Chapada Velha at sunset, in reverse for the last race, against the retired champion. After the podium come the credits and a return to the title. Portraits are 2D art only (256x256 CLUT8 stills, 64x64 HUD icons).

**Topics**

- **story stills**
  > Design the story scenes in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android as stills with text: garage, rivals, the old champion and the ending. List the scenes, their text length, and the 256x256 CLUT8 still budget.
- **cup progression**
  > Design cup progression for Poeira, a Rensga Engine PSP racing game: Bronze, Silver and Gold with 4 races each, a points table, unlocks for cars, paints and night tracks, and how CupData stores this as data.
- **final race design**
  > Design the final race of the Gold cup in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android: Chapada Velha at sunset, the retired champion's AI settings, rubber banding limits and a fair difficulty curve.
- **credits and THIRD_PARTY**
  > Explain how to build a credits screen for a Rensga Engine game that lists the engine, SDKs, music, models and third-party assets, taken from THIRD_PARTY.md, for Poeira, a Rensga Engine PSP racing game.

**Exercises**

- **play the whole game**
  > Guide me through an exercise: play the three cups in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine start to credits with an AI-driven player, and list every unlock, save and screen that must appear.

### Poeira 12 · Builds, input and performance pass

*PC and Android builds, pad and touch, budget pass*

- **Group:** 3 (polish and ship)
- **Estimate:** 3 wk
- **Starts after:** Poeira 11, F5.02, F5.16, F6.05, F6.06

The same project built for Windows (.zip), Linux (AppImage) and Android (APK) with `rensga build`. Input actions map the PSP pad, a gamepad and, on Android, a touch layout: left thumb steer, right thumb throttle, brake and drift buttons, boost; a gamepad also works on Android. Then the pass that makes it fit: measure the worst frame of every track and direction (triangles, draw calls, VRAM, RAM, voices, CPU per system) with the profiler and the `rensga: prof` log, and fix with sector sizes, LOD distances, texture palettes, particle caps and AI update rates. If seven racers do not fit, the grid is cut to six; the decision and its numbers go in the README. VFPU use for the car maths is checked on the PSP build.

**Topics**

- **input actions**
  > Design input actions in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android: steer, throttle, brake and drift, boost, pause, with a PSP pad map, a gamepad map and an Android touch layout, as Rensga input action assets.
- **Android touch controls**
  > Explain how to design touch controls for an arcade racer like Poeira in Rensga Engine on Android (the same project also builds for the PSP): steering, throttle, brake and drift buttons, size, dead zones, and keeping the game deterministic with touch input.
- **worst-frame measurement**
  > Explain how to find and measure the worst frame of each track in Poeira with Rensga Engine's profiler and the rensga: prof log on the PSP: a camera path, seven cars, all effects, and a table of triangles, draw calls, VRAM, RAM and CPU.
- **fixing the budget**
  > Give me a plan to cut a PSP racing game called Poeira that is over budget on the PSP (30 fps, 20,000 triangles, 2 MB VRAM, 32 MB RAM) in Rensga Engine: ordered fixes from cheapest to most costly, and when to go from seven to six racers.

**Exercises**

- **budget pass**
  > Guide me through an exercise: run the worst-frame camera path on every track in Poeira, an arcade off-road racing game for the PSP (480x272, 32 MB RAM, 2 MB VRAM) built with Rensga Engine, a Unity-style C++17 game engine, fill a budget table, fix the worst offender and measure again.

### Poeira 13 · Bot run, trailer footage and packaging

*hash match, trailer shots, END_RESULTS row*

- **Group:** 3 (polish and ship)
- **Estimate:** 2.5 wk
- **Starts after:** Poeira 12, F10.01

A scripted bot, built on the AI's virtual input, plays the whole game: Bronze, Silver and Gold, a time trial that saves a ghost, a save and reload, the credits. It runs on the PSP build in PPSSPP and on the Windows, Linux and Android builds, and the final state hash must be the same everywhere (the same check as tools/ci/determinism.sh). Then the trailer footage listed for Poeira in TRAILER.md, shots 15a to 15h: title card, the garage cover pull, the countdown with six cars, a drift chain on Serra Dourada, the Chapada Velha slow-motion jump, the night cane race, the ghost race (or four-way ad hoc if it exists) and the Android touch plus PC gamepad split, all at native resolution, 1080p60, labelled by source. Last, packaging: licenses, THIRD_PARTY.md and the four cells of the Poeira row in END_RESULTS.md.

**Topics**

- **bot run**
  > Design a scripted bot that plays the whole game in Poeira, an arcade off-road racing game built with Rensga Engine, a Unity-style C++17 game engine, for the PSP, Windows, Linux and Android from title to credits using the AI's virtual input, and prints a final state hash and the line game complete.
- **determinism check**
  > Explain how to compare a final state hash between a PSP build in PPSSPP and Windows, Linux and Android builds of Poeira, a Rensga Engine PSP racing game, and how to find the first frame where they differ using per-frame traces.
- **trailer capture**
  > Plan the footage capture for the Poeira part of a Rensga Engine trailer, showing Poeira running on the PSP: eight shots (title card, garage, countdown, drift chain, slow-motion jump, night race, ghost race, Android and PC split), emulator settings, 60 fps recording and a source label for every clip.
- **legal package**
  > List what the release package of Poeira must contain for each of PSP, Windows, Linux and Android in Rensga Engine: engine and SDK licenses, THIRD_PARTY.md credits, no BIOS, no Sony marks, and a check list for each.

**Exercises**

- **END_RESULTS row**
  > Give me an exercise: tick the Poeira row of the result matrix for Rensga Engine, checking the ten checks (one project, built by the engine, fits, boots and finishes, same game everywhere, no per-frame allocation, frame rate, saves, input, legal package) for the PSP build in PPSSPP and for Windows, Linux and Android.

## Risks

| Risk | What we do |
| --- | --- |
| Seven cars plus a track do not fit 20,000 triangles at 30 fps | Low-detail cars (600 triangles), sector scenery and distance fog from Poeira 0; the grid drops to six racers in Poeira 12 if the numbers say so |
| Float physics differs between PSP and PC, so the state hash does not match | Mathf and fixed timestep only, a seeded RNG, the determinism trace from Poeira 2 on, and a hash check in every milestone |
| Raycast car feels floaty or twitchy | Tune on the slice first (Poeira 0 and 2); keep every value in CarStats so tuning needs no code |
| Near-plane clipping glitches on large road polygons | Grid the road from the first mesh; MODELLING.md rule 6; check in the PSP preview |
| 2 MB VRAM overflows with four tracks of textures | CLUT8 tiling sets with shared palettes; only the active track's textures are resident; check the VRAM map each track |
| An engine milestone (F5, F6) is late | The slice and the data work (Poeira 0 and 1) wait for F5.01 and F6.02 only; art phases can go on in parallel; phase starts name the exact engine tasks |
| Art workload is large (6 cars, 4 tracks, props, UI) | Reuse scenery between tracks, palette-swap paints, procedural wheels and flag; asset register tracks every item |
| Ad hoc multiplayer needs OPT-1 | It is not counted; the trailer uses a ghost race instead (TRAILER.md shot 15g) |
