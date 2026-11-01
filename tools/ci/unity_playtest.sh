#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job unity_playtest (F6.07 exercise): samples/unity_playtest, the small game built from the guide for Unity users (coins, a prefab, a data
# asset for the tuning, a UIText score).
#   * linux:   cook for linux (DATA section), MainBot on rensga_headless: the bot collects every coin, allocs=0
#   * psp:     the same scene as a PSP ELF with AUTOSTART MainBot in PPSSPPHeadless: the same lines, allocs=0, frame dumps as PNG
#   * desktop: the GL player (softpipe), screenshots with the score text
#   * the linux and psp "coingame done" lines must agree (the bot moves in FixedUpdate: deterministic)
#   tools/ci/unity_playtest.sh [linux] [psp] [desktop]       default: all three
# Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing (the linux part still runs).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-linux psp desktop}"
PROJ="$(cd "${RENSGA_PLAYTEST_PROJECT:-$REPO/samples/unity_playtest}" && pwd)"
OUT="$CI_OUT/unity-playtest"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/unity-playtest-build"; HB="$CI_OUT/linux-host-playtest"
mkdir -p "$OUT" "$SHOTS"
fail() { echo "unity_playtest: $*" >&2; exit 1; }
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
  need "rensga: coingame start coins=8 limit_s=30" "$l" "$n: CoinGame did not start (tuning asset or coin prefab missing)"
  need "rensga: coin collected 1/8 score=10" "$l" "$n: the bot never picked up a coin"
  need "rensga: coingame done score=80 collected=8/8 best=80 reason=all" "$l" "$n: the bot did not collect all 8 coins (see the done line)"
  grep -q "coingame no " "$l" && fail "$n: $(grep 'coingame no ' "$l" | head -1)"
  grep -E "rensga: coingame done" "$l" | tail -1 | sed "s/^/  $n: /"
  grep -E "rensga: coingame done" "$l" | tail -1 | sed 's/.*done //' > "$OUT/$n.done"
}

for m in $MODES; do
  case "$m" in
    linux)
      step "linux: cook for linux, MainBot on rensga_headless"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force | tail -2
      python3 -I - "$PROJ/Library/Cooked/linux/game.rpak" <<'PY' || fail "the pack has no DATA section"
import struct, sys
b = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', b, 8)[0]
secs = {b[32 + 16 * i:36 + 16 * i]: struct.unpack_from('<III', b, 36 + 16 * i) for i in range(n)}
assert b'DATA' in secs and secs[b'DATA'][2] == 1, 'DATA sections: %s' % list(secs)
print('  pack: DATA with', secs[b'DATA'][2], 'data assets')
PY
      env PATH="$HOST_PATH" "$HB/engine/runtime/rensga_headless" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene MainBot --frames 1500 --hash > "$OUT/linux.log" 2>&1 || fail "rensga_headless failed (see $OUT/linux.log)"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/linux.log" "linux: alloc-check not 0"
      check_log "$OUT/linux.log" linux
      echo "unity_playtest linux: OK";;
    psp)
      for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
      . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
      GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
      step "psp: cook"
      GENDIR="$PROJ/Library/Generated/psp"
      ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -2
      PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
      reflect_prescan "$B/reflect" "$PROJ"
      step "psp: ELF with AUTOSTART MainBot (frame dumps 30 300 900), PPSSPPHeadless (software)"
      rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
        -DRENSGA_PSP_AUTOSTART_SCENE=MainBot -DRENSGA_EXIT_AFTER_FRAMES=1500 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
        -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="30,300,900" -DRENSGA_GU_SELFTEST=OFF >/dev/null
      rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
      rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
      ( cd "$ROOT" && timeout -s KILL 420 "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=software --timeout=300 > "$OUT/psp.log.emu" 2>&1 || true )
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
      echo "unity_playtest psp: OK";;
    desktop)
      step "desktop: MainBot on the GL player (softpipe), screenshots"
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
      for fr in 40 400; do
        env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$OUT/desktop-save" xvfb-run -a -s "-screen 0 800x600x24" \
          "$HB/engine/runtime/rensga_desktop" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene MainBot --frames $fr --screenshot "$SHOTS/desktop_$fr.png" > "$OUT/desktop_$fr.log" 2>&1 \
          || fail "rensga_desktop failed (see $OUT/desktop_$fr.log)"
        need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/desktop_$fr.log" "desktop: alloc-check not 0"
      done
      ls "$SHOTS"/desktop_*.png | sed 's/^/  shot /'
      echo "unity_playtest desktop: OK";;
    *) fail "unknown mode $m";;
  esac
done
if [ -f "$OUT/linux.done" ] && [ -f "$OUT/psp.done" ]; then
  cmp -s "$OUT/linux.done" "$OUT/psp.done" && echo "unity_playtest: linux and psp agree: $(cat "$OUT/psp.done")" || fail "linux and psp disagree: $(cat "$OUT/linux.done") vs $(cat "$OUT/psp.done")"
fi
