#!/usr/bin/env bash
# F2.04: desktop --audio-stress (16 looping effects + music) through the SDL "disk" audio driver. Expects no underruns,
# alloc-check allocs=0 and a device output file that is not silent.
# Usage: audio_stress.sh <rensga_desktop> <game.rpak>...   (the first pack that exists is used)
# Exit 0 ok, 77 skipped (no pack / no xvfb), 1 failed.
set -u
DESK="$1"; shift
PACK=""; for p in "$@"; do [ -f "$p" ] && { PACK="$p"; break; }; done
[ -n "$PACK" ] || { echo "audio_stress: no cooked pack (run the pipeline test / rensga cook first)"; exit 77; }
command -v xvfb-run >/dev/null || { echo "audio_stress: no xvfb-run, skipped"; exit 77; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
cp "$PACK" "$T/game.rpak"
LOG="$T/log.txt"
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$T/out.raw" \
  xvfb-run -a -s "-screen 0 1280x720x24" "$DESK" --pack "$T/game.rpak" --audio-stress --frames 240 >"$LOG" 2>&1
rc=$?
grep -E 'rensga: (audio-stress|alloc-check)' "$LOG"
[ $rc -eq 0 ] || { echo "audio_stress: player exited $rc"; tail -5 "$LOG"; exit 1; }
grep -q 'rensga: audio-stress voices=16 .*underruns=0' "$LOG" || { echo "audio_stress: no stress line or underruns"; exit 1; }
grep -qE 'rensga: alloc-check frames=[1-9][0-9]* allocs=0' "$LOG" || { echo "audio_stress: allocations while mixing"; exit 1; }
[ -s "$T/out.raw" ] || { echo "audio_stress: the audio device wrote nothing"; exit 1; }
[ -n "$(tr -d '\0' < "$T/out.raw" | head -c 1)" ] || { echo "audio_stress: device output is silent"; exit 1; }
echo "audio_stress: ok"
