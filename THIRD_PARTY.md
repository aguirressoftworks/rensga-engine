# Third-party software

Every dependency, its license and where it comes from. Add a row when you add a dependency.

| Component | Version | License | Source | Used by | Vendored at |
| --- | --- | --- | --- | --- | --- |
| Dear ImGui (docking branch) | v1.93.0-docking | MIT | https://github.com/ocornut/imgui/tree/v1.93.0-docking | editor | `third_party/imgui/` (sources + SDL2/OpenGL3 backends, `LICENSE.txt`) |
| nlohmann/json | v3.11.3 | MIT | https://github.com/nlohmann/json/releases/tag/v3.11.3 | host tools (cooker, pipeline, CLI, editor) | `third_party/nlohmann_json/` (single header, `LICENSE.MIT`) |
| SDL2 (sdl2-compat) | system package | zlib | https://libsdl.org | editor | not vendored |
| pspdev toolchain / PSPSDK | `pspdev/pspdev` Docker image (pinned by digest in `tools/docker/psp.sh`) | BSD-style / GPL parts | https://github.com/pspdev/pspdev | PSP builds | not vendored, runs in Docker |
| PPSSPP | pinned in `third_party/emulators/ppsspp/VERSION` | **GPL-2.0-or-later** | https://github.com/hrydgard/ppsspp | Play button, CLI `play` | separate process only, never linked. Binary git-ignored; `VERSION`, `LICENSE`, `SOURCE` committed |

Rules: PPSSPP and other emulators are GPL. They are shipped unmodified, as separate executables, with their license and a link to their source. No Sony BIOS or SDK files are ever added to this repository.
