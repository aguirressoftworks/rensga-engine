#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job physics_range (F6.08 exercise): samples/shooting_range, a raycast shooting range with layers and trigger zones.
#   * linux:  the bot scene (RangeBot) on rensga_headless: shots, hits, "ignored=Decoy" lines, the zone counter, allocs=0
#   * psp:    the same scene as a PSP ELF with AUTOSTART RangeBot in PPSSPPHeadless (software renderer): the same log lines, allocs=0,
#             frame dumps (before the first shot, mid-game, end) converted to PNG in $RENSGA_CI_OUT/range/shots
#   * both logs must report the same shots / hits / blocked / zone numbers (the simulation is deterministic)
#   tools/ci/physics_range.sh [linux] [psp] [desktop]       default: all three
# Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing (the linux part still runs).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-linux psp desktop}"
PROJ="$(cd "${RENSGA_RANGE_PROJECT:-$REPO/samples/shooting_range}" && pwd)"
OUT="$CI_OUT/range"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/range-build"; HB="$CI_OUT/linux-host-range"
mkdir -p "$OUT" "$SHOTS"
fail() { echo "physics_range: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)

step "host tree with the sample scripts (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_headless rensga_desktop 2>&1 | tail -1

# The log lines the bot prints must agree with what the scene is built to do.
check_log() { # <log> <label>
  local l="$1" n="$2"
  need "rensga: range ready targets=5 " "$l" "$n: the RangeGame script did not start"
  need "rensga: range shot=[0-9]+ hit=Target[0-9] dist=[0-9]+ ignored=Decoy[01]" "$l" "$n: no hit that went through a decoy (LayerMask test)"
  need "rensga: range shot=[0-9]+ miss" "$l" "$n: no deliberate miss"
  need "rensga: range zone enter=[1-9]" "$l" "$n: the trigger zone never counted an entry"
  need "rensga: range done shots=14 " "$l" "$n: the bot did not finish its shots"
  grep -E "rensga: range done" "$l" | tail -1 | sed "s/^/  $n: /"
  local d; d="$(grep -E 'rensga: range done' "$l" | tail -1 | sed 's/.*done //')"
  local hits; hits="$(echo "$d" | sed -n 's/.*hits=\([0-9]*\).*/\1/p')"
  [ "${hits:-0}" -ge 5 ] || fail "$n: fewer than 5 hits ($d)"
  echo "$d" > "$OUT/$n.done"
}

for m in $MODES; do
  case "$m" in
    linux)
      step "linux: cook for linux, RangeBot on rensga_headless"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force | tail -2
      env PATH="$HOST_PATH" "$HB/engine/runtime/rensga_headless" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene RangeBot --frames 600 --hash > "$OUT/linux.log" 2>&1 || fail "rensga_headless failed (see $OUT/linux.log)"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/linux.log" "linux: alloc-check not 0"
      check_log "$OUT/linux.log" linux
      echo "physics_range linux: OK";;
    psp)
      for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
      . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
      GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
      step "psp: cook"
      GENDIR="$PROJ/Library/Generated/psp"
      ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -2
      PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
      grep -q "PHY3" "$PACK" || fail "the pack has no PHY3 section"
      reflect_prescan "$B/reflect" "$PROJ"
      step "psp: ELF with AUTOSTART RangeBot (frame dumps 20 330 600), PPSSPPHeadless (software)"
      rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
        -DRENSGA_PSP_AUTOSTART_SCENE=RangeBot -DRENSGA_EXIT_AFTER_FRAMES=620 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
        -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="50,330,610" -DRENSGA_GU_SELFTEST=OFF >/dev/null
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
      echo "physics_range psp: OK";;
    desktop)
      step "desktop: RangeBot on the GL player (softpipe), screenshot"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
      for fr in 40 330; do
        env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$OUT/desktop-save" xvfb-run -a -s "-screen 0 800x600x24" \
          "$HB/engine/runtime/rensga_desktop" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene RangeBot --frames $fr --screenshot "$SHOTS/desktop_$fr.png" > "$OUT/desktop_$fr.log" 2>&1 \
          || fail "rensga_desktop failed (see $OUT/desktop_$fr.log)"
        need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/desktop_$fr.log" "desktop: alloc-check not 0"
      done
      ls "$SHOTS"/desktop_*.png | sed 's/^/  shot /'
      echo "physics_range desktop: OK";;
    *) fail "unknown mode $m";;
  esac
done
# the two sims must agree (the PSP half only ran when both logs exist)
if [ -f "$OUT/linux.done" ] && [ -f "$OUT/psp.done" ]; then
  cmp -s "$OUT/linux.done" "$OUT/psp.done" && echo "physics_range: linux and psp agree: $(cat "$OUT/psp.done")" || fail "linux and psp disagree: $(cat "$OUT/linux.done") vs $(cat "$OUT/psp.done")"
fi
