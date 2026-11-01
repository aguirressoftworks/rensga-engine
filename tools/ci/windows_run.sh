#!/usr/bin/env bash
# F4.G2 (Windows): runs the cross-built rensga_desktop.exe of the 2D slice under wine inside Xvfb with the pack tools/ci/windows.sh
# cooked, starts the TitleBot scene (the bot plays the whole game, no pad needed) and requires
#   "rensga: game complete coins=<n>" and "rensga: alloc-check frames=<N> allocs=0" (and no texture upload failure).
# GALLIUM_DRIVER=softpipe: llvmpipe's shader JIT allocates through operator new and would trip the alloc check; softpipe does not.
# Saves land in the wine prefix (SDL_GetPrefPath); the bot always starts from New Game. A screenshot of the final frame is written to
# $CI_OUT/windows/windows_slice.png. Needs tools/ci/windows.sh to have run. Exit 77 when wine/xvfb/exe/pack is missing.
# Env: WINE_FRAMES (default 1500: ~35 s of game time at softpipe's ~60 ms frames), WINE_TIMEOUT (default 420 s).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"  # native llvm-mingw build is static; only SDL2.dll is needed next to the exe
D="$CI_OUT/windows/engine/runtime"
command -v wine >/dev/null && command -v xvfb-run >/dev/null || { echo "windows_run: wine/xvfb-run missing, skipped"; exit 77; }
[ -f "$D/rensga_desktop.exe" ] && [ -f "$D/game.rpak" ] || { echo "windows_run: build windows first (tools/ci/windows.sh builds the exe and cooks the slice), skipped"; exit 77; }
wine_env  # WINEDLLOVERRIDES=mscoree,mshtml= (no mono / gecko prompts), WINEPREFIX, WINEDEBUG
trap 'wine_stop' EXIT
SHOT="$CI_OUT/windows/windows_slice.png"; rm -f "$SHOT"
out="$(cd "$D" && nolockfd env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe LIBGL_ALWAYS_SOFTWARE=1 timeout -s KILL "${WINE_TIMEOUT:-420}" xvfb-run -a -s "-screen 0 1280x720x24" \
  wine rensga_desktop.exe --pack game.rpak --scene TitleBot --frames "${WINE_FRAMES:-1500}" --screenshot "Z:${SHOT}" 2>&1)"
echo "$out" | grep -a -E "game complete|bot finished|alloc-check|quit after|screenshot|ERROR" | tail -8
rc=0
echo "$out" | grep -a -q "rensga: game complete coins=[0-9]" || { echo "windows (wine): the bot did not complete the game" >&2; echo "$out" | tail -6 >&2; rc=1; }
echo "$out" | grep -a -E "upload failed|ERROR \[(gl|game)\]" >/dev/null && { echo "windows (wine): errors in the log" >&2; echo "$out" | grep -a -E "upload failed|ERROR \[" | head -5 >&2; rc=1; }
line="$(echo "$out" | grep -a -oE 'rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' | tail -1)"
echo "windows (wine): ${line:-<no alloc-check line>}"
echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0$' || rc=1
[ -s "$SHOT" ] && echo "windows (wine): screenshot $SHOT" || echo "windows (wine): no screenshot" >&2
exit $rc
