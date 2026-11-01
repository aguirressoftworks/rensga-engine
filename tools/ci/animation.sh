#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job animation (F6.03 / F6.10): the two animation exercises on the desktop player and in PPSSPPHeadless.
#   samples/animator_player  idle / run / jump Animator driven by script parameters. The state changes ("rensga: animator_player: state=..
#                            step=..") must be the same on the desktop and on the PSP, the run must end with allocs=0.
#   samples/footsteps        footstep sounds from animation events on a running character: "rensga: footsteps: n=.. step=.. vol=.." every
#                            10 fixed steps while running, equal on the desktop and on the PSP; with SDL's disk audio driver the desktop's
#                            audio output is not silent (the step sound and the Music-group hum reach the mixer).
#
#   tools/ci/animation.sh [player|footsteps ...]   default: both; each runs desktop and PSP.   RENSGA_ANIM_SKIP_PSP=1 = desktop only
# Output: $RENSGA_CI_OUT/animation/ (logs, screenshots, summary.txt). Exit 77 when a needed tool is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
SAMPLES="${*:-player footsteps}"
for t in xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
OUT="$CI_OUT/animation"; mkdir -p "$OUT"; SUMMARY="$OUT/summary.txt"; : > "$SUMMARY"
XV=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x800x24")
fail() { echo "animation: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"

sample_dir() { case "$1" in player) echo "$REPO/samples/animator_player";; footsteps) echo "$REPO/samples/footsteps";; *) fail "unknown sample $1";; esac; }

host_tools() { # <project>
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$1" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_desktop 2>&1 | tail -1
}

lines() { grep -oE "$2" "$1" | sed 's/^INFO \[game\] //'; }

check_player() { # <log> <label>
  need "rensga: animator_player: done steps=420" "$1" "$2: the run did not finish"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$1" || fail "$2: alloc-check is not allocs=0 (see $1)"
  for s in Idle Run Jump; do need "rensga: animator_player: state=$s step=" "$1" "$2: the Animator never reached $s"; done
}
check_foot() { # <log> <label>
  need "rensga: footsteps: done steps=380" "$1" "$2: the run did not finish"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$1" || fail "$2: alloc-check is not allocs=0 (see $1)"
  python3 -I - "$1" "$2" <<'PY' || fail "$2: footstep events are not at the expected fixed steps (see $1)"
import re, sys
steps = [(int(m.group(1)), int(m.group(2)), float(m.group(3))) for m in re.finditer(r"rensga: footsteps: n=(\d+) step=(\d+) vol=([0-9.]+)", open(sys.argv[1]).read())]
if len(steps) < 25: sys.exit("only %d footstep events" % len(steps))
gaps = {b[1] - a[1] for a, b in zip(steps, steps[1:])}
if gaps != {10}: sys.exit("gaps between footsteps are %s, expected only 10 (tick 0 and tick 2 of a 4 tick clip at 12 fps on a 60 Hz step)" % sorted(gaps))
if [v for _, _, v in steps[:4]] != [1.0, 0.7, 1.0, 0.7]: sys.exit("volumes %s" % [v for _, _, v in steps[:4]])
print("%s: %d footsteps, first at step %d, one every 10 steps, volumes alternate 1.00 / 0.70" % (sys.argv[2], len(steps), steps[0][1]))
PY
}

for S in $SAMPLES; do
  PROJ="$(sample_dir "$S")"; HB="$CI_OUT/linux-host-anim-$S"; B="$CI_OUT/animation-build-$S"
  step "$S: desktop"
  host_tools "$PROJ"
  "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force | tail -1
  PACK="$PROJ/Library/Cooked/linux/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
  if [ "$S" = footsteps ]; then
    RAW="$OUT/footsteps_audio.raw"; rm -f "$RAW"
    env XDG_DATA_HOME="$(mktemp -d)" GALLIUM_DRIVER=softpipe SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$RAW" "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --frames 700 --screenshot "$OUT/footsteps.png" > "$OUT/desktop_$S.log" 2>&1 || true
    [ -s "$RAW" ] || fail "no audio was written by SDL's disk driver (see $OUT/desktop_$S.log)"
    python3 -I - "$RAW" <<'PY' | tee -a "$SUMMARY" || fail "desktop footsteps: the audio output is silent"
import struct, sys
d = open(sys.argv[1], "rb").read()
n = len(d) // 2
s = struct.unpack("<%dh" % n, d[:n * 2])
peak = max(abs(x) for x in s)
loud = sum(1 for x in s if abs(x) > 2000)
print("desktop footsteps audio: %d samples, peak %d, %d samples above 2000" % (n, peak, loud))
sys.exit(0 if peak > 4000 and loud > 2000 else 1)
PY
  else
    env XDG_DATA_HOME="$(mktemp -d)" GALLIUM_DRIVER=softpipe "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --frames 700 --screenshot "$OUT/player.png" > "$OUT/desktop_$S.log" 2>&1 || true
  fi
  if [ "$S" = player ]; then check_player "$OUT/desktop_$S.log" "desktop player"; else check_foot "$OUT/desktop_$S.log" "desktop footsteps" | tee -a "$SUMMARY"; fi
  if [ "${RENSGA_ANIM_SKIP_PSP:-0}" = 1 ]; then continue; fi

  step "$S: PPSSPPHeadless"
  command -v PPSSPPHeadless >/dev/null || { echo "skip: PPSSPPHeadless not installed"; exit 77; }
  "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$PROJ/Library/Generated/psp/console_target.gen.h" | tail -1
  PPACK="$PROJ/Library/Cooked/psp/game.rpak"; GENDIR="$PROJ/Library/Generated/psp"
  reflect_prescan "$B/reflect" "$PROJ"
  . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
  GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B/psp" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_EXIT_AFTER_FRAMES=900 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 \
    -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF >/dev/null
  rensga_tc cmake --build "$B/psp" --target rensga_game -j"$JOBS" 2>&1 | tail -1
  R="$OUT/psp_$S"; HH="$OUT/psp_home_$S"; rm -rf "$R" "$HH"; mkdir -p "$R" "$HH"
  cp "$B/psp/engine/runtime/rensga_game.elf" "$PPACK" "$R/"
  ( cd "$R" && HOME="$HH" timeout -s KILL 300 "${XV[@]}" PPSSPPHeadless "$R/rensga_game.elf" -r "$R" -l --graphics=software --timeout=240 > "$OUT/psp_$S.emu" 2>&1 || true )
  cp "$R/rensga.log" "$OUT/psp_$S.log" 2>/dev/null || : > "$OUT/psp_$S.log"
  if [ "$S" = player ]; then
    check_player "$OUT/psp_$S.log" "psp player"
    diff <(lines "$OUT/desktop_$S.log" 'rensga: animator_player: state=[A-Za-z]+ step=[0-9]+') <(lines "$OUT/psp_$S.log" 'rensga: animator_player: state=[A-Za-z]+ step=[0-9]+') > /dev/null \
      || fail "the Animator state changes differ between the desktop and the PSP (diff $OUT/desktop_$S.log $OUT/psp_$S.log)"
    echo "player: Animator state changes equal on the desktop and in PPSSPPHeadless:" | tee -a "$SUMMARY"
    lines "$OUT/psp_$S.log" 'rensga: animator_player: (state=[A-Za-z]+ step=[0-9]+|done steps=[0-9]+ x=[0-9]+ changes=[0-9]+)' | sed 's/^/  /' | tee -a "$SUMMARY"
  else
    check_foot "$OUT/psp_$S.log" "psp footsteps" | tee -a "$SUMMARY"
    diff <(lines "$OUT/desktop_$S.log" 'rensga: footsteps: n=[0-9]+ step=[0-9]+ vol=[0-9.]+') <(lines "$OUT/psp_$S.log" 'rensga: footsteps: n=[0-9]+ step=[0-9]+ vol=[0-9.]+') > /dev/null \
      || fail "the footstep events differ between the desktop and the PSP (diff $OUT/desktop_$S.log $OUT/psp_$S.log)"
    echo "footsteps: events equal on the desktop and in PPSSPPHeadless" | tee -a "$SUMMARY"
  fi
done
echo "animation: all requested samples passed"
