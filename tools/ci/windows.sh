#!/usr/bin/env bash
# CI job windows: cross-build rensga_hello.exe with MinGW-w64 + SDL2 (build only; run under wine only if installed).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="$CI_OUT/windows"
reflect_prescan "$B/reflect"
tools/docker/windows.sh sh -c "cmake --preset windows -B '$B' -DRENSGA_REFLECT_DIR='$B/reflect' >/dev/null && cmake --build '$B' -j$JOBS --target rensga_hello --target rensga_desktop && cp \$SDL2_MINGW_ROOT/bin/SDL2.dll '$B/engine/runtime/'"
ls -la "$B/engine/runtime/rensga_hello.exe" "$B/engine/runtime/SDL2.dll"
