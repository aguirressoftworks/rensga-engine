# TargetModule (Editor 0.5)

The one seam between the console-agnostic editor and a console. `editor/core`, `tools/pipeline`, `tools/cli` and the cooker core never name a console (`tools/check_editor_core.sh`, run by ctest). Everything console-specific lives in `editor/targets/<id>/` and, for mesh formats, a cooker plugin.

Header: `editor/core/include/rensga/editor/target_module.h`.

## Interface

| Member | Purpose |
| --- | --- |
| `id()` | Short id used by `--target <id>` and `ProjectSettings` (`psp`) |
| `displayName()` | Name shown in the UI. Must be a generic product name, no Sony marks (RULES section 5) |
| `cookerPlugin()` | `ICookerFormatPlugin`: turns generic meshes into the console's vertex bytes |
| `previewRules(ConsoleTargetData)` | Game-tab resolution. Defaults to the project's ConsoleTarget.json resolution |
| `inspectorRules(ConsoleTargetData)` | Allowed components. Defaults to ConsoleTarget.json `allowedComponents` |
| `validate`, `compile`, `package` | Pipeline stages run by `tools/pipeline` with a `BuildContext` |
| `launcher()` | `EmulatorLauncher` used by Launch (separate process, never linked) |

Review result (F1.04): the MVP module hard-coded 480x272 and the component list in C++. Both are now data read from `ProjectSettings/ConsoleTarget.json` (`consoleTargetFor(root)`, cached by mtime), so a module only overrides a rule the file cannot express. The only remaining console names in shared code are the default `--target psp` value in the cooker CLI and its plugin registration, both outside the checked directories and listed under F5.04 for removal.

## Adding a console

1. Create `editor/targets/<id>/` with a CMake library and a class deriving `TargetModule`.
2. Register with `RENSGA_REGISTER_TARGET(MyModule);` in its .cpp.
3. Link it where needed with `rensga_link_whole_archive(<exe> PRIVATE rensga_target_<id>)` so the static registrar survives.
4. If it needs its own mesh format, add an `ICookerFormatPlugin` with `RENSGA_REGISTER_COOKER_PLUGIN`.
5. Add `templates/<id>/ProjectSettings/ConsoleTarget.json` (profile, scalar, resolution, budgets, allowed components).
6. No change to `editor/core`, `tools/pipeline`, `tools/cli` or the UI.

## Proof

`editor/targets/dummy` is a test-only module that packages the cooked pack into a zip. `tests/editor/test_target_module.cpp` registers it, checks the data-driven rules and the produced zip, without touching editor core.
