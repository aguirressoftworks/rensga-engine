#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job explosions (F6.09 exercise): samples/explosions, click-spawned particle explosions with camera shake, a minimap camera,
# MaterialPropertyBlock tints, a LineRenderer marker.
#   * linux:   the bot scene (ExplosionsBot) on rensga_headless: the boom lines (screen point -> world point -> screen point), the final
#              storm that fills the particle pools (rejected > 0), allocs=0
#   * desktop: the same scene on the GL player (softpipe, xvfb): screenshots during the first blast and during the storm
#   * psp:     the same scene as a PSP ELF with AUTOSTART ExplosionsBot in PPSSPPHeadless (software renderer): the same log lines,
#              allocs=0, frame dumps converted to PNG in $RENSGA_CI_OUT/explosions/shots
#   tools/ci/explosions.sh [linux] [desktop] [psp]       default: all three
# Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing (the linux part still runs).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-linux psp desktop}"
PROJ="$(cd "${RENSGA_EXPLOSIONS_PROJECT:-$REPO/samples/explosions}" && pwd)"
OUT="$CI_OUT/explosions"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/explosions-build"; HB="$CI_OUT/linux-host-explosions"
mkdir -p "$OUT" "$SHOTS"
fail() { echo "explosions: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)

step "host tree with the sample scripts (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless rensga_desktop 2>&1 | tail -1

# The log lines the bot prints must agree with what the scene is built to do.
check_log() { # <log> <label>
  local l="$1" n="$2"
  need "rensga: fx ready booms=6 crates=5 pointer=bot" "$l" "$n: the ExplosionGame script did not start"
  need "rensga: fx boom=8 " "$l" "$n: fewer than 8 explosions"
  # ScreenPointToRay -> ground point -> WorldToScreenPoint must give the clicked pixel back (within one pixel)
  local bad; bad="$(grep -E 'rensga: fx boom=' "$l" | sed -E 's/.*screen=([0-9]+),([0-9]+) .* back=([0-9]+),([0-9]+) .*/\1 \2 \3 \4/' | awk '{dx=$1-$3; dy=$2-$4; if (dx<0) dx=-dx; if (dy<0) dy=-dy; if (dx>1 || dy>1) print "bad:" $0}')"
  [ -z "$bad" ] || fail "$n: the screen -> world -> screen round trip is off: $bad"
  need "rensga: fx done booms=8 live=[0-9]+ reserved=[0-9]+ rejected=[1-9][0-9]*" "$l" "$n: the storm did not fill the particle pools (rejected=0)"
  grep -E "rensga: fx done" "$l" | tail -1 | sed "s/^/  $n: /"
  echo "$(grep -E 'rensga: fx done' "$l" | tail -1 | sed 's/.*done //')" > "$OUT/$n.done"
}

for m in $MODES; do
  case "$m" in
    linux)
      step "linux: cook for linux, ExplosionsBot on rensga_headless"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force | tail -2
      env PATH="$HOST_PATH" "$HB/engine/runtime/rensga_headless" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene ExplosionsBot --frames 420 --hash > "$OUT/linux.log" 2>&1 || fail "rensga_headless failed (see $OUT/linux.log)"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/linux.log" "linux: alloc-check not 0"
      check_log "$OUT/linux.log" linux
      echo "explosions linux: OK";;
    psp)
      for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
      . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
      GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
      step "psp: cook"
      GENDIR="$PROJ/Library/Generated/psp"
      ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -2
      PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
            reflect_prescan "$B/reflect" "$PROJ"
      step "psp: ELF with AUTOSTART ExplosionsBot (frame dumps 36 70 100 400 440), PPSSPPHeadless (software)"
      rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
        -DRENSGA_PSP_AUTOSTART_SCENE=ExplosionsBot -DRENSGA_EXIT_AFTER_FRAMES=520 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
        -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="36,70,100,400,440" -DRENSGA_GU_SELFTEST=OFF >/dev/null
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
      echo "explosions psp: OK";;
    desktop)
      step "desktop: ExplosionsBot on the GL player (softpipe), screenshot"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
      for fr in 22 96 270 306; do
        env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$OUT/desktop-save" xvfb-run -a -s "-screen 0 800x600x24" \
          "$HB/engine/runtime/rensga_desktop" --fixed-dt --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene ExplosionsBot --frames $fr --screenshot "$SHOTS/desktop_$fr.png" > "$OUT/desktop_$fr.log" 2>&1 \
          || fail "rensga_desktop failed (see $OUT/desktop_$fr.log)"
        need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/desktop_$fr.log" "desktop: alloc-check not 0"
      done
      ls "$SHOTS"/desktop_*.png | sed 's/^/  shot /'
      echo "explosions desktop: OK";;
    *) fail "unknown mode $m";;
  esac
done
# the two sims must agree (the PSP half only ran when both logs exist)
if [ -f "$OUT/linux.done" ] && [ -f "$OUT/psp.done" ]; then
  cmp -s "$OUT/linux.done" "$OUT/psp.done" && echo "explosions: linux and psp agree: $(cat "$OUT/psp.done")" || echo "explosions: linux and psp differ (particles are visual only): $(cat "$OUT/linux.done") vs $(cat "$OUT/psp.done")"
fi
