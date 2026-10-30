#!/usr/bin/env bash
# F2.G1 (Linux): the headless sim and the desktop player must log "rensga: alloc-check frames=<N> allocs=0".
# Usage: check_alloc.sh <rensga_headless> <rensga_desktop|-> <game.rpak> <input.json>
# Exit 0 ok, 77 skipped (no pack / no xvfb for the desktop part), 1 failed.
set -u
HEAD="$1"; DESK="$2"; PACK="$3"; INPUT="$4"
[ -f "$PACK" ] || { echo "no cooked pack at $PACK (run the pipeline test / rensga cook first)"; exit 77; }
check() { # <label> <log>
  line="$(echo "$2" | grep -E '^(INFO .*)?.*rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' | tail -1)"
  echo "$1: ${line:-<no alloc-check line>}"
  echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0$' || { echo "$1: alloc-check failed" >&2; return 1; }
}
rc=0
out="$("$HEAD" --pack "$PACK" --input "$INPUT" --frames 400 2>&1)" || { echo "$out" | tail -5; rc=1; }
check headless "$out" || rc=1
if [ "$DESK" != "-" ]; then
  if command -v xvfb-run >/dev/null; then
    out="$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x720x24" "$DESK" --pack "$PACK" --frames 300 2>&1)" || { echo "$out" | tail -5; rc=1; }
    check desktop "$out" || rc=1
  else
    echo "desktop: skipped (no xvfb-run)"
  fi
fi
exit $rc
