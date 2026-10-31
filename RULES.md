# Rensga Engine — Rules

These rules apply to everyone who changes this repository, human or AI. They come from the design document (`docs/rensga-engine/design-doc/rensga-engine-design-doc.md`). If the two disagree, the design doc wins and this file must be fixed.

**Breaking a rule needs a written reason.** Put it in the design doc (or, once code exists, in the pull request and a code comment at the exception).

Rules are marked **[now]** if they apply to the docs that exist today, or **[code]** if they apply once engine, editor or API code is written.

## 1. The game is data

1. **[code] The engine is a player.** Scenes, prefabs, materials, input actions, animation clips, Animator graphs, UI canvases and build settings are assets. The runtime never contains a specific game's content.
2. **[code] Consoles differ by data, not by code paths.** A console's rules live only in its ConsoleTarget asset (profile, scalar type, resolution, budgets, texture formats, depth mode, media, allowed components). Game code never branches on a console. Engine code reads the profile instead of adding `#ifdef`s.
3. **[code] One source, many cooked forms.** Cooking is a pure function of source asset + import settings + ConsoleTarget.
4. **[code] Reflection is the only description of a type.** Fields are declared once with macros (`SERIALIZE_FIELD`, `RANGE`, `HEADER`, ...). The inspector, serialization, prefab overrides, undo, hot reload and the cooker all read that one description. Never write a second, hand-made description of a type.
5. **[code] Budgets are checked before runtime.** Memory, VRAM, SPU RAM, polygon and voice budgets live in the ConsoleTarget. The cooker and validator refuse a scene that does not fit, with an error that names the asset. Nothing should first show up as a crash on a console.
6. **[code] Text for people, binary for machines.** Editor files (scenes, prefabs, `.meta`) are diff-friendly JSON. Build output is packed binary, loaded with no parsing, aligned for DMA and 2048-byte CD sectors.
7. **[code] References are by GUID**, never by path or pointer.
8. **[code] Behaviour is configured, not rewritten.** Prefer exposed fields, data assets, serialized events and Animator parameters over new code paths.
9. **[code] Determinism is part of the contract.** The same data and inputs give the same result on a console and on its PC and Android builds. PSX uses fixed-point everywhere, including its PC and Android builds.

## 2. Architecture boundaries [code]

Planned layout: `engine/{core,api,profiles,backends}`, `editor/{core,targets}`, `tools/cooker`, `templates`, `third_party/emulators`.

- `engine/core` depends on nothing console-specific. It sees backends only through the four interfaces: platform, renderer, audio, file system.
- `engine/api` depends on `core` and `profiles`, never on a backend.
- `editor/core` never names a console. Console-specific editor code lives in `editor/targets/<console>` as a TargetModule. If a console needs something the TargetModule interface lacks, grow the interface. Do not add a special case to editor core.
- Game projects depend only on `api`.
- The cooker shares reflection and asset code with the editor.
- Adding a console means writing a target module, never changing editor core.

## 3. Runtime [code]

- C++17. No exceptions.
- No heap allocation per frame. Use the arena, pool and frame allocators sized from the ConsoleTarget budget.
- 16-byte alignment for anything sent by DMA.
- Components of one type live in one fixed-capacity, contiguous array sized from scene data.
- All core math goes through the profile's `Scalar` (float, or 20.12 fixed-point on PSX).
- Renderers take a sort key as well as depth, because PSX has no z-buffer.
- Runtime builds strip reflection metadata and modules the profile does not use.

## 4. Scripting API [code]

- Keep Unity's names (`GetComponent`, `Instantiate`, `Destroy`, `Raycast`, `StartCoroutine`). Unity properties become getters and setters.
- Scripts hold `Ref<T>` handles, which read as null after `Destroy`. Never hand scripts raw pointers to engine objects.
- One API for every profile. A feature the profile lacks is greyed out in the inspector with a reason, rejected by the validator, and a compile error with a hint in scripts.
- On PSX, using `float` in game code is a compile error with a hint to use `Scalar`.
- Blueprint visual scripts compile to C++ and follow every rule here; there is no runtime interpreter. C++ stays a full scripting language: a project picks C++ or Blueprint at creation.
- Lifecycle order matches Unity: Awake, OnEnable, Start, FixedUpdate, Update, LateUpdate, OnDisable, OnDestroy.

## 5. Licensing and legal [now]

- Engine, editor, API, tools and templates are MIT.
- PCSX2, PPSSPP and PCSX-Redux are GPL. Ship them unmodified, as separate executables, each with its license and a link to its source. Launch them as separate processes only. Loading an emulator core inside the editor needs a licensing review first.
- **Never add a Sony BIOS, SDK leak or other proprietary Sony file to the repository.** PSX uses OpenBIOS, PSP needs no BIOS, PS2 users supply their own dump.
- No Sony trademarks or logos in the name, icon, packages or UI. Say a build "runs on" a console.
- Check each console SDK's license (PSn00bSDK, PSPSDK, PS2SDK) and include its notices in every package.
- Credit ideas or code borrowed from other projects (for example SplashEdit, psxsplash) in the docs and the source.
- Pin every toolchain and emulator to a version.
- **[code] Users never need Docker.** Console and cross builds use pinned, sha256-verified native build support modules, installed per user from Hub > Modules or with `rensga modules install <id>`. CI installs the same modules, so what CI tests is what users get. Docker is only an optional contributor backend (`RENSGA_TOOLCHAIN=docker`). A module that ships a GPL tool keeps it unmodified, with its license and source link, and runs it as a separate process. Google's Android SDK files are downloaded from Google after the user accepts its license, never redistributed.

## 6. Documentation [now]

- **Never hand-edit generated files.** These are written by the build scripts and get overwritten:
  - `docs/rensga-engine/index.html`
  - `docs/rensga-engine/rensga-icon.svg`
  - `docs/rensga-engine/roadmaps/*.html`
  - `docs/rensga-engine/roadmaps/data/roadmaps.json`
  - `docs/rensga-engine/design-doc/rensga-engine-design-doc.html`
  - everything in `docs/rensga-engine/brand/`
- Edit the sources (`roadmaps/src/data_*.py`, `roadmaps/src/build*.py`, `design-doc/*.md`, `design-doc/diagrams/*.svg`), rebuild, and commit the sources and the regenerated files together.
- **Phase numbers are stable references.** The design doc points to phases as "<Roadmap> <number>" (for example "PSX Engine 3"). If you add, remove or renumber phases, update every reference in the design doc, its roadmap index table (phases, estimates, "starts after"), and the roadmap's own `stats`.
- Keep the design doc and the roadmaps consistent. A design change usually touches both.
- `TASKLIST.md` refers to phases the same way and has one task per required phase. Keep it in step with the roadmaps.
- Estimates assume about 15 hours of work a week.
- Generated pages stay standalone: inline CSS and JS, no external scripts, fonts or CDNs, light and dark mode.
- Write plainly: short sentences, concrete nouns, no marketing language.

## 7. Visual identity [now]

The full guide is `docs/rensga-engine/brand/visual-identity.md`. Its source is `roadmaps/src/data_brand.py`.

- Use the semantic tokens (`bg`, `text`, `primary`, `danger`, ...), never raw palette steps or new hex values. If a token is missing, add it to `data_brand.py`.
- Pequi gold (`primary`) marks the single main action. On light backgrounds gold is a fill with dark text, never text.
- Pair every status color with an icon or a word.
- Every text and background pair must pass its contrast check. `build_brand.py` enforces this, so don't weaken a check to make a color fit.
- The icon's colors are fixed. If the icon changes in `build.py`, update `LOGO_PARTS` and the palette to match, or the brand build fails.
- Console target colors (PSX ipê roxo, PSP vereda, PS2 ipê rosa) only identify a target. Don't use them for status.
- No Sony marks, PlayStation logo colors or controller face-button shapes in the logo, icon or branding.
