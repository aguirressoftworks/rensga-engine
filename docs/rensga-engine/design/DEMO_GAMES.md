# Demo games

One showcase game per console, each in the genre that console is best remembered for. They are made for videos: short, complete, and built only from engine features on the roadmap. All three are original: new names, characters, places and art, set in the Brazilian cerrado like the engine's pequi icon. They use other games' genres, not their content (see RULES.md §5).

Each game also builds for Windows, Linux and Android, which the videos can show side by side.

Model limits come from [MODELLING.md](MODELLING.md). The engine milestone each demo needs is listed with it.

| Console | Genre | Demo | Needs |
| --- | --- | --- | --- |
| PSX | Survival horror | **Submersa** | F9 (PSX track) |
| PSP | Arcade racing | **Poeira** | F5 (PSP track) + F6 (physics raycasts, UI) |
| PS2 | 3D mascot action platformer | **Pequi** | F8 (PS2 track) + F6 |

## Why these genres

- **PSX: survival horror.** Retrospectives credit the PlayStation with popularizing the genre and giving it its name. JRPGs are the other strong candidate. Horror fits the hardware: fixed cameras hide the draw distance, fog hides pop-in, and small rooms fit in 2 MB.
- **PSP: arcade racing.** Racing games were the PSP's showcase from launch: reviewers called its launch racers the quintessential launch games and some of the best-looking games on the system. Races of 2–4 minutes suit a handheld, and a racer is simpler to build than a game full of animated characters.
- **PS2: 3D mascot platformer.** The early-2000s mascot platformer, often mixed with combat, is the genre most people link with the PS2. Big colourful worlds use the PS2's polygon budget and fog well.

---

## PSX · Submersa

**Genre:** survival horror, fixed camera angles. **Length:** 40–60 minute demo. **Resolution:** 320x240, fixed-point.

### Pitch

Goiás, 1997. A drought drains the reservoir of a hydroelectric dam, and the town of Santa Luzia do Vau rises from the mud for the first time in thirty years. The power company sends Inês Costa, a young surveyor, to map the ruins before the rains return. The town was evacuated before the flooding, but the church bell still rings at night, and the mud-covered figures in the streets are wearing the clothes of the people who refused to leave. Inês has three nights to find out what the company buried under the water, and to get out before the floodgates open.

### Gameplay

- Fixed camera per room, switching at doorways. Controls are camera-relative, with a tank-control option.
- Scarce supplies: a flare gun, a revolver with few bullets, a machete. Running is often better than fighting.
- Key-and-lock exploration across three areas: the church, the school and the pumping station. Each area is streamed from disc.
- Puzzles from the survey work: line up the old water gauges, read the dam's pressure dial, match land deed numbers.
- Save at transistor radios, one memory card block per save, with an icon.
- Ends with one boss, the "Caretaker", in the dam's flooded control room.

### Engine features shown

Segmented characters driven by the Animator. Ordering-table sorting with sort bias. Depth-cue fog and 3 lights. CLUT4 textures packed into VRAM pages. Area streaming. XA music and VAG effects. Memory card saves. Fixed-point gameplay that plays the same on PC and Android.

### Video beats (2–3 minutes)

1. Title over the drained reservoir, a bell rings.
2. Inês walks the main street; the camera cuts between angles.
3. First mud figure, a chase, a door slammed shut.
4. Inventory and map screens.
5. A gauge puzzle solved.
6. Boss reveal.
7. Split screen: PSX, PC and Android running the same room.

### Assets

| Kind | Asset | Budget / notes |
| --- | --- | --- |
| Character | Inês Costa, segmented (11 pieces) | 400 triangles, 128x128 CLUT4 |
| Enemies | Mud walker, Leech swarm (one mesh, instanced), Bell ringer (tall, slow), the Caretaker (boss) | 250, 80, 350, 700 triangles; 64–128 px CLUT4 |
| NPC | Old ferryman (only survivor, gives hints) | 300 triangles |
| Areas | Main street, church (nave and bell tower), school (2 classrooms and yard), pumping station (3 rooms), dam control room | Built from grid tiles; 8–12 rooms per area; 4-bit tiled textures (mud, plaster, tile, rust) |
| Props | Doors, crates, pews, desks, water gauges, radio, boat, lamp posts, items (keys, deeds, ammo, flares, bandages) | 20–150 triangles each, vertex coloured where possible |
| Animations | Inês: idle, walk, run, turn, aim, shoot, swing, hurt, death, push, pick up. Enemies: idle, walk, attack, hurt, death (4–6 each). Boss: 8 clips | Rigid transform clips |
| UI | Title, inventory grid, item close-up, map, save radio dialog, health status (no HUD bar: Inês's pose shows health), memory card icon | 4-bit; one bitmap font |
| Audio | 3 XA tracks (title, safe room, boss), ambience loops (wind, dripping, bell), about 30 VAG effects (steps on mud and tile, door, gun, flare, enemy groans) | Fits the SPU RAM budget |
| Text | Notes and documents (6–8), short dialogue | Plain text assets |

---

## PSP · Poeira

**Genre:** arcade off-road racing, drifting and boosts. **Length:** 3 cups of 4 races plus time trial, about 60 minutes to finish. **Resolution:** 480x272.

### Pitch

Every dry season the dirt roads of Goiás host the Rally da Poeira, a race from farm to farm across the cerrado, and this is the last one: next year the roads get paved. Lia Moura, eighteen, rebuilds her late grandfather's old rally car in the family garage in Vila Pequi. If she wins the final cup, the prize money keeps the garage open. Six rivals stand in the way, from the rich rancher's son in a brand-new truck to the retired champion who taught her grandfather to drift.

### Gameplay

- **Handling:** arcade, easy to learn. Drift with the brake tap, and drifting fills the boost bar. Boost on straights and jumps.
- **Tracks:** 4 tracks, each also raced in reverse at night with headlights:
  - Estrada do Buriti: palm wetlands, water splashes.
  - Serra Dourada: mountain switchbacks, cliffs.
  - Canavial: sugar cane fields, narrow corridors, dust clouds.
  - Chapada Velha: rock plateau, big jumps, a final sunset run.
- **Cars:** 6 fictional cars (no real brands): the rebuilt hatchback, a pickup, a light buggy, a rally sedan, a vintage coupe, the rancher's truck. Each has speed, grip, drift and weight stats.
- **Modes:** Cup (Bronze, Silver, Gold, 4 races each, points per race), Time Trial with a ghost saved to the memory stick, Quick Race, optional 2–4 player ad hoc (PSP API, optional phase).
- **Story:** short scenes between cups, drawn as stills with text: garage, rivals, the old champion.
- **Progression:** win cups to unlock cars, paint jobs and the night tracks.

### Engine features shown

- **GU 3D at a steady 30 fps on the PSP-1000 budget:** level of detail on track scenery, fog and a sky dome.
- **GU T&L:** 4 hardware lights with headlights at night, swizzled CLUT8 textures.
- **Arcade car physics** from a script using raycasts against the track collision mesh (no rigid-body vehicle needed).
- **AI rivals** following spline racing lines with rubber banding.
- **CPU particles** for dust and water.
- **Engine sound** from looped samples crossfaded by RPM band in the 16-voice mixer; music streamed from the memory stick.
- **Ghost and saves** through the system save dialog.

### Video beats (2–3 minutes)

1. Lia pulls the cover off the old car in the garage.
2. Start-line countdown.
3. A drift chain into a boost on Serra Dourada.
4. A jump at Chapada Velha in slow motion.
5. Night race with headlights.
6. A four-way ad hoc race (PPSSPP instances).
7. Cup podium.
8. The same race on Android with touch steering and on PC with a gamepad.

### Assets

| Kind | Asset | Budget / notes |
| --- | --- | --- |
| Cars | 6 cars, each with a body, 4 wheels (separate, spinning), a damage-free design, 3 paint jobs (palette swaps) | Player car 1,500–2,000 triangles, rivals 1,000–1,500; one 256x256 CLUT8 atlas per car; a 600-triangle low-detail version each |
| Drivers | Lia and 6 rivals as portraits only (story stills and HUD) | 2D art, 256x256 CLUT8 stills, 64x64 HUD icons |
| Tracks | 4 tracks, each a road mesh, a collision mesh, scenery props and a sky dome; a night lighting variant (vertex colour set) | Road subdivided into a grid (GU clipping); scenery in LOD groups; CLUT8 tiling textures (dirt, grass, rock, cane, water) |
| Scenery props | Buriti palms, cerrado trees, fences, farm gates, cattle (static), crowd sprites, banners, checkpoint arches, rocks, bridges | 20–500 triangles each, billboards for distant trees |
| Spline data | Racing line and AI brake points per track, checkpoints, respawn points | Data assets edited in the editor |
| Animations | Few: wheel spin and steer (code), suspension bounce (code), flag waver at the start line, crowd sprite loops | Mostly procedural |
| VFX | Dust trail, water splash, boost flame, drift smoke, sparks on walls, rain (one night race) | CPU particles, small CLUT4 sprites |
| UI | Title, garage (car select and paint), cup map, HUD (position, lap, speed, boost bar, mini map), results and podium, story stills with text, save icons (ICON0, PIC1) | CLUT8; one bitmap font plus a large number font |
| Audio | Title theme, 4 track themes, results jingle, engine loops per car (3 RPM bands), tyre skid, gravel, impacts, boost, countdown voice beeps, crowd | Music streamed from memory stick, 16-voice mixer |

---

## PS2 · Pequi

**Genre:** 3D mascot action platformer (collectathon with combat). **Length:** hub plus 3 levels and a boss, about 60–90 minutes. **Resolution:** 640x448, dual analog.

### Pitch

Deep in the cerrado, every spring is guarded by a seed spirit. When the Baron Ressequido and his clanking pump machines start draining the springs to sell the water, the rains stop coming. Only Pequi, a small, stubborn fruit spirit with a spiky seed for a heart, is left awake. Roll, jump and spin through the wetlands, the cliffs and the Baron's canyon factory, free the seven captured rains, and bring the wet season back before the cerrado dries for good.

### Gameplay

- **Moves:** double jump, ground pound, spin attack with the spiky seed (breaks crates, stuns enemies), leaf glide, roll for speed, wall kick.
- **Hub:** Nascente (a dry spring valley) that turns green as rains return. Doors open with the rains collected.
- **Three levels:**
  - Vereda dos Buritis: swamp, palm platforms, rising water.
  - Chapada dos Ventos: cliffs, wind currents for gliding, moving platforms.
  - Fábrica Seca: the Baron's canyon factory, conveyors, pistons, steam.
- **Collectibles:** 7 rains (main goal, 2 per level plus 1 for the boss), 300 water drops per level, 5 hidden seed sprouts per level (unlock concept art in the menu).
- **Enemies and bosses:** pump drones, dry-thorn critters, armoured robo-armadillos. One mini boss per level. The Baron's giant pump-walker is the final boss.
- **Feel:** right-stick camera, rumble on landings and hits, a blob shadow for depth reading, progressive scan (480p).

### Engine features shown

ps2gl VU1 3D with triangle strips. Big draw distance with fog. CPU skinning. Texture streaming into 4 MB of VRAM. Many lights. Moving platforms and character physics. Dual-analog camera and DualShock 2 rumble. Memory card saves with an icon.

### Video beats (2–3 minutes)

1. Pequi wakes in the dry hub and the first rain returns: the valley turns green.
2. Platforming montage across the three levels.
3. Glide through the wind currents.
4. Spin attack combo on drones.
5. Collectible counter ticking.
6. Final boss.
7. PS2 next to PC 4K and Android with a gamepad.

### Assets

| Kind | Asset | Budget / notes |
| --- | --- | --- |
| Hero | Pequi (round body, seed-spike crest, leaf scarf), 2 alternative skins | 5,000 triangles, 256x256 CLUT8, 30 bones |
| Allies | Seed spirits (7 rains, small floating creatures), Vovó Buriti (old palm tree, hub guide) | 800–3,000 triangles |
| Enemies | Pump drone, dry-thorn critter, robo-armadillo, steam turret | 800–2,000 triangles |
| Bosses | Mini bosses: Giant Mosquito Pump (vereda), Wind Kite Machine (chapada), Piston Golem (factory); final boss Baron Ressequido in his pump-walker | 4,000–8,000 triangles each |
| Levels | Hub (dry and green versions of the same mesh via vertex colour and textures), Vereda dos Buritis, Chapada dos Ventos, Fábrica Seca | Strip-friendly modular kits; several CLUT8 texture sets per level |
| Props | Crates, water drops, rain jars, spring stones, moving platforms, wind fans, conveyors, pistons, checkpoints (seed totems) | 20–600 triangles |
| Animations | Pequi: idle, idle fidget, walk, run, jump, double jump, fall, land, glide, spin, ground pound, roll, wall kick, hurt, death, victory dance, collect. Enemies: 6–8 clips each. Bosses: 10–15 clips each | Skinned clips, 30 fps |
| VFX | Water splash, rain return (hub turning green), spin trail, steam, dust, collectible sparkle, blob shadow | CPU particles |
| UI | Title, hub map, level select, collectible counters, pause menu, concept art gallery, save icon (memory card `icon.sys` and 3D icon) | CLUT8 |
| Audio | Hub theme (dry and green versions), 3 level themes, boss theme, ~80 effects (jumps, spins, drops, robots, wind, water) | 16-voice mixer, music streamed from disc |

---

## Production notes

- **Make each demo's vertical slice first:** one room for Submersa, one track and car for Poeira, one level for Pequi. Use it to check the budgets in the editor before building the rest.
- **Capture the video footage in the emulators** (PCSX-Redux, PPSSPP, PCSX2) and on PC. Label every clip with its target and whether it came from an emulator or real hardware (brand guide rule).
- **The engine's 2D slice (F4) and its templates** are the place to try UI layouts, fonts and audio before the 3D work.
- **Write the asset list of each demo into its project README** when the project is created, so progress can be tracked per asset.

## Sources

- Survival horror on PSX: [Racketboy, PS1 survival horror library](https://racketboy.com/retro/the-playstation-ps1-survival-horror-library), [Infinity Retro, best PlayStation survival horror games](https://infinityretro.com/best-playstation-survival-horror-games/), [Time Extension, best PS1 games](https://www.timeextension.com/guides/best-ps1-games-of-all-time-playstation-titles-you-shouldnt-miss)
- Racing on PSP: [Push Square, best PSP games](https://www.pushsquare.com/guides/best-psp-games-of-all-time1), [GameSpot, WipEout Pure review](https://gamespot.com/reviews/wipeout-pure-review/1900-6120740/), [GamesRadar, Ridge Racer review](https://www.gamesradar.com/ridge-racer-review/2), [Fandomspot, best PSP racing games](https://www.fandomspot.com/best-psp-racing-games/)
- Mascot platformers on PS2: [GamePressure, PS2 3D platformers](https://www.gamepressure.com/games/ps2/3d-platformers/), [PlayStation Blog, classic PlayStation 3D platformers](https://blog.eu.playstation.com/2017/08/23/which-of-these-16-classic-playstation-3d-platformers-was-your-favourite/), [ComicBook.com, Daxter retrospective](https://comicbook.com/gaming/feature/daxter-psp-spin-off-gaming-history)
