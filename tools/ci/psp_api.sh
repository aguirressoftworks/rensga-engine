#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job psp_api (F5.18): the PSP API tests (tests/psp_api/ApiTests.cpp) run as a game on the PSP in PPSSPPHeadless and on the host, from one
# source. The test project is generated (tests/psp_api/make_project.py) into $RENSGA_CI_OUT/psp-api-project; assets come from samples/psp3d_walk.
#
#   tools/ci/psp_api.sh [mode...]       default: all
#     host     cook for linux, run rensga_headless: every "psp_api: PASS" and no FAIL
#     psp      cook for psp, PSP ELF, PPSSPPHeadless (software): the same; the list of passed checks must equal the host list
#     fail     the scene ApiTestsFail adds a check that is wrong on the PSP only (sizeof(long) == 8): it must PASS on the host and FAIL on the PSP,
#              and this script must exit non-zero for the PSP run (proved here by running the check with the expectation inverted)
#     all      host psp fail
# Output: $RENSGA_CI_OUT/psp-api/ (logs). Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing (host mode needs none of them).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-all}"; [ "$MODES" = all ] && MODES="host psp fail"
OUT="$CI_OUT/psp-api"; PROJ="$CI_OUT/psp-api-project"; HB="$CI_OUT/linux-host-api"; B="$CI_OUT/psp-api-build"; ROOT="$OUT/root"
FRAMES=90
mkdir -p "$OUT" "$OUT/hostsave"
fail() { echo "psp_api: $*" >&2; exit 1; }
passes() { grep -E "psp_api: PASS " "$1" | sed 's/.*psp_api: PASS //' | sort; }
needall() { # <log> <label>: DONE line, no FAIL, at least 20 PASS
  grep -qE "psp_api: DONE pass=[0-9]+ fail=0 " "$1" || { grep -E "psp_api: (FAIL|DONE)" "$1" >&2 || true; fail "$2: not all checks passed (see $1)"; }
  ! grep -q "psp_api: FAIL" "$1" || fail "$2: FAIL line (see $1)"
  [ "$(passes "$1" | wc -l)" -ge 20 ] || fail "$2: fewer than 20 PASS lines (see $1)"
  grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$1" || fail "$2: no alloc-check allocs=0 (see $1)"
}

step "project"
python3 -I "$REPO/tests/psp_api/make_project.py" "$PROJ" >/dev/null
step "host tree (RENSGA_PROJECT_DIR=$PROJ)"
HOSTPATH="$PATH"
cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless rensga_desktop 2>&1 | tail -2

host_run() { # <label> <scene or empty> : fills $OUT/host_<label>.log
  local l="$1" sc="${2:-}" log="$OUT/host_$1.log"
  "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
  local pack="$PROJ/Library/Cooked/linux/game.rpak"
  [ -s "$pack" ] || fail "no linux pack"
  if [ -z "$sc" ]; then
    "$HB/engine/runtime/rensga_headless" --pack "$pack" --frames $FRAMES --save-dir "$OUT/hostsave" > "$log" 2>&1 || fail "host $l: rensga_headless exit $?"
  else
    env -u WAYLAND_DISPLAY XDG_DATA_HOME="$OUT/hostsave" GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 xvfb-run -a \
      "$HB/engine/runtime/rensga_desktop" --pack "$pack" --scene "$sc" --frames $FRAMES > "$log" 2>&1 || fail "host $l: rensga_desktop exit $?"
  fi
}

want_psp=0; echo " $MODES " | grep -qE " (psp|fail) " && want_psp=1
if [ "$want_psp" = 1 ]; then
  for t in PPSSPPHeadless xvfb-run; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
  . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
  GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
  GENDIR="$PROJ/Library/Generated/psp"
  step "cook for psp"
  "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" | tail -2
  PACK="$PROJ/Library/Cooked/psp/game.rpak"
  reflect_prescan "$B/reflect" "$PROJ"
  EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)
  psp_run() { # <label> <autostart scene or empty>
    local l="$1" sc="${2:-}"
    rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
      -DRENSGA_PSP_AUTOSTART_SCENE="$sc" -DRENSGA_EXIT_AFTER_FRAMES=$FRAMES -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
      -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF >/dev/null
    rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
    rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
    ( cd "$ROOT" && timeout -s KILL 180 "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=software --timeout=90 > "$OUT/psp_$l.log.emu" 2>&1 || true )
    cp "$ROOT/rensga.log" "$OUT/psp_$l.log" 2>/dev/null || : > "$OUT/psp_$l.log"
    grep -q "rensga: boot" "$OUT/psp_$l.log" || fail "psp $l: the game did not start (see $OUT/psp_$l.log.emu)"
  }
fi

for m in $MODES; do
  case "$m" in
    host)
      step "host: rensga_headless, $FRAMES frames"
      host_run ok ""
      needall "$OUT/host_ok.log" "host"
      grep -E "psp_api: (begin|DONE|SKIP)" "$OUT/host_ok.log" | sed 's/^/  /'
      echo "psp_api host: OK ($(passes "$OUT/host_ok.log" | wc -l) checks)";;
    psp)
      [ -s "$OUT/host_ok.log" ] || { host_run ok ""; }
      step "psp: PPSSPPHeadless (software), $FRAMES frames"
      psp_run ok ""
      needall "$OUT/psp_ok.log" "psp"
      grep -E "psp_api: (begin|DONE|SKIP)" "$OUT/psp_ok.log" | sed 's/^/  /'
      # checks named *_psp_* run on the PSP only (the host has no PSP memory to report); every other check must pass on both
if ! diff <(passes "$OUT/host_ok.log") <(passes "$OUT/psp_ok.log" | grep -v '_psp_') > "$OUT/passes.diff"; then cat "$OUT/passes.diff" >&2; fail "psp and host passed different checks"; fi
      echo "psp_api psp: OK ($(passes "$OUT/psp_ok.log" | wc -l) checks; all $(passes "$OUT/host_ok.log" | wc -l) host checks pass here too, plus the PSP-only ones)";;
    fail)
      step "fail demo: ApiTestsFail (sizeof(long) == 8) on the host, then on the PSP"
      host_run fail ApiTestsFail
      grep -q "psp_api: PASS demo_long_is_64_bit" "$OUT/host_fail.log" || fail "fail demo: the check should PASS on the host (see $OUT/host_fail.log)"
      psp_run fail ApiTestsFail
      if grep -q "psp_api: FAIL demo_long_is_64_bit" "$OUT/psp_fail.log"; then
        grep -E "psp_api: FAIL" "$OUT/psp_fail.log" | sed 's/^/  caught: /'
        # the regular verdict of this script on that log must be a failure
        if ( needall "$OUT/psp_fail.log" "psp(fail demo)" ) 2>/dev/null; then fail "fail demo: the verdict should be FAIL"; fi
        echo "psp_api fail: OK (the PSP-only failure is caught: PASS on the host, FAIL on the PSP, verdict FAIL)"
      else
        fail "fail demo: the PSP run did not report FAIL demo_long_is_64_bit (see $OUT/psp_fail.log)"
      fi;;
    *) fail "unknown mode $m";;
  esac
done
