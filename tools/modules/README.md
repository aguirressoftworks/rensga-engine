# Build support modules

Rensga never asks a user to install Docker. Each console or cross-build target is a **build support module**:
a pinned, sha256-verified, prebuilt native toolchain archive for the user's host (Linux x86_64, Windows x86_64,
the two hosts the editor runs on). Users install modules per user from the Hub (Modules tab) or the CLI. CI installs
the same modules, so what we test is what users get. Docker stays as an optional contributor backend
(`RENSGA_TOOLCHAIN=docker`, wrappers in `tools/docker/`).

This follows the Unity Hub "modules" model. Toolchains are third-party programs run as separate processes.
We ship the manifest, not the toolchains: archives come from their upstream release pages (or from our own CI,
`.github/workflows/toolchains.yml`, where upstream has no build for a host).

## Files

| Path | What |
| --- | --- |
| `manifest.d/<id>.json` | one catalog entry per module (schema below) |
| `include/rensga/modules/modules.h`, `src/` | library `rensga_modules`: catalog, installer, environment. No ImGui |
| `cli/` | `rensga modules ...` (also the standalone `rensga-modules` binary) |
| `toolchain_env.sh` | for scripts: `. tools/modules/toolchain_env.sh <id> [docker-wrapper]`, then `rensga_tc <cmd...>` |
| `tools/pipeline/.../toolchain.h` | `ToolchainRunner`: native > Docker > clear error, used by target modules |

## Manifest schema

```json
{
  "id": "psp-build",                 // = file name without .json
  "name": "PSP Build Support",       // shown in the Hub
  "kind": "toolchain",               // toolchain | emulator | tools
  "target": "psp",                   // ConsoleTarget id the module builds for, or null
  "version": "20261001",             // pinned; also part of the install folder name
  "requires": ["build-tools"],       // installed first; their env is part of this module's env
  "license": "...", "source": "<source url>", "notice": "<license/notice text>",
  "hosts": {
    "linux-x86_64":   { "status": "available", "url": "...", "sha256": "...", "size": 172980272,
                        "archive": "tar.gz", "strip": 1 },
    "windows-x86_64": { "status": "unpublished", "note": "..." }
  },
  "env":  { "PSPDEV": "{root}" },
  "path": ["{root}/bin"],
  "postInstall": [ { "argv": ["sdkmanager", "--install", "..."], "license": "android-sdk" } ],
  "check": ["psp-gcc", "--version"]
}
```

* `status`: `available` (download it), `unpublished` (we build it in our CI, not released yet: the Hub and CLI say so
  plainly), `manual` (the user installs it and points the editor at it; `note` says how).
  A host that is missing from `hosts` gets "no build for <host>".
* `archive`: `tar.gz`, `tar.xz` or `zip` (guessed from the url when left out). `strip` drops that many leading
  directories after unpacking (each must hold exactly one directory). `size` is bytes, for progress only.
* **Several archives per host:** instead of the inline `url`/`sha256`/... put `"parts": [ {url, sha256, size, archive, strip, dest}, ... ]`.
  Each part is unpacked into `<root>/<dest>` (default the root); directories merge, a file clash fails the install.
  `build-tools` uses this (CMake + Ninja).
* `env` values, `path` entries, `postInstall.argv` and `check` expand `{root}` (this module's install folder)
  and `{root:<id>}` (another module's). `path` entries are prepended to `PATH`; a module's own come before its dependencies'.
* `postInstall` steps run in the module root with the module environment, after `check` passes. A step with
  `license` runs only after the user accepted that license id. The install refuses **before downloading anything**
  when a needed license was not accepted (Hub: dialog; CLI: prompt or `--accept-licenses`).
  Files owned by someone else (Google's Android SDK, for example) are downloaded from their owner by such a step
  and never redistributed by us.
* `check` must exit 0 after unpacking, or the install is rolled back.

## Install layout and flow

* Modules dir: `$RENSGA_HOME/modules` if `RENSGA_HOME` is set, else `$XDG_DATA_HOME/rensga/modules`
  (`~/.local/share/rensga/modules`) on Linux, `%LOCALAPPDATA%\Rensga\modules` on Windows.
* `<modules dir>/<id>-<version>/` the module, `downloads/` the cache, `installed.json` the records (id, version, host, root).
* Install: license check -> download with `curl` (resumable, cached, cancel flag, byte progress) -> sha256 (own
  implementation) -> unpack with `tar` (Windows 10+ `tar.exe`/bsdtar reads zip; on Linux a zip uses `bsdtar`, `unzip`
  or `cmake -E tar`) into a temp folder -> one atomic rename into place -> `check` -> `postInstall` -> record.
  A failure or cancel leaves no partial module folder; a partial download is kept and resumes.
* A wrong sha256 deletes the download. A manifest without a sha256 is refused.

## CLI

```
rensga modules list [--json]
rensga modules install <id>... [--accept-licenses]
rensga modules remove <id>          # refused while an installed module requires it
rensga modules verify <id>
rensga modules env <id> [--shell sh|cmd|json]
rensga modules path <id>
rensga modules doctor [--json]
```

`RENSGA_TOOLCHAIN=auto|native|docker` (default `auto`): native module if installed, else Docker if usable
(contributors), else the error "<Module name> is not installed. Install it from Hub > Modules or run:
rensga modules install <id>". `RENSGA_MODULE_CATALOG` points at another manifest folder.

## Adding a module

1. Find the upstream release asset (`curl https://api.github.com/repos/<owner>/<repo>/releases/tags/<tag>`), download it, compute `sha256sum`.
2. Write `manifest.d/<id>.json`, pin the version, keep the upstream license text in `notice`.
3. `rensga modules install <id>` on a dev machine with a throwaway `RENSGA_HOME`; `rensga modules verify <id>`.
4. Never add a console BIOS or other proprietary Sony file to a module. GPL tools (emulators) stay unmodified, run as
   separate processes, and the manifest links their source.

Tests: `tests/modules` (no network: archives are built at test time and served through `file://`).
