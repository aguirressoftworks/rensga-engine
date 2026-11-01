#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F2.G2: Linux rensga_headless vs the PSP simulation ELF (rensga_sim) in PPSSPPHeadless: same cooked pack, same input script
# (tests/determinism/walk.json), same frame count; the "rensga: hash <hex>" lines must match. Also checks the PSP alloc-check.
# Env: DET_FRAMES (default 400; 2700 in 2d mode), DET_TIMEOUT (PPSSPP seconds; 100, 300 in 2d mode), DET_TRACE=1 (per-frame hashes; also on a mismatch the script reruns with objects), DET_OBJ_FRAME=N.
#   tools/ci/determinism.sh [3d|2d|3dlit|phys|anim]   (default 3d)
# anim mode (F6.03): samples/animator_player (or DET_ANIM_PROJECT=<dir>), a bot that drives an Animator (idle/run/jump property clips with
# cross-fades, transform and sprite curves) on the fixed step, run on both sides. Besides the state hash and the per-frame trace, every
# "rensga: anim f=<step> h=<hash>" line (Animator states, times, parameters and the property values written) must be equal.
# phys mode (F6.08): a generated 3D physics project (tests/determinism/make_phys_project.py: falling Rigidbody crates, spheres and a capsule on an
# oriented ramp, a kinematic platform, a CharacterController, layer-matrix ghosts, raycast / spherecast / overlap probes) run on both sides; the state hash
# and the per-frame trace must match.
# 3dlit mode (F5.14): a shared-3D pack (lit mesh, skinned bar with a looping clip, Light; written by tests/determinism/pack3d_write, or DET_3DLIT_PACK=<game.rpak>,
# e.g. a cooked samples/psp3d_room) run on both sides. Besides the state hash, "rensga: skinhash" and the per-fixed-step "rensga: skin f=<n> h=<hex>" lines must be equal.
# 2d mode (F4.07): the 2D slice samples/platformer2d_psp, a copy of it whose first scene is TitleBot (the bot plays the whole game, no input
# script), the same fixed 1/60 s step on both sides (the emulator and desktop players are wall-clock driven, so their game times differ).
# The hash covers every object's transform and the pickups, i.e. the whole game state of the bot run.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODE="${1:-3d}"; [ "$MODE" = 3d ] || [ "$MODE" = 2d ] || [ "$MODE" = 3dlit ] || [ "$MODE" = phys ] || [ "$MODE" = anim ] || { echo "usage: determinism.sh [3d|2d|3dlit|phys|anim]" >&2; exit 2; }
OBJF="${DET_OBJ_FRAME:--1}"
# 2d: a bot run ends on the static Results scene, so the final hash alone proves little: the per-frame trace is always compared.
if [ "$MODE" = 2d ]; then FRAMES="${DET_FRAMES:-2700}"; TMO="${DET_TIMEOUT:-300}"; TRACE="${DET_TRACE:-1}"
elif [ "$MODE" = phys ]; then FRAMES="${DET_FRAMES:-600}"; TMO="${DET_TIMEOUT:-300}"; TRACE="${DET_TRACE:-1}"
elif [ "$MODE" = anim ]; then FRAMES="${DET_FRAMES:-430}"; TMO="${DET_TIMEOUT:-300}"; TRACE="${DET_TRACE:-1}"
elif [ "$MODE" = 3dlit ]; then FRAMES="${DET_FRAMES:-240}"; TMO="${DET_TIMEOUT:-100}"; TRACE="${DET_TRACE:-1}"
else FRAMES="${DET_FRAMES:-400}"; TMO="${DET_TIMEOUT:-100}"; TRACE="${DET_TRACE:-0}"; fi
HOST_PATH="$PATH"   # the module's cmake (3.28) cannot configure the host tree: host cmake runs with the PATH from before the module env
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || exit 1
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
if [ "$MODE" = 3d ] || [ "$MODE" = 3dlit ]; then
  CLI="$CI_OUT/linux-host/tools/cli/rensga"; HEAD="$CI_OUT/linux-host/engine/runtime/rensga_headless"
  step "host build"; env PATH="$HOST_PATH" cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null
  env PATH="$HOST_PATH" cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga rensga_headless $( [ "$MODE" = 3dlit ] && echo pack3d_write )
  PROJ="$REPO/templates/psp-3d"; B="$CI_OUT/psp"; ROOT="$CI_OUT/psp-sim-root"; INPUT="$REPO/tests/determinism/walk.json"
  step "cook"; "$CLI" cook "$PROJ" >/dev/null
  PACK="$PROJ/Library/Cooked/psp/game.rpak"; SIMDEFS=(-DRENSGA_EXIT_AFTER_FRAMES=240); GENDIR="$PROJ/Library/Generated/psp"; SRCPROJ="$PROJ"
  if [ "$MODE" = 3dlit ]; then  # the cook above only provides the PSP generated header; the pack under test is the shared-3D one
    PACK="${DET_3DLIT_PACK:-$CI_OUT/determinism-3dlit/game.rpak}"
    if [ -z "${DET_3DLIT_PACK:-}" ]; then mkdir -p "$(dirname "$PACK")"; "$CI_OUT/linux-host/tests/pack3d_write" "$PACK"; fi
    INPUT=""
  fi
else
  if [ "$MODE" = phys ]; then
    SRCPROJ="$CI_OUT/determinism-phys/src"; HB="$CI_OUT/linux-host-phys"; B="$CI_OUT/psp-sim-phys"; ROOT="$CI_OUT/psp-sim-phys-root"
    python3 -I tests/determinism/make_phys_project.py "$SRCPROJ"
  elif [ "$MODE" = anim ]; then
    SRCPROJ="${DET_ANIM_PROJECT:-$REPO/samples/animator_player}"; HB="$CI_OUT/linux-host-anim"; B="$CI_OUT/psp-sim-anim"; ROOT="$CI_OUT/psp-sim-anim-root"
  else
    SRCPROJ="${DET_2D_PROJECT:-$REPO/samples/platformer2d_psp}"; HB="$CI_OUT/linux-host-2d"
    B="$CI_OUT/psp-sim-2d"; ROOT="$CI_OUT/psp-sim-2d-root"
  fi
  HEAD="$HB/engine/runtime/rensga_headless"; INPUT=""
  step "host build (RENSGA_PROJECT_DIR=$(basename "$SRCPROJ"))"
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$SRCPROJ" >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless
  if [ "$MODE" = phys ] || [ "$MODE" = anim ]; then
    PROJ="$SRCPROJ"  # the project already starts with its only scene
  else
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
  fi
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
grep -E "alloc-check|rensga: hash|rensga: skinhash" "$LA" | sed 's/^/linux: /'
grep -E "alloc-check|rensga: hash|rensga: skinhash" "$PA" | sed 's/^/psp:   /'
grep -qE "alloc-check frames=[1-9][0-9]* allocs=0" "$PA" || { echo "psp: no alloc-check allocs=0 line (see $PA)" >&2; exit 1; }
hint() { { [ "$MODE" = 2d ] || [ "$MODE" = phys ] || [ "$MODE" = anim ]; } && echo "hint: scripts of equal EXECUTION_ORDER update in type-registration order (static init order, which differs per build): give the sample scripts distinct EXECUTION_ORDER, or break ties in World::ensurePool by type hash" >&2; return 0; }
trace() { grep -oE 'rensga: trace f=[0-9]+ h=[0-9a-f]{8}' "$1"; }
animl() { grep -oE 'rensga: anim f=[0-9]+ h=[0-9a-f]{8}' "$1"; }
skin() { grep -oE 'rensga: skin(hash |  *f=[0-9]+ h=)[0-9a-f]*' "$1"; }
if tests/determinism/compare_hash.sh "$LA" "$PA"; then
  if [ "$MODE" = 3dlit ]; then  # skinned palettes: the final skinhash and every fixed step's palette hash must match bit for bit
    n="$(skin "$LA" | wc -l)"
    if [ "$n" -gt 1 ] && diff <(skin "$LA") <(skin "$PA") >/dev/null; then
      echo "determinism: skinhash MATCH $(grep -oE 'rensga: skinhash [0-9a-f]{8}' "$LA" | tail -1 | awk '{print $3}'), per-step skin trace equal over $((n - 1)) steps"
    else
      echo "determinism: skinhash MISMATCH (steps linux=$n)" >&2; diff <(skin "$LA") <(skin "$PA") | grep -m2 '^[<>]' >&2 || echo "  (no skin lines in a log)" >&2
      exit 1
    fi
  fi
  if [ "$MODE" = anim ]; then  # Animator state + written property values, every fixed step
    n="$(animl "$LA" | wc -l)"
    if [ "$n" -gt 1 ] && diff <(animl "$LA") <(animl "$PA") >/dev/null; then
      echo "determinism: animator trace equal over $n fixed steps (last $(animl "$LA" | tail -1 | awk '{print $4}'))"
    else
      echo "determinism: animator trace MISMATCH (steps linux=$n psp=$(animl "$PA" | wc -l))" >&2; diff <(animl "$LA") <(animl "$PA") | grep -m2 '^[<>]' >&2 || echo "  (no anim lines in a log)" >&2
      exit 1
    fi
  fi
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
