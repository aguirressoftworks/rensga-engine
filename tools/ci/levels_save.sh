#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job levels_save (F6.11, API 9): samples/levels_save, two levels behind an async loading screen and a save that survives a restart.
#
#   tools/ci/levels_save.sh [mode...]       default mode: headless desktop
#     headless  PPSSPPHeadless, TWO runs with the same private HOME (= the same memory stick): run 1 is a new game, plays level 1 behind
#               the loading screen, writes SaveSystem slot 0 and quits; run 2 starts, reads the save ("continue level=2 coins=10"),
#               plays level 2 and saves level 3. Also: PlayerPrefs counts the runs (1 then 2), Resources::Load finds sfx/blip,
#               Application::profile() is PSP, allocs=0 in both. Screenshots of the loading screen (progress bar) and the level.
#     desktop   the same two runs on the desktop player (host build, Linux pack), an empty XDG_DATA_HOME shared by both.
#     ps2       LOCAL ONLY (your BIOS): the PS2 copy of the sample (make_sample.py --profile ps2) as an ISO in PCSX2, two boots that
#               share one memory card file (RENSGA_PCSX2_CARD). Needs the ps2-build module, PCSX2 and a BIOS; exit 77 without.
# Output: $RENSGA_CI_OUT/levels-save/ (logs, shots/*.png). Exit 77 when a needed tool is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-headless desktop}"; [ "$MODES" = all ] && MODES="headless desktop ps2"
for t in xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
PROJ="$REPO/samples/levels_save"
OUT="$CI_OUT/levels-save"; SHOTS="$OUT/shots"; HB="$CI_OUT/linux-host-levels"; B="$CI_OUT/levels-save-build"
mkdir -p "$OUT" "$SHOTS"
XV=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x800x24")
fail() { echo "levels_save: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"   # the module's cmake cannot configure the host tree: host cmake runs with the PATH from before the module env

host_tools() { # host cooker (+ desktop player) with the sample's script built in
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$1" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_desktop 2>&1 | tail -1
}
cook() { # <project> <target> [extra cooker args]
  local p="$1" t="$2"; shift 2
  ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$p" --target "$t" --force "$@" ) 9>"/tmp/rensga-cook-levels_save.lock" | tail -2
}
check_runs() { # <log1> <log2> <label>: the save/restart evidence shared by every mode
  local l1="$1" l2="$2" what="$3"
  need "levels: prefs runs=1" "$l1" "$what: run 1 should be the first run (PlayerPrefs)"
  need "levels: new game" "$l1" "$what: run 1 should start a new game"
  need "levels: resources blip valid=1" "$l1" "$what: Resources::Load(sfx/blip)"
  need "levels: loading level=1 progress=[0-9]+ frames=1 " "$l1" "$what: the loading screen did not report progress"
  need "levels: loaded level=1 scene=Level1" "$l1" "$what: level 1 did not load"
  need "levels: hud loaded scenes=2 active=Level1" "$l1" "$what: the additive Hud scene is not loaded next to Level1"
  need "levels: save written slot=0 version=2 level=2 coins=10 ok=1" "$l1" "$what: save not written"
  need "levels: save readback ok=1 level=2 coins=10" "$l1" "$what: save does not read back in the same run"
  need "levels: prefs runs=2" "$l2" "$what: PlayerPrefs did not survive the restart"
  need "levels: continue level=2 coins=10 lives=3" "$l2" "$what: run 2 did not read the save of run 1"
  need "levels: loaded level=2 scene=Level2" "$l2" "$what: level 2 did not load"
  need "levels: save written slot=0 version=2 level=3 coins=20 ok=1" "$l2" "$what: second save"
  grep -E "progress=" "$l1" | awk 'match($0,/progress=[0-9]+/){print substr($0,RSTART+9,RLENGTH-9)}' | sort -n | uniq | tr '\n' ' ' | sed 's/^/  progress values seen: /'; echo
  for l in "$l1" "$l2"; do
    grep -qE "alloc-check frames=[0-9]+ allocs=0" "$l" || fail "$what: alloc-check is not allocs=0 in $l"
  done
}

if echo " $MODES " | grep -q " headless "; then
  step "headless: PPSSPPHeadless, two runs on one memory stick"
  for t in PPSSPPHeadless; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
  host_tools "$PROJ"
  GENDIR="$PROJ/Library/Generated/psp"
  cook "$PROJ" psp --gen-header "$GENDIR/console_target.gen.h"
  PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
  reflect_prescan "$B/reflect" "$PROJ"
  . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
  GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B/psp" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE= -DRENSGA_EXIT_AFTER_FRAMES=600 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="50,58,100,150,200" -DRENSGA_GU_SELFTEST=OFF >/dev/null
  rensga_tc cmake --build "$B/psp" --target rensga_game -j"$JOBS" 2>&1 | tail -1
  R="$OUT/psp"; HH="$OUT/psp_home"; rm -rf "$R" "$HH"; mkdir -p "$R" "$HH"
  # The pack goes where a release build looks for it: PPSSPP's memory stick ms0:/PSP/GAME/RENSGA/ (= $HOME/.ppsspp/PSP/GAME/RENSGA). The
  # game finds it there (data root probe), so slot0 and prefs are written to the memory stick too and the second run reads them back.
  MS="$HH/.ppsspp/PSP/GAME/RENSGA"; mkdir -p "$MS"
  cp "$B/psp/engine/runtime/rensga_game.elf" "$R/"; cp "$PACK" "$MS/"
  for run in 1 2; do
    rm -f "$R/rensga.log"; rm -f "$R"/rensga_shot_*.bmp
    ( cd "$R" && HOME="$HH" timeout -s KILL 240 "${XV[@]}" PPSSPPHeadless "$R/rensga_game.elf" -r "$R" -l --graphics=software --timeout=200 > "$OUT/psp_run$run.emu" 2>&1 || true )
    cp "$R/rensga.log" "$OUT/psp_run$run.log" 2>/dev/null || : > "$OUT/psp_run$run.log"
    grep -E "levels:|alloc-check|rensga: prof" "$OUT/psp_run$run.log" | sed "s/^/  run$run: /" | head -60
    if [ "$run" = 1 ]; then
      for f in 50 58 100 150 200; do
        [ -f "$R/rensga_shot_$f.bmp" ] && python3 -I "$REPO/tools/ci/bmp2png.py" "$R/rensga_shot_$f.bmp" "$SHOTS/psp_frame_$f.png" && echo "  shot $SHOTS/psp_frame_$f.png"
      done
    fi
  done
  check_runs "$OUT/psp_run1.log" "$OUT/psp_run2.log" "headless"
  need "levels: platform=PSP profile=PSP" "$OUT/psp_run1.log" "headless: Application::platform / profile on the PSP"
  need "data root=ms0:/PSP/GAME/RENSGA/" "$OUT/psp_run1.log" "headless: the game should have found its pack on the memory stick"
  [ -f "$MS/slot0" ] && [ -f "$MS/prefs" ] || fail "headless: slot0 / prefs are not on the memory stick ($MS)"
  echo "  memory stick files: $(cd "$MS" && ls -l slot0 prefs | awk '{printf "%s=%s bytes  ", $9, $5}')"
  echo "levels_save headless: OK"
fi

if echo " $MODES " | grep -q " desktop "; then
  step "desktop: the same two runs on the Linux player"
  host_tools "$PROJ"
  cook "$PROJ" linux
  PACK="$PROJ/Library/Cooked/linux/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
  XD="$(mktemp -d)"
  for run in 1 2; do
    # run 1 also leaves a screenshot of the loading screen (frame 52) and the level
    if [ "$run" = 1 ]; then
      env XDG_DATA_HOME="$XD" GALLIUM_DRIVER=softpipe "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --frames 52 --screenshot "$SHOTS/desktop_loading.png" > "$OUT/desktop_shot1.log" 2>&1 || true
      env XDG_DATA_HOME="$XD" GALLIUM_DRIVER=softpipe "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --frames 170 --screenshot "$SHOTS/desktop_level.png" > "$OUT/desktop_shot2.log" 2>&1 || true
      rm -rf "$XD"; XD="$(mktemp -d)"   # the screenshot runs saved nothing yet (they stop before the level is complete): start the real run 1 clean
    fi
    env XDG_DATA_HOME="$XD" GALLIUM_DRIVER=softpipe "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --frames 900 > "$OUT/desktop_run$run.log" 2>&1 || true
    grep -E "levels:|alloc-check" "$OUT/desktop_run$run.log" | sed "s/^/  run$run: /" | head -40
  done
  # a frame in the middle of the level, with the HUD scene on top (a third boot of run 2 is not needed: replay run 2's state)
  check_runs "$OUT/desktop_run1.log" "$OUT/desktop_run2.log" "desktop"
  rm -rf "$XD"
  echo "levels_save desktop: OK"
fi

if echo " $MODES " | grep -q " ps2 "; then
  step "ps2 (local): PCSX2, two boots on one memory card"
  "$REPO/tools/emulators/pcsx2_bin.sh" >/dev/null || { echo "levels_save: PCSX2 not installed (rensga modules install pcsx2), skipped"; exit 77; }
  CFGJ="$HOME/.config/rensga/emulators.json"
  [ -f "$CFGJ" ] || ls "$HOME/.config/PCSX2/bios/"*.bin >/dev/null 2>&1 || { echo "levels_save: no PS2 BIOS configured, skipped"; exit 77; }
  P2="$OUT/ps2-project"; rm -rf "$P2"
  python3 "$PROJ/tools/make_sample.py" --profile ps2 --out "$P2" >/dev/null
  host_tools "$PROJ"
  cook "$P2" ps2
  export RENSGA_PS2_PROJECT="$P2" RENSGA_PS2_BUILD="$OUT/ps2"
  RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_FORMAT_BLANK_CARD=ON -DRENSGA_PS2_STRESS_SPRITES=0" "$REPO/tools/ci/ps2.sh" > "$OUT/ps2_build.log" 2>&1 || { rc=$?; [ $rc = 77 ] && exit 77; tail -20 "$OUT/ps2_build.log" >&2; fail "PS2 ISO build failed"; }
  CARD="$OUT/ps2-card/Mcd001.ps2"; rm -rf "$OUT/ps2-card"
  export RENSGA_PCSX2_CARD="$CARD" RENSGA_PCSX2_WORK="$OUT/pcsx2"
  # a first boot on a throw-away card only for the screenshot of the loading screen (the PS2 copy holds it for 300 frames)
  RENSGA_PCSX2_CARD= "$REPO/tools/emulators/pcsx2_boot.sh" "$OUT/ps2/rensga.iso" --frames 120 --timeout "${LEVELS_PS2_TIMEOUT:-600}" --log "$OUT/ps2_shot.log" \
    --shot "$SHOTS/ps2_loading.png" || fail "PCSX2 screenshot boot failed (see $OUT/ps2_shot.log)"
  for run in 1 2; do
    "$REPO/tools/emulators/pcsx2_boot.sh" "$OUT/ps2/rensga.iso" --frames "${LEVELS_PS2_FRAMES:-700}" --timeout "${LEVELS_PS2_TIMEOUT:-600}" --log "$OUT/ps2_run$run.log" \
      || fail "PCSX2 run $run: no heartbeat (see $OUT/ps2_run$run.log)"
    grep -a -E "levels:|memory card|alloc-check" "$OUT/ps2_run$run.log" | sed "s/^/  run$run: /" | head -40
  done
  check_runs "$OUT/ps2_run1.log" "$OUT/ps2_run2.log" "ps2"
  need "levels: platform=PS2 profile=PS2" "$OUT/ps2_run1.log" "ps2: platform / profile"
  echo "  memory card file: $(ls -l "$CARD" | awk '{print $5}') bytes"
  echo "levels_save ps2: OK"
fi
echo "levels_save: all requested modes passed"
