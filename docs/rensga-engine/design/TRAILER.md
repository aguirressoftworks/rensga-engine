# Rensga Engine showcase trailer

Screenplay for a 3:00 trailer that shows the engine, why it is different, and its three demo games ([DEMO_GAMES.md](DEMO_GAMES.md)). Every shot has a timecode, the picture, the camera, the on-screen text and the sound. Durations add up to exactly 180 seconds.

## Ground rules

- **Brand:**
  - Title cards use Inter and code uses JetBrains Mono (the editor's fonts).
  - Pequi gold is the single accent colour. Target tags use the target colours:
    - PSX: ipê roxo `#6e4a9e`
    - PSP: vereda `#1d6f73`
    - PS2: ipê rosa `#b23c6e`
    - PC and Android: soil
  - Colours come from `docs/rensga-engine/brand/`.
- **Trademarks:**
  - Say a build "runs on PSP". No PlayStation logos, no logo colours, no controller face-button shapes, and nothing that suggests Sony made or endorses Rensga.
  - Console names appear only as plain text tags.
- **Honest footage:**
  - Every gameplay clip carries a corner tag with its target and where it ran, for example `PSP · PPSSPP emulator`, `PS2 · PCSX2 emulator`, `PC · Windows build`, `Android · phone`.
  - Only show features that exist when the trailer is cut.
  - If games are still in development, the end card says so.
- **Text style:** short plain sentences, no marketing superlatives (brand voice). "Rensga Engine" on first mention, then "Rensga".
- **No voiceover needed.** The trailer works with on-screen text and music alone. Optional voiceover lines are at the end.

## Music

One original track, 120 BPM (one bar = 2 seconds), in four parts:

| Time | Part | Feel |
| --- | --- | --- |
| 0:00–0:15 | Intro | Cerrado ambience (cicadas, dry wind), a single viola caipira phrase |
| 0:15–1:30 | Build | Light beat comes in; ticks and UI clicks sit on the beat |
| 1:30–2:42 | Drop | Full band. Each game section changes the colour: dark drone for Submersa, driving rhythm for Poeira, bright melody for Pequi |
| 2:42–3:00 | Outro | Peak for 8 seconds, then a ring-out over the logo |

Cut on the beat: act and game boundaries fall on a bar (an even second), and shots inside a game section cut on a beat (whole seconds).

## Shot list

### Act 1 · Hook (0:00–0:15)

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 1 | 0:00–0:04 | Black, then the pequi icon assembles from low-poly facets over cracked dry soil | Slow push-in, centred | — | Cicadas, wind, first viola note |
| 2 | 0:04–0:10 | Three 2-second clips. (a) **Submersa:** a bell tower in fog. (b) **Poeira:** a car drifts through dust. (c) **Pequi:** the hero glides over a green valley. | Each clip a still frame that starts moving on the beat | Target tag on each clip | Viola phrase, a bell, an engine, a whoosh, one per clip |
| 3 | 0:10–0:15 | Logo lockup: icon plus "Rensga Engine" | Static, gentle scale-up of 2% | Line 1: **Rensga Engine** · Line 2: "One editor. Three consoles. Every game also on PC and Android." | Beat comes in on 0:14 |

### Act 2 · The workflow (0:15–1:10)

Screen recordings of the editor at 1920x1080, cursor visible, zoomed crops where needed. Each shot has one text line, bottom-left, in a pequi-gold bar.

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 4 | 0:15–0:22 | Hub, Create Project. The cursor picks the PSP API, then hovers the **2D** and **3D** template cards and clicks 2D. The PSX and PS2 cards are visible next to it. | Full screen, then a crop on the cards | "Start from a 2D or 3D template for each console." | Click on the beat |
| 5 | 0:22–0:28 | Hub > Modules: "PSP build support" installs, the progress bar fills, a green "Installed" badge with a check icon | Crop on the module row | "Toolchains install from the Hub. No SDK setup. No Docker." | Rising tick |
| 6 | 0:28–0:38 | Editor, 2D level. A 4x time-lapse of painting a platformer level with the Tile Palette (paint, fill, rule tiles), then 2 seconds of the Sprite Editor slicing a sheet into frames | Full editor, then a slow pan across the Scene view | "A Unity-style editor: scenes, inspector, tile palette, sprite editor." | Brush sounds in rhythm |
| 7 | 0:38–0:46 | Split. Left: a code editor with `PlayerController2D.cpp`, changing `jumpHeight` from 3 to 6 and saving. Right: the Game view, where the character now jumps twice as high, with no restart | Split screen 50/50; a highlight box on the changed number | "C++ scripts with Unity's names. Save, and the game reloads." | Keyboard taps, save chime |
| 8 | 0:46–0:54 | Play dropdown, "Play on console". The PSP emulator window opens and the level boots | Crop on the dropdown, then a zoom into the emulator window | "Press Play. It runs in the console's emulator." | Boot sound |
| 9 | 0:54–1:02 | A 1024x1024 sky texture is dropped in. The Console shows a red error with an icon: `Assets/Textures/sky.png: 1024x1024 is over the PSP limit of 512`. The VRAM map turns red. Setting max size to 512 turns it back to green. | Crop on the Console line, then the VRAM map | "Console limits are checked before you build, and errors name the asset." | Error buzz, then a soft confirm |
| 10 | 1:02–1:10 | Build: one project, four outputs. A 2x2 grid of the same level running: PSP emulator, Windows, Linux, Android phone | Grid fades in tile by tile on the beat | "One project builds for its console plus Windows, Linux and Android." | Four hits, one per tile |

### Act 3 · Why it is different (1:10–1:30)

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 11 | 1:10–1:18 | Split: the PSP emulator and the Linux build play the same recorded input. An overlay counter shows both state hashes updating each second; they stay equal (`05d7d1bf = 05d7d1bf`) | Static split, the hash bar centred at the bottom | "Same inputs, same result, on the console and on PC." | Two clock ticks in sync |
| 12 | 1:18–1:24 | Motion graphic. A scene file (JSON) flows into a box labelled "Cooker", which splits into three packs: swizzled palette textures for PSP, palette textures for PS2, 4-bit texture pages for PSX | Flat diagram, left to right, in the brand colours | "The game is data. Each console gets its own cooked version." | Whoosh, three chimes |
| 13 | 1:24–1:30 | The repository page with the MIT license, then the module list (pinned versions, checksums) | Slow scroll | "Open source, MIT. Pinned toolchains. No console BIOS included." | Beat drops out for one bar before the drop |

### Act 4 · Three games (1:30–2:42)

Each game gets 24 seconds: a 2-second title card in its target colour, gameplay, and a closing comparison of platforms.

**Submersa · survival horror · runs on PSX (1:30–1:54)**

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 14a | 1:30–1:32 | Title card on ipê roxo | Static | **SUBMERSA** · "Survival horror · runs on PSX" | Drop: low drone |
| 14b | 1:32–1:36 | The drained reservoir at dusk, the town's church rising from the mud | Fixed wide camera, as in the game | — | Bell tolls once |
| 14c | 1:36–1:40 | Inês walks down the main street; three cuts between fixed angles at doorways | The game's own fixed cameras | — | Footsteps in mud |
| 14d | 1:40–1:44 | A mud walker lurches into frame; Inês runs; a door slams shut | Cut to a low angle on the slam | — | Groan, slam on the beat |
| 14e | 1:44–1:47 | Inventory grid, then the map screen | Static, game UI | — | Menu clicks |
| 14f | 1:47–1:50 | The Caretaker rises in the flooded control room | Low angle, slow tilt up | — | Drone swells |
| 14g | 1:50–1:54 | Three-way split of the same room: PSX emulator, PC, Android | Tiles slide in | "Fixed-point gameplay plays the same on PC and Android." | Drone cuts out |

**Poeira · arcade racing · runs on PSP (1:54–2:18)**

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 15a | 1:54–1:56 | Title card on vereda | Static | **POEIRA** · "Arcade racing · runs on PSP" | Driving rhythm starts |
| 15b | 1:56–1:59 | Lia pulls the cover off her grandfather's car in the garage | Slow dolly toward the car | — | Cloth whoosh |
| 15c | 1:59–2:02 | Start-line countdown, six cars | Low chase camera | — | Countdown beeps on the beat |
| 15d | 2:02–2:06 | A drift chain on Serra Dourada, the boost bar fills, boost flame | Chase camera, HUD visible | — | Tyre skid, boost burst |
| 15e | 2:06–2:10 | A jump at Chapada Velha in slow motion against the sunset | Replay camera, side angle | — | Music drops to half-time for 2 bars |
| 15f | 2:10–2:13 | Night race through the cane fields, headlights | Bumper camera | — | Engine roar |
| 15g | 2:13–2:15 | Four-way split, ad hoc race (only if ad hoc exists; otherwise a ghost time-trial race) | Grid | — | — |
| 15h | 2:15–2:18 | Split: Android with touch steering, PC with a gamepad | Hands-on footage, side by side | "Same race on PSP, PC and Android." | Engine fades |

**Pequi · 3D platformer · runs on PS2 (2:18–2:42)**

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 16a | 2:18–2:20 | Title card on ipê rosa | Static | **PEQUI** · "3D platformer · runs on PS2" | Bright melody starts |
| 16b | 2:20–2:25 | The dry hub valley. Pequi wakes and the first rain returns: the valley turns green in a time-lapse | Slow orbit, high angle | — | Rain, melody rises |
| 16c | 2:25–2:30 | Platforming montage: wetlands, cliffs, factory, cuts of about 1 second | The game camera, cuts on the beat | — | Jump sounds on the beat |
| 16d | 2:30–2:33 | Glide through wind currents over the cliffs | Orbit behind and around Pequi | — | Wind whoosh |
| 16e | 2:33–2:36 | Spin attack through three drones; the collectible counter ticks up | Game camera, the counter in a zoomed inset | — | Hits, sparkle |
| 16f | 2:36–2:39 | The Baron's pump-walker towers over the arena | Low angle, push-in | — | Heavy stomps |
| 16g | 2:39–2:42 | Three-way split: PS2 emulator, PC at 4K, Android with a gamepad | Tiles slide in | "One game, every screen." | Melody resolves |

### Act 5 · Close (2:42–3:00)

| # | Time | Picture | Camera | On-screen text | Sound |
| --- | --- | --- | --- | --- | --- |
| 17 | 2:42–2:50 | Fast montage, 0.5-second cuts: editor, the three games, the Hub, the build grid | Hard cuts on every beat | 2:44 "Make it once." · 2:47 "Ship it everywhere." | Music peak |
| 18 | 2:50–2:56 | Logo lockup, pequi icon | Static | **Rensga Engine** · "Free and open source" · `github.com/<org>/rensga-engine` · release date or "Coming <date>" | Final chord |
| 19 | 2:56–3:00 | Legal card, small text on dark background | Static | "Not affiliated with or endorsed by Sony Interactive Entertainment. Console names say where builds run. Footage from the PCSX-Redux, PPSSPP and PCSX2 emulators and from PC and Android builds. Games shown are in development." | Ring-out |

## Timing check

| Act | Time | Seconds |
| --- | --- | --- |
| 1 · Hook | 0:00–0:15 | 15 |
| 2 · Workflow | 0:15–1:10 | 55 |
| 3 · Why it is different | 1:10–1:30 | 20 |
| 4 · Three games | 1:30–2:42 | 72 |
| 5 · Close | 2:42–3:00 | 18 |
| **Total** | | **180** |

## Footage to capture

| Shots | Source | Needs |
| --- | --- | --- |
| 4, 5 | Hub screen recordings | F4.09 (2D/3D template cards), F3.08 (modules) |
| 6 | Tile Palette and Sprite Editor | F4.08 |
| 7 | Hot reload | Done (F3.03) |
| 8 | Play on console | Done for PSP (MVP and F3) |
| 9 | Budget error and VRAM map | F4.05 (PSP texture limits), F3.02 (VRAM map) |
| 10, 11 | Build grid, hash match | F4.G2 (2D slice on every target), determinism job |
| 14 | Submersa | F9 (PSX track) and the game |
| 15 | Poeira | F5 and F6, and the game |
| 16 | Pequi | F8 and F6, and the game |

**Capture settings:**
- Emulators: native resolution scaled by a whole number, with no filtering and no "enhancements". The trailer should show the real console look.
- PC builds: 1080p or 4K.
- Android: a screen capture plus one hands-on camera shot.
- Record all footage at 60 fps, edit at 60 fps, and deliver 1080p60 plus a 4K master.
- Keep a list that maps every clip to its source (emulator, build or device), so the corner tags are correct.

## Optional voiceover

One calm voice, at most 14 short lines, placed under the text it matches:

1. (0:10) "Rensga Engine. One editor for three consoles."
2. (0:15) "Pick a console, pick 2D or 3D."
3. (0:22) "The toolchain installs itself."
4. (0:28) "Build your scenes the way you already know."
5. (0:38) "Write C++ with Unity's names. Save, and it reloads."
6. (0:46) "Press Play, and it runs in the console's emulator."
7. (0:54) "If it won't fit, Rensga tells you before you build."
8. (1:02) "Every game also ships on Windows, Linux and Android."
9. (1:10) "And it plays the same everywhere."
10. (1:24) "Free and open source."
11. (1:30) "Submersa, on PSX."
12. (1:54) "Poeira, on PSP."
13. (2:18) "Pequi, on PS2."
14. (2:44) "Make it once. Ship it everywhere."
