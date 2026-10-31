#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F2.G2: Linux rensga_headless vs the PSP simulation ELF (rensga_sim) in PPSSPPHeadless: same cooked pack, same input script
# (tests/determinism/walk.json), same frame count; the "rensga: hash <hex>" lines must match. Also checks the PSP alloc-check.
# Env: DET_FRAMES (default 400; 2700 in 2d mode), DET_TIMEOUT (PPSSPP seconds; 100, 300 in 2d mode), DET_TRACE=1 (per-frame hashes; also on a mismatch the script reruns with objects), DET_OBJ_FRAME=N.
#   tools/ci/determinism.sh [3d|2d]   (default 3d)
# 2d mode (F4.07): the 2D slice samples/platformer2d_psp, a copy of it whose first scene is TitleBot (the bot plays the whole game, no input
# script), the same fixed 1/60 s step on both sides (the emulator and desktop players are wall-clock driven, so their game times differ).
# The hash covers every object's transform and the pickups, i.e. the whole game state of the bot run.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODE="${1:-3d}"; [ "$MODE" = 3d ] || [ "$MODE" = 2d ] || { echo "usage: determinism.sh [3d|2d]" >&2; exit 2; }
OBJF="${DET_OBJ_FRAME:--1}"
# 2d: a bot run ends on the static Results scene, so the final hash alone proves little: the per-frame trace is always compared.
if [ "$MODE" = 2d ]; then FRAMES="${DET_FRAMES:-2700}"; TMO="${DET_TIMEOUT:-300}"; TRACE="${DET_TRACE:-1}"
else FRAMES="${DET_FRAMES:-400}"; TMO="${DET_TIMEOUT:-100}"; TRACE="${DET_TRACE:-0}"; fi
HOST_PATH="$PATH"   # the module's cmake (3.28) cannot configure the host tree: host cmake runs with the PATH from before the module env
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || exit 1
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
if [ "$MODE" = 3d ]; then
  CLI="$CI_OUT/linux-host/tools/cli/rensga"; HEAD="$CI_OUT/linux-host/engine/runtime/rensga_headless"
  step "host build"; env PATH="$HOST_PATH" cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null
  env PATH="$HOST_PATH" cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga rensga_headless
  PROJ="$REPO/templates/psp-3d"; B="$CI_OUT/psp"; ROOT="$CI_OUT/psp-sim-root"; INPUT="$REPO/tests/determinism/walk.json"
  step "cook"; "$CLI" cook "$PROJ" >/dev/null
  PACK="$PROJ/Library/Cooked/psp/game.rpak"; SIMDEFS=(-DRENSGA_EXIT_AFTER_FRAMES=240); GENDIR="$PROJ/Library/Generated/psp"; SRCPROJ="$PROJ"
else
  SRCPROJ="${DET_2D_PROJECT:-$REPO/samples/platformer2d_psp}"; HB="$CI_OUT/linux-host-2d"; HEAD="$HB/engine/runtime/rensga_headless"
  B="$CI_OUT/psp-sim-2d"; ROOT="$CI_OUT/psp-sim-2d-root"; INPUT=""
  step "host build (RENSGA_PROJECT_DIR=samples/platformer2d_psp)"
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$SRCPROJ" >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless
  # A copy whose first scene is TitleBot: both sides boot the first scene of the pack, so no --scene option is needed.
  PROJ="$CI_OUT/determinism-2d/project"; rm -rf "$PROJ"; mkdir -p "$PROJ"
  cp -r "$SRCPROJ/Assets" "$SRCPROJ/ProjectSettings" "$SRCPROJ/Streaming" "$PROJ/"
  python3 -I - "$PROJ/ProjectSettings/Project.json" "$PROJ/Assets/Scenes/TitleBot.scene.meta" <<'PY'
import json, sys
proj, meta = sys.argv[1], sys.argv[2]
g = json.load(open(meta))["guid"]
d = json.load(open(proj))
d["scenes"] = [g] + [s for s in d["scenes"] if s != g]
json.dump(d, open(proj, "w"), indent=1)
PY
  step "cook for psp"
  "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --gen-header "$PROJ/Library/Generated/psp/console_target.gen.h" | tail -2
  PACK="$PROJ/Library/Cooked/psp/game.rpak"; SIMDEFS=(); GENDIR="$PROJ/Library/Generated/psp"
fi
step "psp sim build"
reflect_prescan "$B/reflect" "$SRCPROJ"
rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$SRCPROJ" -DRENSGA_GENERATED_DIR="$GENDIR" "${SIMDEFS[@]}" >/dev/null
rensga_tc cmake --build "$B" --target rensga_sim -j"$JOBS"

run_psp() { # <trace> <objframe> <outlog>
  rm -rf "$ROOT"; mkdir -p "$ROOT"
  cp "$B/engine/runtime/rensga_sim.elf" "$PACK" "$ROOT/"; if [ -n "$INPUT" ]; then cp "$INPUT" "$ROOT/input.json"; fi
  echo "frames=$FRAMES trace=$1 objframe=$2" > "$ROOT/sim.cfg"
  (cd "$ROOT" && timeout -s KILL $((TMO + 30)) env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_sim.elf" -r "$ROOT" --graphics=software --timeout=$TMO > "$3" 2>&1 || true)
  # the PSP log sink writes host0:/rensga.log (PPSSPPHeadless stdout may stay empty)
  [ -f "$ROOT/rensga.log" ] && cat "$ROOT/rensga.log" >> "$3"
}
run_lin() { local sd; sd="$CI_OUT/determinism/linux-saves"; rm -rf "$sd"; mkdir -p "$sd"
  "$HEAD" --pack "$PACK" $( [ -n "$INPUT" ] && echo --input "$INPUT" ) --save-dir "$sd" --frames "$FRAMES" --hash $( [ "$1" = 1 ] && echo --trace ) $( [ "$2" != -1 ] && echo --obj-frame "$2" ) > "$3" 2>&1; }

mkdir -p "$CI_OUT/determinism"; LA="$CI_OUT/determinism/linux.log"; PA="$CI_OUT/determinism/psp.log"
step "run"
run_lin "$TRACE" "$OBJF" "$LA"; run_psp "$TRACE" "$OBJF" "$PA"
grep -E "alloc-check|rensga: hash" "$LA" | sed 's/^/linux: /'
grep -E "alloc-check|rensga: hash" "$PA" | sed 's/^/psp:   /'
grep -qE "alloc-check frames=[1-9][0-9]* allocs=0" "$PA" || { echo "psp: no alloc-check allocs=0 line (see $PA)" >&2; exit 1; }
hint() { [ "$MODE" = 2d ] && echo "hint: scripts of equal EXECUTION_ORDER update in type-registration order (static init order, which differs per build): give the sample scripts distinct EXECUTION_ORDER, or break ties in World::ensurePool by type hash" >&2; return 0; }
trace() { grep -oE 'rensga: trace f=[0-9]+ h=[0-9a-f]{8}' "$1"; }
if tests/determinism/compare_hash.sh "$LA" "$PA"; then
  if [ "$TRACE" = 1 ]; then
    n="$(trace "$LA" | wc -l)"
    if [ "$n" -gt 0 ] && diff <(trace "$LA") <(trace "$PA") >/dev/null; then echo "determinism: per-frame trace equal over $n frames"
    else
      echo "determinism: final hashes match but the per-frame traces differ:" >&2
      diff <(trace "$LA") <(trace "$PA") | grep -m1 '^[<>]' >&2 || echo "  (no trace lines in a log)" >&2
      hint; exit 1
    fi
  fi
  exit 0
fi
if [ "$TRACE" != 1 ]; then
  step "mismatch: rerun with per-frame trace"
  run_lin 1 -1 "$LA"; run_psp 1 -1 "$PA"; tests/determinism/compare_hash.sh "$LA" "$PA" || true
fi
hint; exit 1
