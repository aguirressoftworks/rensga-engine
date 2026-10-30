#!/usr/bin/env bash
# CI job windows: cross-build rensga_hello.exe + rensga_desktop.exe with the windows-build module (llvm-mingw + SDL2).
# No Docker needed. RENSGA_TOOLCHAIN=docker uses tools/docker/windows.sh (MinGW-w64 GCC) instead.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
. "$REPO/tools/modules/toolchain_env.sh" windows-build windows
cd "$REPO"; B="$CI_OUT/windows"
reflect_prescan "$B/reflect"
if [ "$RENSGA_TOOLCHAIN_BACKEND" = native ]; then
  GEN="-G Ninja"; RT="\$SDL2_MINGW_ROOT/bin/SDL2.dll"
else
  GEN=""; RT="\$SDL2_MINGW_ROOT/bin/SDL2.dll /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll"
fi
rensga_tc sh -c "cmake --preset windows $GEN -B '$B' -DRENSGA_REFLECT_DIR='$B/reflect' >/dev/null && cmake --build '$B' -j$JOBS --target rensga_hello --target rensga_desktop && cp $RT '$B/engine/runtime/'"
ls -la "$B/engine/runtime/rensga_hello.exe" "$B/engine/runtime/rensga_desktop.exe" "$B/engine/runtime/SDL2.dll"
