#!/usr/bin/env bash
# F2.G1 (Windows): runs the cross-built rensga_desktop.exe under wine inside Xvfb with the cooked pack and requires
# "rensga: alloc-check frames=<N> allocs=0". Needs tools/ci/windows.sh to have run. Exit 77 when wine/xvfb/exe/pack is missing.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"  # native llvm-mingw build is static; only SDL2.dll is needed next to the exe
D="$CI_OUT/windows/engine/runtime"; PACK="$REPO/templates/psp-3d/Library/Cooked/psp/game.rpak"
command -v wine >/dev/null && command -v xvfb-run >/dev/null || { echo "windows_run: wine/xvfb-run missing, skipped"; exit 77; }
[ -f "$D/rensga_desktop.exe" ] && [ -f "$PACK" ] || { echo "windows_run: build windows + cook first, skipped"; exit 77; }
W="$(mktemp -d)"; trap 'wineserver -k >/dev/null 2>&1; rm -rf "$W"' EXIT
cp "$PACK" "$W/game.rpak"
export WINEPREFIX="${WINE_PREFIX:-$REPO/build/wine-prefix}" WINEDEBUG=-all WINEARCH=win64 WINEDLLOVERRIDES="mscoree,mshtml="
out="$(cd "$D" && env -u WAYLAND_DISPLAY timeout -s KILL "${WINE_TIMEOUT:-420}" xvfb-run -a -s "-screen 0 1280x720x24" wine rensga_desktop.exe --pack "Z:${W}/game.rpak" --frames 200 2>&1)"
echo "$out" | tail -4
line="$(echo "$out" | grep -a -oE 'rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' | tail -1)"
echo "windows (wine): ${line:-<no alloc-check line>}"
echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0$'
