#!/bin/bash
# F5.10: the Profiler panel parses the game's log lines (no emulator needed): 'rensga: prof', 'rensga: budget', 'rensga: gu draws', '[gu] vram',
# 'rensga: mem budget', 'rensga: psp clock' -> numbers (profilerdump) and a screenshot of the panel.
# Exit 77 = skipped (no xvfb-run or editor).
# usage: profiler_feed_ui.sh <rensga-editor> <project template dir> <work dir>
EDITOR=$1; TPL=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/cfg"; WORK=$(cd "$WORK" && pwd)
cp -r "$TPL" "$WORK/proj"; rm -rf "$WORK/proj/Library"
fail() { echo "FAIL: $*"; tail -20 "$WORK/run.log" | cut -c1-240; exit 1; }
EV=(profiler)
for i in 1 2 3 4 5 6 7 8; do
  EV+=("profilerfeed:rensga: prof frames=60 total_us=$((16600 + i * 300))/$((17000 + i * 500)) update_us=$((2000 + i * 100))/4000 render_us=$((5000 + i * 150))/7000 present_us=$((9000 + i * 20))/9500 audio_us=75/185")
done
EV+=("profilerfeed:rensga: budget scene=Demo draws=250/300 tris=21000/20000 textures=12/64 vram=1800000/2097152 status=over"
     "profilerfeed:rensga: gu draws=250 lights=3 batches=40 clipped=2 hwlights=3 tris=21000"
     "profilerfeed:rensga: mem budget=25165824 used=9000000 arena=20643840 peak=9100000"
     "profilerfeed:rensga: psp clock cpu=222 bus=111 (requested 222 MHz)"
     profilerdump "shot:$WORK/profiler_feed.png" quit)
args=(); for e in "${EV[@]}"; do args+=(--ui-event "$e"); done
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --screenshot "$WORK/end.png" --screenshot-frames 40000 "${args[@]}" > "$WORK/run.log" 2>&1
[ "$(grep -c '^profiler: feed parsed' "$WORK/run.log")" -eq 12 ] || fail "not every profiler line was parsed"
D=$(grep '^profiler: samples=' "$WORK/run.log" | tail -1)
echo "$D"
echo "$D" | grep -q 'samples=8 prof_lines=8' || fail "expected 8 samples"
echo "$D" | grep -q 'draws=250 tris=21000 vram=1800000/2097152 mem=9000000/25165824 cpu_mhz=222 source=console' || fail "budget / memory / clock fields wrong"
[ -s "$WORK/profiler_feed.png" ] || fail "no screenshot"
echo "profiler_feed_ui: ok (screenshot $WORK/profiler_feed.png)"
