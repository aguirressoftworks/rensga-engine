# Third-party software

Every dependency, its license and where it comes from. Add a row when you add a dependency.

| Component | Version | License | Source | Used by | Vendored at |
| --- | --- | --- | --- | --- | --- |
| Dear ImGui (docking branch) | v1.93.0-docking | MIT | https://github.com/ocornut/imgui/tree/v1.93.0-docking | editor | `third_party/imgui/` (sources + SDL2/OpenGL3 backends, `LICENSE.txt`) |
| nlohmann/json | v3.11.3 | MIT | https://github.com/nlohmann/json/releases/tag/v3.11.3 | host tools (cooker, pipeline, CLI, editor) | `third_party/nlohmann_json/` (single header, `LICENSE.MIT`) |
| Inter | v4.0 (Regular, SemiBold) | SIL OFL 1.1 | https://github.com/rsms/inter/releases/tag/v4.0 | editor UI font | `third_party/fonts/Inter-*.ttf`, `Inter-OFL.txt` |
| JetBrains Mono | v2.304 (Regular) | SIL OFL 1.1 | https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304 | editor Console font | `third_party/fonts/JetBrainsMono-Regular.ttf`, `JetBrainsMono-OFL.txt` |
| stb_image_write | v1.16 (nothings/stb commit 31c1ad3) | MIT or public domain | https://github.com/nothings/stb/blob/31c1ad37456438565541f4919958214b6e762fb4/stb_image_write.h | editor `--screenshot` PNG output | `third_party/stb/stb_image_write.h` (license text is at the end of the header) |
| ImGuizmo | commit 105e4fd (CedricGuillemet/ImGuizmo, 2026-10-10; built against ImGui v1.93) | MIT | https://github.com/CedricGuillemet/ImGuizmo/tree/105e4fde45e4e74aae8973d98750820ef7aca7e2 | editor Scene view gizmos and orientation cube | `third_party/imguizmo/` (`ImGuizmo.cpp`, `ImGuizmo.h`, `LICENSE`) |
| SDL2 (sdl2-compat) | system package | zlib | https://libsdl.org | editor | not vendored |
| pspdev toolchain / PSPSDK | `pspdev/pspdev` Docker image (pinned by digest in `tools/docker/psp.sh`) | BSD-style / GPL parts | https://github.com/pspdev/pspdev | PSP builds | not vendored, runs in Docker |
| PPSSPP | pinned in `third_party/emulators/ppsspp/VERSION` | **GPL-2.0-or-later** | https://github.com/hrydgard/ppsspp | Play button, CLI `play` | separate process only, never linked. Binary git-ignored; `VERSION`, `LICENSE`, `SOURCE` committed |

Rules: PPSSPP and other emulators are GPL. They are shipped unmodified, as separate executables, with their license and a link to their source. No Sony BIOS or SDK files are ever added to this repository.
