#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F2.G2: Linux rensga_headless vs the PSP simulation ELF (rensga_sim) in PPSSPPHeadless: same cooked pack, same input script
# (tests/determinism/walk.json), same frame count; the "rensga: hash <hex>" lines must match. Also checks the PSP alloc-check.
# Env: DET_FRAMES (default 400), DET_TRACE=1 (per-frame hashes; also on a mismatch the script reruns with objects), DET_OBJ_FRAME=N.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
FRAMES="${DET_FRAMES:-400}"; TRACE="${DET_TRACE:-0}"; OBJF="${DET_OBJ_FRAME:--1}"
CLI="$CI_OUT/linux-host/tools/cli/rensga"; HEAD="$CI_OUT/linux-host/engine/runtime/rensga_headless"
step "host build"; cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null
cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga rensga_headless
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || exit 1
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
PROJ="$REPO/templates/psp"; B="$CI_OUT/psp"; ROOT="$CI_OUT/psp-sim-root"; INPUT="$REPO/tests/determinism/walk.json"
step "cook"; "$CLI" cook "$PROJ" >/dev/null
PACK="$PROJ/Library/Cooked/psp/game.rpak"
step "psp sim build"
reflect_prescan "$B/reflect" "$PROJ"
rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$PROJ/Library/Generated/psp" -DRENSGA_EXIT_AFTER_FRAMES=240 >/dev/null
rensga_tc cmake --build "$B" --target rensga_sim -j"$JOBS"

run_psp() { # <trace> <objframe> <outlog>
  rm -rf "$ROOT"; mkdir -p "$ROOT"
  cp "$B/engine/runtime/rensga_sim.elf" "$PACK" "$ROOT/"; cp "$INPUT" "$ROOT/input.json"
  echo "frames=$FRAMES trace=$1 objframe=$2" > "$ROOT/sim.cfg"
  (cd "$ROOT" && timeout -s KILL 120 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_sim.elf" -r "$ROOT" --graphics=software --timeout=100 > "$3" 2>&1 || true)
  # the PSP log sink writes host0:/rensga.log (PPSSPPHeadless stdout may stay empty)
  [ -f "$ROOT/rensga.log" ] && cat "$ROOT/rensga.log" >> "$3"
}
run_lin() { "$HEAD" --pack "$PACK" --input "$INPUT" --frames "$FRAMES" --hash $( [ "$1" = 1 ] && echo --trace ) $( [ "$2" != -1 ] && echo --obj-frame "$2" ) > "$3" 2>&1; }

mkdir -p "$CI_OUT/determinism"; LA="$CI_OUT/determinism/linux.log"; PA="$CI_OUT/determinism/psp.log"
step "run"
run_lin "$TRACE" "$OBJF" "$LA"; run_psp "$TRACE" "$OBJF" "$PA"
grep -E "alloc-check|rensga: hash" "$LA" | sed 's/^/linux: /'
grep -E "alloc-check|rensga: hash" "$PA" | sed 's/^/psp:   /'
grep -qE "alloc-check frames=[1-9][0-9]* allocs=0" "$PA" || { echo "psp: no alloc-check allocs=0 line (see $PA)" >&2; exit 1; }
if tests/determinism/compare_hash.sh "$LA" "$PA"; then exit 0; fi
if [ "$TRACE" != 1 ]; then
  step "mismatch: rerun with per-frame trace"
  run_lin 1 -1 "$LA"; run_psp 1 -1 "$PA"; tests/determinism/compare_hash.sh "$LA" "$PA" || true
fi
exit 1
