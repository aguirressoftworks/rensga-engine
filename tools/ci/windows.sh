#!/usr/bin/env bash
# CI job windows (F4.01): cross-build rensga_hello.exe + rensga_desktop.exe of the 2D SLICE (samples/platformer2d: its scripts are
# compiled into the player) with the windows-build module (llvm-mingw + SDL2), and cook the slice pack in this job (host cooker with the
# same scripts, ps2 profile = 640x448: the Windows, Linux and Android players share it). The pack and the streamed music are copied next
# to the exe, so build/ci/windows/engine/runtime/ is a playable folder: rensga_desktop.exe starts the game.
# No Docker needed. RENSGA_TOOLCHAIN=docker uses tools/docker/windows.sh (MinGW-w64 GCC) instead.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="$CI_OUT/windows"
step "cook the slice (host cooker, system cmake)"; slice_cook ps2
. "$REPO/tools/modules/toolchain_env.sh" windows-build windows
reflect_prescan "$B/reflect" "$SLICE_PROJ"
if [ "$RENSGA_TOOLCHAIN_BACKEND" = native ]; then
  GEN="-G Ninja"; RT="\$SDL2_MINGW_ROOT/bin/SDL2.dll"
else
  GEN=""; RT="\$SDL2_MINGW_ROOT/bin/SDL2.dll /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll"
fi
rensga_tc sh -c "cmake --preset windows $GEN -B '$B' -DRENSGA_REFLECT_DIR='$B/reflect' -DRENSGA_PROJECT_DIR='$SLICE_PROJ' >/dev/null && cmake --build '$B' -j$JOBS --target rensga_hello --target rensga_desktop && cp $RT '$B/engine/runtime/'"
PACKDIR="$(dirname "$(SLICE_PACK ps2)")"
cp "$PACKDIR/game.rpak" "$PACKDIR"/*.wav "$B/engine/runtime/"
ls -la "$B/engine/runtime/rensga_hello.exe" "$B/engine/runtime/rensga_desktop.exe" "$B/engine/runtime/SDL2.dll" "$B/engine/runtime/game.rpak"
