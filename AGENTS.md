# AGENTS.md

Guide for AI coding agents working in this repository. Read [RULES.md](RULES.md) too: it holds the project rules and they are not optional.

## What this repository is

Rensga Engine is a planned open-source (MIT) engine, editor and Unity-style C++17 API for making games that ship as PSX, PSP and PS2 disc images plus Windows, Linux and Android builds.

**The repository holds the docs plus the Step 1 MVP** (engine/core, engine/api, PSP and GU backends, cooker, pipeline, CLI, minimal editor, `templates/psp-3d`, tests). Do not invent build commands, source files or features that don't exist; see TASKLIST.md for what is done.

- `docs/rensga-engine/design-doc/rensga-engine-design-doc.md` is the main design reference. Read it before any design or architecture work.
- `docs/rensga-engine/roadmaps/` has 13 roadmaps (phases, estimates, Claude prompts), generated from Python data.
- `docs/rensga-engine/brand/` has the visual identity: pequi palette, design tokens, logo rules and the editor theme, all generated from `roadmaps/src/data_brand.py`. Use its tokens for any UI or page color.

## Source files vs generated files

| Kind | Files |
| --- | --- |
| **Edit these** | `docs/rensga-engine/roadmaps/src/data_core.py`, `data_psp.py`, `data_psx.py`, `data_minor.py`, `data_brand.py`; `roadmaps/src/build.py`, `build_doc.py`, `build_brand.py`; `design-doc/rensga-engine-design-doc.md`; `design-doc/diagrams/*.svg`; `docs/rensga-engine/README.md` |
| **Never edit by hand** | `docs/rensga-engine/index.html`, `rensga-icon.svg`, `roadmaps/*.html`, `roadmaps/data/roadmaps.json`, `design-doc/rensga-engine-design-doc.html`, everything in `brand/` |

The icon is the `ICON` string in `build.py`. Page CSS and JS are the `CSS` and `JS` strings there too.

## Commands

Code (MVP; toolchains via build support modules, `rensga modules install psp-build` (Docker via `tools/docker/psp.sh` is the optional contributor backend), PPSSPP via `tools/emulators/fetch_ppsspp.sh`):

```bash
# Build support modules (F3.08; catalog in tools/modules/manifest.d/<id>.json, installed per user in $RENSGA_HOME/modules or ~/.local/share/rensga/modules)
build/host/tools/cli/rensga modules list [--json]                         # modules, versions, installed state, host availability
build/host/tools/cli/rensga modules install psp-build [--accept-licenses] # download, verify sha256, extract, check; also remove|verify|path <id>
build/host/tools/cli/rensga modules env psp-build --shell sh              # env vars + PATH for a module (sh|cmd|json); tools/modules/toolchain_env.sh <id> evals it
build/host/tools/cli/rensga modules doctor [--json]                       # what is installed, what is missing, whether Docker is usable
# Hub modules --ui-event verbs (editor screenshots, tests/editor/modules_ui_check.sh): modules | modhub[:off] | modselect:<id> | modinstall:<id> | modaccept:<module or license id> | modremove:<id> | modverify:<id> | modlocate:<id>|<dir> | modstate[:<id>] | modwait | playconsole | modgate:<target> | moddialoginstall | moddialogcancel
# test env: RENSGA_MODULE_CATALOG=<dir> (fixture catalog), RENSGA_EDITOR_TEST_MODULE_DELAY_MS=<ms> (slows install progress for screenshots)
RENSGA_TOOLCHAIN=auto|native|docker                                       # auto (default) = native module, else Docker if usable, else a clear "install it" error
export RENSGA_HOME=$PWD/build/rensga-home                                 # dev: shared module install inside the repo (gitignored); tests use their own temp RENSGA_HOME and file:// fixtures, never the network
cmake --preset linux-host -B build/host && cmake --build build/host -j4   # tests, cooker, pipeline, CLI, editor, headless sim
ctest --test-dir build/host                                               # all tests
build/host/tools/cli/rensga cook templates/psp-3d                            # cook + budget check
build/host/engine/runtime/rensga_headless --pack templates/psp-3d/Library/Cooked/psp/game.rpak --frames 400 --hash
xvfb-run -a build/host/tools/cli/rensga play --target psp templates/psp-3d --json --quit-after 25   # dev build with the native psp-build module, boots in PPSSPP
build/host/tools/cli/rensga build --target psp templates/psp-3d              # release: EBOOT.PBP in Library/Build/psp-release/
build/host/editor/app/rensga-editor --project templates/psp-3d [--play --quit-after 30]   # --play runs the emulator pipeline (exits 1 if it fails); project scripts build into <project>/Library/ScriptAssemblies and hot-reload in the background on change (--no-auto-rebuild turns it off)
xvfb-run -a -s "-screen 0 1600x900x24" build/host/editor/app/rensga-editor --project templates/psp-3d --screenshot out.png [--screenshot-frames N] [--select <object>] [--ui-event click|rclick|move:x,y|script:Name|focus|create:cube|select:Name|addscript:Name|save ...]   # editor PNG, never opens a window on the desktop
# F3.02 asset verbs for --ui-event (Project window, asset inspector, VRAM map): asset:<Assets/..path> | assettab:<default|build target id> | assetset:<key>=<value> (e.g. format=clut4) | assetapply | assetrevert | assetundo | assetredo | assetrename:<path>|<new name> | assetmove:<path>|<folder> | assetdelete:<path> | assetdrop:<path>[|<object>] | search:<text> | folder:<Assets/..> | vram   # open Window > VRAM Map; the Inspector shows the asset until an object is picked
# F3.07 edit-mode callbacks: --ui-event editworld rebuilds the edit-mode World now and prints "scene: editworld valid= rebuilds= objects= error="; tests/editor/callbacks_ui_check.sh (ctest editor_callbacks_ui) shows the Patrol waypoint gizmos
# F3.01 inspector verbs for --ui-event: multiselect:A|B (object names) | addcomp:<Type> | rmcomp:<i> | moveup:<i> | movedown:<i> | resetcomp:<i> | copycomp:<i> | pastecomp:<i> | ctxmenu:<i>|<method> | setfield:<Type>.<field>=<json> | setobj:<name|active|tag|layer>=<json> | undo | redo   # act on the whole selection as one undo step; combine with shot:<png>
ctest --test-dir build/host -R "inspector"                                 # inspector model + gate F3.G2 (every edit undoes/redoes exactly), xvfb inspector screenshots (77 = skipped)
xvfb-run -a build/host/engine/runtime/rensga_desktop --stress-sprites 1000 --frames 300 --screenshot out.png   # desktop GL player (or --pack <game.rpak>); exits 4 if alloc-check allocs != 0
# F3.04 Play in editor (Play dropdown default; --play still runs the emulator pipeline) --ui-event verbs: openscene:<scene name> | showgame | playedit | pause | resume | pausetoggle | step[:N] (exactly N fixed frames, pauses first) | playwait:N (hold the queue until N game frames ran) | playkeys:<SDL key names,comma> (Game view input, `none` clears) | playset:<object>|<component index>|<field>|<json> | playmove:<object>|x,y,z | playdump:<object>[|<component>|<field>] (prints runtime values) | stopedit (prints `play-check: ... identical=1 dirty=0 undo=N`)
#   tests/editor/play_ui_check.sh <editor> <project> <scene> <object> <keys> <component|field|json> <workdir>   # Play, pause, step, edit, Stop, scene file byte-identical (ctest editor_play_psp, editor_play_platformer)
tools/emulators/pcsx2_boot.sh build/ci/ps2/rensga.iso --frames 120 --log /abs/log.txt [--shot out.png]   # PS2 boot in PCSX2 (local only; PCSX2 from `rensga modules install pcsx2` or RENSGA_PCSX2, BIOS referenced in place via ~/.config/rensga/emulators.json)
# F3.01 scene verbs for --ui-event (hierarchy edits, 2D mode, ID-buffer picking; output lines start with "scene:"): new:<name>[|<parent>] | selectmany:A,B | selectadd:A | reparent:A,B|<parent or empty>[|<before>] | dup | del | copy | paste | undo | redo | setpos:<name>|x,y,z | reload | savecopy:<file> | mode:2d|3d | framesel | pickobj:<name> | boxsel:x0,y0,x1,y1[,add] | dumpsel | dumptree | gizmos
# F4 2D slice (samples/platformer2d: Title, 3 rooms, GameOver, Results, TitleBot = the bot plays the whole game; PS2 profile 640x448; scripts are compiled into a project build tree)
#   cmake --preset linux-host -B build/sample -DRENSGA_PROJECT_DIR=$PWD/samples/platformer2d && cmake --build build/sample -j4 --target rensga_desktop rensga-cooker
#   build/sample/tools/cooker/rensga-cooker cook --project samples/platformer2d --target ps2 --force   # the ps2 pack is also the Windows, Linux and Android pack
#   GALLIUM_DRIVER=softpipe XDG_DATA_HOME=$(mktemp -d) xvfb-run -a build/sample/engine/runtime/rensga_desktop --pack samples/platformer2d/Library/Cooked/ps2/game.rpak --scene TitleBot --frames 1500 --screenshot results.png   # "game complete coins=.. time=..", "bot finished the game", allocs=0; softpipe because llvmpipe's JIT allocates; empty XDG_DATA_HOME = empty save
python3 samples/platformer2d/tools/make_sample.py [--profile ps2|psp --out <dir>]   # regenerates the sample (default ps2 into samples/platformer2d); samples/platformer2d_psp is generated with --profile psp (480x272); rensga_platformer_tests fails if either is stale
#   sample scene names: Title Room Room2 Room3 GameOver Results TitleBot (Title + AutoPilot) InputTest RoomBot; bot logs "rensga: room cleared <n>", "rensga: game complete", "rensga: bot finished the game"; AutoPilot.quitAfterResultsFrames (0 = never) quits after Results
#   UIText/UIImage/UIButton `anchor` (0 TopLeft .. 4 Center .. 8 BottomRight): x/y are offsets from that point of the view; buttons take focus in screen reading order
# F3 exercises (samples/editor_tour, see its README): one ctest each, driven by --ui-event under xvfb, screenshots in the build dir
#   ctest --test-dir build/host -L f3_exercises --output-on-failure      # scene_roundtrip hot_reload play_revert door patrol input_desktop + editor_assets_ui
#   tools/ci/editor_tour.sh                                                # console side: rensga play/build --target psp on samples/editor_tour (native psp-build module + PPSSPP, exit 77 without)
build/host/tools/iso/rensga-iso -o out.iso -V VOLID [--order A,B] <dir>   # ISO 9660 writer (no xorriso); --list image.iso reads one back; ctest -R iso
# F4 PSP base (PSP Engine 0-5; psp-build module + PPSSPP 1.20.4, pinned in tools/modules/manifest.d/ppsspp.json and tools/ci/build_ppsspp_headless.sh)
tools/ci/psp_eboot.sh                     # F4.02: release flow (rensga build --target psp: PRX, mksfoex, pack-pbp) on a copy of templates/psp-3d, EBOOT.PBP booted in PPSSPPHeadless (EBOOT in the -r dir = host0:, game.rpak in $HOME/.ppsspp/PSP/GAME/RENSGA = ms0:); fails on PPSSPP version drift under CI
tools/ci/psp_hello.sh                     # F4.02: rensga_hello as EBOOT.PBP in PPSSPPHeadless: "rensga hello: done frames=120", allocs=0 (77 = no module/PPSSPP)
build/host/tools/cooker/rensga-cooker cook --project <dir> --target psp   # F4.05: PSP textures (auto|clut4|clut8|rgba5551|rgba4444|rgba8; pow2 stored size <= 512, swizzled, kTexSwizzled) and, only for the psp target itself, compact MSHX meshes; PSP-only formats go in .meta targetOverrides.psp (a psp pack is not drawable by GL/GS: pack_check.py refuses it)
build/host/tools/cooker/rensga-cooker gen-header --project <dir> --out <dir>/Library/Generated/psp/console_target.gen.h   # header needed to build the PSP ELF of a project without the CLI
build/host/tests/cooker/rensga_cooker_tests psp_                          # F4.05: PSP cooker (swizzle round trip, padding, nibble order, size error, compact meshes, RAM ceiling)
ctest --test-dir build/host -R "cooker_cli_assets2d_psp|pad_psp|rensga_gu_tests"   # psp cook of the 2D fixture, pad maps (sceCtrl + SDL controller), GU sprite queue and VRAM allocator (build/host/tests/render/rensga_gu_tests)
cmake --preset psp -B build/psp -DRENSGA_PSP_CPU_MHZ=222|333 -DRENSGA_PSP_STRESS_SPRITES=<n> -DRENSGA_PSP_AUDIO_STRESS=ON -DRENSGA_PSP_AUTOSTART_SCENE=<name> -DRENSGA_EXIT_AFTER_FRAMES=<n> -DRENSGA_PSP_NATIVE_PAD=ON -DRENSGA_GU_DUMP_FRAME=<n> -DRENSGA_GU_DUMP_FRAMES="20,400,1000" -DRENSGA_GU_SELFTEST=ON   # PSP options: SDL2 for pad + audio (never video); DUMP_FRAME(S) write host0:/rensga_shot[_<frame>].bmp (tools/ci/bmp2png.py converts); SELFTEST draws a 2D panel; RENSGA_PSP_EXIT_AFTER_FRAMES=<n> is the env form CI uses
cmake --build build/psp --target rensga_hello                              # hello as ELF/PRX (psp_hello.sh makes the EBOOT)
tools/ci/psp_2d.sh [slice|gles|input|stress|audio|determinism|all]        # F4.07: cooks samples/platformer2d_psp, PSP ELF with TitleBot autostart, PPSSPPHeadless: game complete, allocs=0, saves, a PNG per title/room/results in $RENSGA_CI_OUT/psp2d/shots; slice (default, CI) | gles (--graphics=gles, ~8 min, log checks only: its frame dump is stale) | input / stress / audio = the PSP exercises (exercises.md next to the shots; stress also boots the PS2 side in PCSX2 when a BIOS exists) | all
# PSP log lines: "rensga: data root=", "rensga: mem budget=", "rensga: prof ...", "rensga: audio device want.freq=...", "rensga: input sdl controller=", "[gu] vram tex=<used>/<area> ram_tex=<n>", "[gu] meshes compact=<n> float=<m>"
# F4.09/F4.10 templates and Hub: templates/{psp,ps2}-2d are generated from samples/platformer2d, templates/psp-3d is the 3D one; editor/data/hub.json "templates" = {api, kind, dir, available, comingIn, description, note}; a card is selectable only when available and templates/<dir>/ exists (templates/<dir>/thumbnail.png is the card image, not copied into projects)
python3 tools/templates/make_2d_template.py --profile psp|ps2 --out templates/<api>-2d   # regenerates a 2D template (calls make_sample.py); --check <dir> exits 1 on a difference (ctest templates_2d_generator)
tools/ci/templates.sh [--thumbnails]      # the 2D templates on private copies: generator equality, cook for linux + rensga_headless/desktop (allocs=0), psp-2d in PPSSPPHeadless, ps2-2d ISO (+ PCSX2 when a BIOS is set); --thumbnails rewrites templates/<dir>/thumbnail.png; RENSGA_TEMPLATES_ONLY=linux|psp|ps2; 'rensga cook <project>' only knows the scripts of the project the CLI was built with, so this uses a project-built rensga-cooker
# F4.09 hub verbs for --ui-event (Create Project dialog, six template cards; tests/editor/hub_templates_ui.sh = ctest editor_hub_templates_ui): hub-new | hub-name:<n> | hub-api:<id> | hub-template:<2d|3d> | hub-dump (prints "template: <dir> api= kind= state=available|disabled reason=Coming in <phase>") | hub-submit (prints "hub-create: ok template=<dir> dir=<path>" or "hub-create: refused: <why>")
# F4.08 tile verbs for --ui-event (Tile Palette, 2D painting; output lines start with "tile:"; undo/redo are the scene's shared history): palette:<tilemap asset path> | brush:<paint|erase|rect|fill|rule> | tile:<id> | ruletile:<*.ruletile.json path> | painttile:<x>,<y>[;<x>,<y>..] | paintrect:x0,y0,x1,y1 | filltile:x,y | tileflag:<id>=<solid|oneway|slope45L|slope45R|slope22L_lo|..|none> | savetiles | dumptiles:x0,y0,x1,y1 | newtilemap:<name>|<tileset texture path>|<tile size>|<w>x<h> (assigns it to the selected Tilemap) | tiledrag:x0,y0,x1,y1[;x,y..] (real mouse stroke) | tilepaint:on|off | closetools | undoname | tilehover
# F4.08 sprite verbs for --ui-event (Sprite Editor, SpriteRenderer picker; output lines start with "sprite:"): spriteeditor:<texture path> | slicegrid:w,h[,pad,offx,offy] | sliceapply | slicerevert | setsprite:<object>|<slice name>[|<texture path>] | spriteundo | spriteredo | spriterect:x,y,w,h[,name] | spritename:<i>|<name> | spritepivot:<i>|<Top Left|..|Bottom Right|x,y> | spriteselect:<i|name> | spritedump | spritedrag:x0,y0,x1,y1[,..] (texel coordinates) | spritepick | spriteclose
ctest --test-dir build/host -R "rensga_tilemap_tests|editor_tile_palette_ui|editor_sprite_editor_ui"   # TilemapEdit (strokes, fill, rule tiles, flags, resize, undo, byte-identical round trip), sprite slices in .meta "sprites", Rigidbody2D scene cooked and run; Tile Palette / Sprite Editor screenshots in the build dir (77 = skipped without xvfb)
ctest --test-dir build/host -L f4_exercises --output-on-failure           # exercise_paint_level: paint a level, TilemapCollider2D, Rigidbody2D player, Play in editor, player rests on the painted ground (y=192), Stop restores the scene
build/host/tests/rensga_core_tests rb2d                                   # Rigidbody2D: dynamic/kinematic/static on a tilemap (gravity is a module constant, Rigidbody2DSim::setGravity, default 1400 px/s^2)
# *.ruletile.json: {"format":1,"tileset":guid,"neighbours":4|8,"outside":"filled"|"empty","default":id,"members":[ids],"rules":[{"mask":n,"care":n,"tile":id}]}, mask bits N=1 E=2 S=4 W=8 NE=16 SE=32 SW=64 NW=128 (editor/core/include/rensga/editor/tilemap_edit.h); texture .meta top-level "sprites": {mode grid|manual, cellW, cellH, padding, offset, rects[{name,x,y,w,h,pivotX,pivotY}]} (editor/core/include/rensga/editor/sprite_slices.h)
tools/install_git_hooks.sh                                                # pre-commit guard: refuses BIOS/ROM dumps
tools/emulators/screenshot.sh                       # PPSSPP screenshot into docs/rensga-engine/mvp/ (needs a prior cook)
tools/check_editor_core.sh                                                # editor/core must not name a console
tools/check_api_rules.sh                                                  # API design rules (docs/rensga-engine/api-rules.md)
cmake --preset psp|windows|android|ps2 -B build/<x>                       # cross builds; run through the native modules (tools/modules/toolchain_env.sh <id>); tools/docker/{psp,windows,android,ps2}.sh is the optional contributor backend (RENSGA_TOOLCHAIN=docker)
tools/ci/run_local.sh                                                     # every CI job locally, native modules first (RENSGA_TOOLCHAIN=native, shared RENSGA_HOME=build/rensga-home; flags --install installs the modules the jobs need, --docker uses the contributor images); same jobs as .github/workflows/ci.yml, which installs the same modules with the CLI and never uses Docker; modules upstream lacks for Windows are built by .github/workflows/toolchains.yml (manual or tag toolchains-<id>-<version>); RENSGA_CI_OUT overrides build/ci; exit 77 = skipped; default jobs: linux_host psp psp_eboot psp_hello psp_2d templates editor_tour determinism psp_gpu_check windows windows_run android android_device ps2 ps2_check (pre-build RENSGA_CI_OUT/linux-host with the system cmake first, the jobs cannot configure it with a module cmake)
ctest --test-dir build/host -R "touch_|hot_reload|audio_stress|audio_sdl"                    # F2 host checks: touch pad + SDL finger events, desktop pack hot reload, audio-stress on SDL disk audio (skip with 77 without xvfb or a cooked pack)
build/host/tests/cooker/rensga_cooker_tests assets_cook_cache                              # per-asset cook cache (Library/Cache/<target>/; prints `cook: cooked <a>, cached <b>`)
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/tmp/a.raw xvfb-run -a build/host/engine/runtime/rensga_desktop --pack <game.rpak> --audio-stress --frames 300   # 16 voices + music, logs mix_us and underruns
cmake --preset ps2 -B build/ps2 -DRENSGA_PS2_VRAM_STRESS=ON                                # PS2 VRAM stress (24 textures); run the ISO with tools/emulators/pcsx2_boot.sh, PS2-local only
tools/ci/linux_host.sh                                                    # host build + ctest + the 2D slice on the desktop player (TitleBot, softpipe, xvfb): game complete, allocs=0, Title/Results screenshots in $RENSGA_CI_OUT/linux-slice-run
tools/ci/windows.sh && tools/ci/windows_run.sh                            # F4.01: slice cross-built (windows-build module) and cooked in the job, TitleBot under wine + Xvfb (softpipe): game complete, allocs=0 (coin count varies on slow wine runs)
tools/ci/ps2.sh                                                           # F4.01: ISO of the 2D slice (cooks samples/platformer2d for ps2 with a host cooker that has its scripts built in; GAME.RPK + MUSIC*.WAV); refuses a pack the GS cannot draw (tools/ci/pack_check.py: swizzled textures, PSP formats, MSHX)
tools/ci/ps2_check.sh                                                     # LOCAL ONLY (your BIOS in emulators.json): slice ISO with AUTOSTART TitleBot in PCSX2: game complete, prof lines, icon.sys, card save + readback, allocs=0, no texture upload error, screenshot $RENSGA_CI_OUT/ps2-check/ps2_slice.png (77 without PCSX2/BIOS)
RENSGA_PCSX2_WORK=<dir> tools/emulators/pcsx2_boot.sh <iso> ...            # own sandbox/display dir for parallel PCSX2 runs (default build/pcsx2)
tools/ci/android.sh                                                       # slice APK build/ci/android/rensga-player.apk (game.rpak + every music*.wav; cooks the slice itself); exits 77 locally without the android-build module (its Google license is the user's to accept, never pass --accept-licenses for them)
tools/android/run_device_check.sh                                          # install + run the APK on a USB phone via adb (LOCAL ONLY, never in CI; the phone run is deferred, F4.01/F4.G2 stay half ticked)
tools/ci/android_device.sh                                                # install + run on an adb phone; prints "built, not run" without one
tools/ci/determinism.sh [3d|2d]                                           # 3d (default): same walk.json on Linux and PSP, state hashes must match (DET_TRACE=1 compares every frame); 2d: rensga_headless vs the PSP sim ELF playing TitleBot (copy of samples/platformer2d_psp), fixed 1/60 step, final hash AND per-frame trace must match; DET_2D_PROJECT=<dir> overrides the project
# every runtime main prints "rensga: alloc-check frames=<N> allocs=<K>" (K must be 0) and "rensga: frame <n>" every 60 frames
# heavy jobs on a small machine: wrap in flock /tmp/rensga-heavy.lock, nice -n 19, RENSGA_JOBS=2
```

Docs:

Run from `docs/rensga-engine/roadmaps/src/`. The argument is the output folder, `docs/rensga-engine`.

```sh
python build.py ../..        # roadmap pages, index.html, rensga-icon.svg, roadmaps.json
python build_doc.py ../..    # design doc HTML from the Markdown (needs: pip install markdown)
python build_brand.py ../..  # brand/: visual identity HTML + Markdown, palette.json, rensga-tokens.css, one-color icons
```

Always pass `../..`. With no argument the scripts write to `./out` instead.

`build_brand.py` first checks every token pair for contrast and that the icon's colors (`LOGO_PARTS`) appear in `build.ICON`. If a check fails, it prints the failing pairs and writes nothing. Fix the colors; don't lower the minimums.

**Check a change without touching the repo:** build into a temporary folder and compare.

```sh
python build.py /tmp/rensga-check && python build_brand.py /tmp/rensga-check \
  && diff -r /tmp/rensga-check ../.. | grep -v '^Only in ../..'
```

No output means the committed pages match the data. The build is deterministic, so any difference is a real change.

## Roadmap data format

Each `data_*.py` module defines roadmap dicts and exports them as `ROADMAPS = [...]`. `build.py` lists the modules in `GROUPS` (group name, blurb, roadmaps). A new module must be imported and added to `GROUPS`.

```python
ROADMAP = {
    "slug": "psp-engine",            # output file name: roadmaps/<slug>.html
    "title": "...",
    "summary": "...",                # also shown on the index card
    "stats": [("total", "~21 weeks"), ("phases", "9"), ...],  # "total" is shown on the index card
    "legend": ["name for group 1", "name for group 2", "name for group 3"],
    "phases": [
        (group, title, subtitle, estimate, note, topics, exercises),
        (group, title, subtitle, estimate, note, topics, exercises, "0.5"),  # optional label
    ],
}
```

- `group` is 1, 2 or 3. It picks the legend entry and the color (CSS classes `g1` to `g3` only).
- Phases are numbered automatically from 0. A phase with an 8th element uses it as its label and does **not** advance the counter. Editor's "0.5 TargetModule interface" works this way.
- `topics` and `exercises` are lists of `(label, prompt)`. Prompts are written to Claude in the first person ("Design ...", "Explain ..."). Exercise prompts start with "Give me an exercise:" or "Guide me through an exercise:". Prompts must stand on their own, because users paste them into a fresh chat.
- `stats` values are typed by hand. Keep the `"phases"` count and `"total"` in step with the phase list and with the design doc's roadmap index table.
- All text is HTML-escaped by the builder, so write plain text, not HTML.

## Brand data format

`data_brand.py` holds plain data. Colors are written as `"<ramp>.<step>"` (for example `"pequi.500"`), as a token name (for example `"primary"`), or as raw hex.

- `RAMPS`: the palette, as named ramps of `(step, hex)`.
- `LOGO_PARTS`: icon part -> palette color. These must match `build.ICON` exactly.
- `TOKENS`: `(name, light, dark, use)`. These become `--rg-<name>` in CSS.
- `CHECKS`: `(fg, bg, minimum)`, checked in both themes. Target tags and the `EDITOR` rows that carry text are checked automatically.
- `TARGETS`, `LOGO`, `TYPE`, `EDITOR` (the Dear ImGui dark theme), `IMAGERY`, `VOICE`: the guide's other sections.

The docs pages' own CSS (`CSS` in `build.py`) predates the brand tokens and doesn't use them yet.

## Cross-references

The design doc cites phases as "<Roadmap> <number>", for example "(Editor 3; PSX Engine 4)". Changing phase numbers, names or estimates means updating:

1. the phase list in `data_*.py`,
2. the roadmap's `stats`,
3. the design doc's text references and its "Roadmap index" table,
4. the generated files (rebuild both scripts).

Search before renumbering: `grep -n "PSX Engine" docs/rensga-engine/design-doc/rensga-engine-design-doc.md`.

## Domain facts to get right

- Profiles: PSX (20.12 fixed-point, 2 MB RAM, 320x240, PSn00bSDK, no SDL), PSP (float, 480x272, pspdev + SDL2, GU), PS2 (float, 640x448, ps2dev + SDL2, gsKit for 2D, ps2gl on VU1 for 3D).
- One project = one profile, always also built for Windows, Linux and Android.
- Emulators: PCSX-Redux with OpenBIOS (PSX), PPSSPP (PSP), PCSX2 with the user's own BIOS (PS2). PS2 cannot boot in public CI.
- The editor runs on Windows and Linux only: Dear ImGui (docking), ImGuizmo, imnodes, SDL2, OpenGL.
- Console and cross builds run in pinned native build support modules (psp-build, ps2-build, windows-build, android-build, ...), installed per user from the Hub or `rensga modules`. Users never need Docker; Docker is an optional contributor backend (`RENSGA_TOOLCHAIN=docker`).
- Non-goals: C#/.NET, shader graphs, per-pixel lighting, URP/HDRP, DOTS, store integration, shipping any Sony BIOS, an editor on Android or consoles.

## Task list

`TASKLIST.md` is the work queue: Step 1 (MVP, tasks `MVP-xx`), then Step 2 (milestones F1–F10, one task per roadmap phase, with gates).

- Start from the lowest unticked task in the current step. Read its roadmap phase before writing code.
- Tick a task only when its "done when" is fully true. Note partial progress under the task.
- Never renumber task IDs. Add new tasks at the end of their block.
- Every MVP shortcut gets a row in the MVP hook register naming the Step 2 task that finishes it. A deviation from the design doc needs its reason there.
- Step 2 weights are `estimate / 243.5 weeks`, rounded so they sum to exactly 100%. If a roadmap phase is added, removed or re-estimated, update TASKLIST.md in the same change and recompute the weights and the Progress table.
- Update the Progress table's Status column when you tick tasks.

## When code is added

Follow the planned layout and dependency rules in RULES.md §2 and the design doc's "Architecture" section. Before writing a system, find its roadmap phase and follow the scope and constraints there. Keep this file's commands section up to date as real build and test commands appear.

## Environment notes

- This is a git repository (branch `f4`, main branch `main`). Don't commit unless asked, and never push without the user's consent.
- Python 3 is required for the docs build. `markdown` may not be installed. If it is missing, ask before installing it, or skip `build_doc.py` and say so.
