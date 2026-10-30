#!/usr/bin/env bash
# F2.03: desktop hot reload. Starts rensga_desktop on a COPY of a cooked pack in a temp dir, replaces the copy once the game
# runs, and expects "rensga: hot-reload <pack>" in the log, a scene reload (objects>0) and, after it, alloc-check allocs=0.
# Usage: hot_reload.sh <rensga_desktop> <game.rpak>...   (the first pack that exists is used)
# Exit 0 ok, 77 skipped (no pack / no xvfb), 1 failed.
set -u
DESK="$1"; shift
PACK=""; for p in "$@"; do [ -f "$p" ] && { PACK="$p"; break; }; done
[ -n "$PACK" ] || { echo "hot_reload: no cooked pack (run the pipeline test / rensga cook first)"; exit 77; }
command -v xvfb-run >/dev/null || { echo "hot_reload: no xvfb-run, skipped"; exit 77; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cp "$PACK" "$T/game.rpak"
LOG="$T/log.txt"
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x720x24" "$DESK" --pack "$T/game.rpak" --frames 420 >"$LOG" 2>&1 &
PID=$!
for _ in $(seq 1 60); do grep -q 'rensga: frame 60 ' "$LOG" && break; sleep 0.5; done
grep -q 'rensga: frame 60 ' "$LOG" || { echo "hot_reload: the player never reached frame 60"; tail -5 "$LOG"; kill $PID 2>/dev/null; exit 1; }
cp "$PACK" "$T/game.rpak.new" && mv "$T/game.rpak.new" "$T/game.rpak"   # same bytes, new mtime: what the cooker's rename does
wait $PID; rc=$?
grep -E 'rensga: (hot-reload|alloc-check)' "$LOG"
[ $rc -eq 0 ] || { echo "hot_reload: player exited $rc"; tail -5 "$LOG"; exit 1; }
grep -q 'rensga: hot-reload .*game.rpak.*objects=[1-9]' "$LOG" || { echo "hot_reload: no hot-reload line"; exit 1; }
grep -qE 'rensga: alloc-check frames=[1-9][0-9]* allocs=0' "$LOG" || { echo "hot_reload: allocations after the reload"; exit 1; }
echo "hot_reload: ok"
