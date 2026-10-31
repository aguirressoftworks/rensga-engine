# TargetModule (Editor 0.5)

The one seam between the console-agnostic editor and a console. `editor/core`, `tools/pipeline`, `tools/cli` and the cooker core never name a console (`tools/check_editor_core.sh`, run by ctest). Everything console-specific lives in `editor/targets/<id>/` and, for mesh formats, a cooker plugin.

Header: `editor/core/include/rensga/editor/target_module.h`.

## Interface

| Member | Purpose |
| --- | --- |
| `id()` | Short id used by `--target <id>` and `ProjectSettings` (`psp`) |
| `displayName()` | Name shown in the UI. Must be a generic product name, no Sony marks (RULES section 5) |
| `cookerPlugin()` | `ICookerFormatPlugin`: turns generic meshes into the console's vertex bytes |
| `previewRules(ConsoleTargetData)` | Game-tab resolution. Defaults to the project's ConsoleTarget.json resolution. F5 adds `colorBits` (16 = quantize the preview to RGB565), `dither`, `maxLightsPerObject` (0 = no limit), `deviceFrame` (image path drawn around the view) and `safeArea` (pixel insets) |
| `inspectorRules(ConsoleTargetData)` | Allowed components. Defaults to ConsoleTarget.json `allowedComponents`. F5 adds `fieldRules`: `{component, field, reason}` rows the inspector greys out and explains |
| `importRules(ConsoleTargetData)` | F5: what the asset import tab offers: `maxTextureSize`, `pow2Warn`, `clutDepths`, `vertexPrecisions`, `audioRates`. Defaults from the ConsoleTarget limits |
| `toolchain()` | F5: `ToolchainInfo {moduleId, installed}`: the build support module the target needs (Hub offers to install it) |
| `emulator()` | F5: `EmulatorInfo {path, version, resetSandbox}`: the emulator executable, its version and a callback that wipes the project's emulator save area |
| `validate`, `compile`, `package` | Pipeline stages run by `tools/pipeline` with a `BuildContext` |
| `launcher()` | `EmulatorLauncher` used by Launch (separate process, never linked) |

`ICookerFormatPlugin` (cooker side) gained in F5: `cookLitMesh(LitMesh, bytes, PackMeshLit&, error)` (default writes the float `PackVertexLit` / `PackVertexSkin` forms; a target overrides it for 16-bit vertices) and `decodeCookedTexture(PackTexRec, blob, ImageRgba&, error)` (decodes a cooked texture back to RGBA for the editor preview; the default handles every `PackTexFormat` including swizzled).

`LaunchSpec` gained `headless`, `embed` (the editor will reparent the emulator window), `debugger`, `env` and `memstickDir`. `EmulatorLauncher` gained `pause()`, `resume()`, `step(n)` (all default to false = unsupported), `exitState()` (`Running`, `Exited`, `Crashed`, `Hung`), `nativeWindow()` (X11 id / HWND, 0 = none) and `saveLog(path)`. Every new member has a default, so `editor/targets/dummy` compiles unchanged.

Review result (F1.04): the MVP module hard-coded 480x272 and the component list in C++. Both are now data read from `ProjectSettings/ConsoleTarget.json` (`consoleTargetFor(root)`, cached by mtime), so a module only overrides a rule the file cannot express. F5: the cooker CLI no longer defaults to `--target psp`; without `--target` it cooks for the profile named in the project's `ConsoleTarget.json`. The only console names left in shared code are plugin registrations, outside the checked directories.

## Adding a console

1. Create `editor/targets/<id>/` with a CMake library and a class deriving `TargetModule`.
2. Register with `RENSGA_REGISTER_TARGET(MyModule);` in its .cpp.
3. Link it where needed with `rensga_link_whole_archive(<exe> PRIVATE rensga_target_<id>)` so the static registrar survives.
4. If it needs its own mesh format, add an `ICookerFormatPlugin` with `RENSGA_REGISTER_COOKER_PLUGIN`.
5. Add `templates/<id>/ProjectSettings/ConsoleTarget.json` (profile, scalar, resolution, budgets, allowed components).
6. No change to `editor/core`, `tools/pipeline`, `tools/cli` or the UI.

## Proof

`editor/targets/dummy` is a test-only module that packages the cooked pack into a zip. `tests/editor/test_target_module.cpp` registers it, checks the data-driven rules and the produced zip, without touching editor core.
