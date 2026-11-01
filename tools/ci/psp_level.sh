#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F6.02 (Editor 8) exercise on a console: samples/level_walk, the same glTF level twice. LevelRawBot is lit by the runtime lights (the PSP
# draws at most 4 per object), LevelBot has MeshRenderer.bakeLighting on every renderer (all 7 lamps, the moon, shadows and ambient occlusion
# in the vertex colors). Both are walked along the same route by the LevelWalker script. Cooked for psp, built as a PSP ELF with its scripts,
# run in PPSSPPHeadless (software renderer) with frame dumps; PNGs and a side-by-side in $RENSGA_CI_OUT/psp_level/shots.
#
#   tools/ci/psp_level.sh              frame dumps of both scenes, probes (tests/psp_api/probe_level.py)
# Env: RENSGA_PSP_LEVEL_PROJECT (default samples/level_walk). Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
HOST_PATH="$PATH"
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
PROJ="$(cd "${RENSGA_PSP_LEVEL_PROJECT:-$REPO/samples/level_walk}" && pwd)"
OUT="$CI_OUT/psp_level"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/psp_level-build"; HB="$CI_OUT/linux-host-level"
mkdir -p "$OUT" "$SHOTS"
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)
fail() { echo "psp_level: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }

step "host tree with the project scripts (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker 2>&1 | tail -1
GENDIR="$PROJ/Library/Generated/psp"
step "cook for psp (the bake runs here: lights + AO into the vertex colors of the LevelBot renderers)"
( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -4
PACK="$PROJ/Library/Cooked/psp/game.rpak"
[ -s "$PACK" ] || fail "no $PACK"
reflect_prescan "$B/reflect" "$PROJ"

psp_build() { # <cmake -D args...>
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE= -DRENSGA_EXIT_AFTER_FRAMES=0 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF "$@" >/dev/null
  rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
}
boot() { # <label> <timeout s>
  local l="$1" t="$2"
  rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
  ( cd "$ROOT" && timeout -s KILL $((t + 90)) "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=software --timeout="$t" > "$OUT/$l.log.emu" 2>&1 || true )
  cp "$ROOT/rensga.log" "$OUT/$l.log" 2>/dev/null || : > "$OUT/$l.log"
  need "rensga: boot" "$OUT/$l.log" "$l: the game did not start"
  need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/$l.log" "$l: no alloc-check allocs=0"
  need "rensga: walker ready auto=1" "$OUT/$l.log" "$l: the LevelWalker script did not start"
  if grep -q "Unknown syscall" "$OUT/$l.log.emu"; then fail "$l: Unknown syscall in the emulator log"; fi
  local f n
  for f in "$ROOT"/rensga_shot*.bmp; do
    [ -f "$f" ] || continue
    n="$(basename "$f" .bmp)"; n="${n#rensga_shot}"; n="${n#_}"; [ -n "$n" ] || n=end
    python3 -I "$REPO/tools/ci/bmp2png.py" "$f" "$SHOTS/${l}_$n.png" > /dev/null || fail "$l: frame $n is a single color (blank)"
    cp "$f" "$OUT/${l}_$n.bmp"
  done
}
FRAMES="60,150,240,330"
for pair in "raw:LevelRawBot" "baked:LevelBot"; do
  label="${pair%%:*}"; scene="${pair##*:}"
  step "$label: scene $scene, dumps at $FRAMES"
  psp_build -DRENSGA_PSP_AUTOSTART_SCENE="$scene" -DRENSGA_EXIT_AFTER_FRAMES=340 -DRENSGA_GU_DUMP_FRAMES="$FRAMES"
  boot "$label" 120
  grep -E "rensga: (gu draws|budget|walker ready|walker pos)|vram tex" "$OUT/$label.log" | sort -u | head -8 | sed 's/^/  /'
done
python3 -I "$REPO/tests/psp_api/probe_level.py" "$OUT" "$FRAMES" | tee "$OUT/probes.txt"
grep -q "probes: OK" "$OUT/probes.txt" || fail "pixel probes failed"
for f in $(echo "$FRAMES" | tr ',' ' '); do
  convert "$SHOTS/raw_$f.png" "$SHOTS/baked_$f.png" -background '#202020' -gravity center +append "$SHOTS/before_after_$f.png" 2>/dev/null \
    || montage "$SHOTS/raw_$f.png" "$SHOTS/baked_$f.png" -tile 2x1 -geometry +4+4 "$SHOTS/before_after_$f.png" 2>/dev/null || true
done
ls "$SHOTS"/*.png | sed 's/^/  shot /'
echo "psp_level: OK (left: runtime lights, right: baked vertex lighting)"
