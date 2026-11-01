#!/usr/bin/env bash
unset WAYLAND_DISPLAY
# F6.06 exercise (Editor 12): samples/frame_spike on a PSP development build in PPSSPPHeadless, twice: scene Spiky (one 1500000-iteration job
# inside a single Update every 120 frames) and scene Smooth (the same job over 40 frames). The runtime streams one "rensga: pf" line per frame;
# tools/ci/prof_stats.py turns them into the numbers the Profiler window shows (median / p99 / max frame, frames over 1.5 x budget, the worst
# frame's samples). PPSSPPHeadless time is EMULATOR time (your PC running the emulated PSP): read the ratio and the shape, not the absolute ms.
#   tools/ci/frame_spike.sh          Env: RENSGA_CLI=<rensga binary> (default build/host/tools/cli/rensga), RENSGA_CI_OUT.
# Output: $RENSGA_CI_OUT/frame-spike/{spiky,smooth}.log and summary.txt. Exit 77 without the psp-build module or PPSSPP.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
CLI="${RENSGA_CLI:-$REPO/build/host/tools/cli/rensga}"
[ -x "$CLI" ] || { echo "skip: no rensga CLI at $CLI (cmake --build build/host --target rensga)"; exit 77; }
"$CLI" modules path psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed"; exit 77; }
OUT="$CI_OUT/frame-spike"; rm -rf "$OUT"; mkdir -p "$OUT"; SUMMARY="$OUT/summary.txt"; : > "$SUMMARY"
export RENSGA_TOOLCHAIN="${RENSGA_TOOLCHAIN:-native}" RENSGA_JOBS="${RENSGA_JOBS:-2}"
fail() { echo "frame_spike: $*" >&2; exit 1; }
P="$OUT/proj"; cp -r "$REPO/samples/frame_spike" "$P"; rm -rf "$P/Library" "$P/Builds"   # one project, one engine build: only the scene order changes
for mode in spiky smooth; do
  if [ "$mode" = smooth ]; then   # the fix, done the way a user does it in Build Settings: Smooth moves above Spiky
    python3 -I - "$P" <<'PY'
import json, sys
p = sys.argv[1] + "/ProjectSettings/Project.json"
j = json.load(open(p)); j["scenes"] = list(reversed(j["scenes"])); json.dump(j, open(p, "w"), indent=1)
PY
  fi
  "$CLI" build --platform psp --development --run --headless --show-prof "$P" > "$OUT/$mode.log" 2>&1 || { tail -20 "$OUT/$mode.log"; fail "$mode: the PSP run failed (see $OUT/$mode.log)"; }
  grep -aq "^\[game\].*rensga: spike: mode=$mode frames=730" "$OUT/$mode.log" || fail "$mode: the game did not finish its run (see $OUT/$mode.log)"
  grep -E "alloc-check" "$OUT/$mode.log" | grep -q "allocs=0" || fail "$mode: alloc-check is not allocs=0"
  python3 -I "$REPO/tools/ci/prof_stats.py" "$OUT/$mode.log" --label "$mode" --skip 10 | tee -a "$SUMMARY"
  grep -a "^\[game\].*rensga: spike:" "$OUT/$mode.log" | sed "s/^.*rensga: /rensga: /" | tee -a "$SUMMARY"
done
rm -rf "$P"
echo "frame_spike: summary in $SUMMARY"
