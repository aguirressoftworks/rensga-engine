#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job extending_editor (F6.07 exercise): samples/extending_editor, a data asset driving gameplay (the Editor/ part is the editor test
# tests/editor/extensions_ui_check.sh, ctest editor_extensions_ui).
#   * linux:   cook for linux (DATA section), Arena on rensga_headless: every Patroller reads its EnemyStats, allocs=0
#   * psp:     the same scene as a PSP ELF with AUTOSTART Arena in PPSSPPHeadless: the same lines, allocs=0, frame dumps as PNG
#   * desktop: the GL player (softpipe), screenshots: the cubes carry the tint of their data asset
#   tools/ci/extending_editor.sh [linux] [psp] [desktop]       default: all three
# Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing (the linux part still runs).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-linux psp desktop}"
PROJ="$(cd "${RENSGA_EXT_PROJECT:-$REPO/samples/extending_editor}" && pwd)"
OUT="$CI_OUT/extending-editor"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/extending-editor-build"; HB="$CI_OUT/linux-host-extending"
mkdir -p "$OUT" "$SHOTS"
fail() { echo "extending_editor: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)

if [ -n "${RENSGA_CI_HOST_TREE:-}" ]; then  # a host tree already configured for this project (RENSGA_PROJECT_DIR) and built: cooker, headless, desktop
  HB="$RENSGA_CI_HOST_TREE"
else
  step "host tree with the sample scripts (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless rensga_desktop 2>&1 | tail -1
fi

check_log() { # <log> <label>
  local l="$1" n="$2"
  need "rensga: patroller PatrolGrunt label=Grunt kind=0 hp=30 speed_cm=200 pauses=1" "$l" "$n: PatrolGrunt did not read its data asset"
  need "rensga: patroller PatrolScout label=Scout kind=1 hp=18 speed_cm=550 pauses=1" "$l" "$n: PatrolScout did not read its data asset"
  need "rensga: patroller PatrolBrute label=Brute kind=2 hp=120 speed_cm=90 pauses=1" "$l" "$n: PatrolBrute did not read its data asset"
  need "rensga: patrol done PatrolScout laps=[1-9]" "$l" "$n: the Scout (speed 5.5) never finished a lap"
  grep -q "patroller .* no stats" "$l" && fail "$n: a Patroller has no stats"
  grep -E "rensga: patrol done" "$l" | sed "s/^/  $n: /"
}

for m in $MODES; do
  case "$m" in
    linux)
      step "linux: cook for linux, Arena on rensga_headless"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force | tail -2
      python3 -I - "$PROJ/Library/Cooked/linux/game.rpak" <<'PY' || fail "the pack has no DATA section"
import struct, sys
b = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', b, 8)[0]
secs = {b[32 + 16 * i:36 + 16 * i]: struct.unpack_from('<III', b, 36 + 16 * i) for i in range(n)}
assert b'DATA' in secs and secs[b'DATA'][2] == 3, 'DATA sections: %s' % list(secs)
print('  pack: DATA with', secs[b'DATA'][2], 'data assets')
PY
      env PATH="$HOST_PATH" "$HB/engine/runtime/rensga_headless" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene Arena --frames 600 --hash > "$OUT/linux.log" 2>&1 || fail "rensga_headless failed (see $OUT/linux.log)"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/linux.log" "linux: alloc-check not 0"
      check_log "$OUT/linux.log" linux
      echo "extending_editor linux: OK";;
    psp)
      for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
      . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
      GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
      step "psp: cook"
      GENDIR="$PROJ/Library/Generated/psp"
      ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -2
      PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
      reflect_prescan "$B/reflect" "$PROJ"
      step "psp: ELF with AUTOSTART Arena (frame dumps 30 300 590), PPSSPPHeadless (software)"
      rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
        -DRENSGA_PSP_AUTOSTART_SCENE=Arena -DRENSGA_EXIT_AFTER_FRAMES=620 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
        -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="30,300,590" -DRENSGA_GU_SELFTEST=OFF >/dev/null
      rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
      rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
      ( cd "$ROOT" && timeout -s KILL 300 "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=software --timeout=200 > "$OUT/psp.log.emu" 2>&1 || true )
      cp "$ROOT/rensga.log" "$OUT/psp.log" 2>/dev/null || : > "$OUT/psp.log"
      need "rensga: boot" "$OUT/psp.log" "psp: the game did not start"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/psp.log" "psp: no alloc-check allocs=0"
      if grep -q "Unknown syscall" "$OUT/psp.log.emu"; then fail "psp: Unknown syscall in the emulator log"; fi
      check_log "$OUT/psp.log" psp
      for f in "$ROOT"/rensga_shot*.bmp; do
        [ -f "$f" ] || continue
        n="$(basename "$f" .bmp)"; n="${n#rensga_shot}"; n="${n#_}"; [ -n "$n" ] || n=end
        python3 -I "$REPO/tools/ci/bmp2png.py" "$f" "$SHOTS/psp_$n.png" > /dev/null || fail "psp: frame $n is a single color (blank)"
      done
      ls "$SHOTS"/psp_*.png | sed 's/^/  shot /'
      echo "extending_editor psp: OK";;
    desktop)
      step "desktop: Arena on the GL player (softpipe), screenshots"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
      for fr in 40 330; do
        env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$OUT/desktop-save" xvfb-run -a -s "-screen 0 800x600x24" \
          "$HB/engine/runtime/rensga_desktop" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene Arena --frames $fr --screenshot "$SHOTS/desktop_$fr.png" > "$OUT/desktop_$fr.log" 2>&1 \
          || fail "rensga_desktop failed (see $OUT/desktop_$fr.log)"
        need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/desktop_$fr.log" "desktop: alloc-check not 0"
      done
      ls "$SHOTS"/desktop_*.png | sed 's/^/  shot /'
      echo "extending_editor desktop: OK";;
    *) fail "unknown mode $m";;
  esac
done
