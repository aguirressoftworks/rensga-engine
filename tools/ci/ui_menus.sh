#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job ui_menus (F6.04 Editor 10, F6.12 API 10): the two UI exercises.
#   samples/menus        a title screen and a pause menu built in the editor, walked by a scripted pad (MenuBot pushes the same navigation as the
#                        pad: UI::pushNav). The "navigation trace" is every "rensga: ui focus|click|slider|toggle ..." and "rensga: menus: ..." line.
#   samples/hud_options  a HUD (Image fill health bar, allocation-free score/clock text) and an options menu whose sliders write PlayerPrefs and
#                        the mixer groups. HudBot sets the sliders and presses Back; a second run (OptionsVerify, a restart with the same save
#                        store) must read the same values back.
#
#   tools/ci/ui_menus.sh [mode...]       default: desktop psp     (ps2 is LOCAL ONLY: PCSX2 needs your own BIOS)
#     desktop  rensga_desktop under xvfb (softpipe): menus trace, hud + options + restart
#     psp      PPSSPPHeadless (psp-build module): the same, traces must equal the desktop trace line for line
#     ps2      ISO of both samples (ps2 profile, 640x448: the Canvas scaler maps the 480x272 reference) booted in PCSX2; trace equal again
# Output: $RENSGA_CI_OUT/ui-menus/ (logs, traces, shots). Exit 77 when a needed tool is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-desktop psp}"
for t in xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
OUT="$CI_OUT/ui-menus"; mkdir -p "$OUT"
MENUS="$REPO/samples/menus"; HUD="$REPO/samples/hud_options"
XV=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x800x24")
HOST_PATH="$PATH"
fail() { echo "ui_menus: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
trace() { grep -aoE 'rensga: (ui (focus|click|slider|toggle) .*|menus: .*|options: .*|hud-bot: .*)' "$1" | sed 's/[[:space:]]*$//'; }

# The expected walk through the menus (what the bot does; see MenuBot.cpp). Each line must appear, in this order.
EXPECT_MENUS=(
  "rensga: menus: title ready"
  "rensga: ui focus PlayButton"
  "rensga: menus: bot in scene TitleBot"
  "rensga: ui focus OptionsButton"
  "rensga: ui click OptionsButton"
  "rensga: menus: options shown"
  "rensga: ui focus VolumeSlider"
  "rensga: ui slider VolumeSlider value_x100=90"
  "rensga: ui slider VolumeSlider value_x100=80"
  "rensga: ui slider VolumeSlider value_x100=90"
  "rensga: ui focus MuteToggle"
  "rensga: ui toggle MuteToggle isOn=1"
  "rensga: ui toggle MuteToggle isOn=0"
  "rensga: ui focus QuitButton"
  "rensga: ui focus OptionsButton"
  "rensga: ui focus PlayButton"
  "rensga: ui click PlayButton"
  "rensga: menus: play"
  "rensga: menus: game ready"
  "rensga: ui focus ResumeButton"
  "rensga: menus: paused at tick"
  "rensga: ui focus QuitToTitleButton"
  "rensga: ui focus ResumeButton"
  "rensga: ui click ResumeButton"
  "rensga: menus: resumed at tick"
  "rensga: ui focus ResumeButton"
  "rensga: menus: paused at tick"
  "rensga: ui focus QuitToTitleButton"
  "rensga: ui click QuitToTitleButton"
  "rensga: menus: to title"
  "rensga: menus: bot finished"
)
# A console ELF boots the project's first scene and then switches to the autostart scene (TitleBot); the desktop player is started on it with --scene.
# The PCSX2 log holds the EE console twice (stdout and the log file), so the trace also ends at the first "bot finished". The console trace starts at that switch, so the first scene's own "title ready / focus" lines do not count.
console_trace() { awk '/scene loaded index=[0-9]+ name=TitleBot/{on=1} on{print} /menus: bot finished/{exit}' "$1" | trace /dev/stdin; }
check_order() {  # <log> <label> <expected lines...>: each expected line is found after the previous one
  local log="$1" label="$2"; shift 2
  local at=0 n=0 line
  for line in "$@"; do
    n=$((n + 1))
    local rel; rel="$(tail -n +"$((at + 1))" "$log" | grep -an -m1 -F "$line" | head -1 | cut -d: -f1)"
    [ -n "$rel" ] || fail "$label: step $n not found in order: '$line' (see $log)"
    at=$((at + rel))
  done
  echo "  $label: $n navigation steps in order"
}
check_hud() {  # <label> <run1 log> <run2 log>
  local l="$1" r1="$2" r2="$3"
  need "rensga: options: loaded music=50 sfx=50 mixer music=50 sfx=50 \(hasSaved=0\)" "$r1" "$l run 1: the first run must start from the defaults"
  need "rensga: options: music=80 sfx=50" "$r1" "$l run 1: music slider +3 steps"
  need "rensga: options: music=80 sfx=30" "$r1" "$l run 1: effects slider -2 steps"
  need "rensga: options: saved music=80 sfx=30 ok=1" "$r1" "$l run 1: Back must save"
  need "rensga: hud: volumes music=80 sfx=30 mixer music=80 sfx=30" "$r1" "$l run 1: the HUD must find the saved volumes in the mixer"
  need "rensga: hud: tick=[0-9]+ score=[0-9]+ hp=[0-9]+/100 bar=[0-9]+" "$r1" "$l run 1: HUD lines"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$r1" || fail "$l run 1: alloc-check is not allocs=0 (see $r1)"
  need "rensga: options: loaded music=80 sfx=30 mixer music=80 sfx=30 \(hasSaved=1\)" "$r2" "$l run 2 (restart): the saved values must come back"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$r2" || fail "$l run 2: alloc-check is not allocs=0 (see $r2)"
  echo "  $l: options saved in run 1 (music 80, effects 30) and read back after the restart"
}

host_tools() {  # <name> <project>
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$CI_OUT/linux-host-$1" -DRENSGA_PROJECT_DIR="$2" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  env PATH="$HOST_PATH" cmake --build "$CI_OUT/linux-host-$1" -j"$JOBS" --target rensga-cooker rensga_desktop 2>&1 | tail -1
}
cook() { # <name> <project> <target> [extra]
  ( flock 9; "$CI_OUT/linux-host-$1/tools/cooker/rensga-cooker" cook --project "$2" --target "$3" --force "${@:4}" ) 9>"/tmp/rensga-cook-ui_$1.lock" | tail -2
}

if echo " $MODES " | grep -q " desktop "; then
  step "desktop: menus"
  host_tools menus "$MENUS"; cook menus "$MENUS" linux
  XDG_DATA_HOME="$(mktemp -d)" GALLIUM_DRIVER=softpipe "${XV[@]}" "$CI_OUT/linux-host-menus/engine/runtime/rensga_desktop" \
    --pack "$MENUS/Library/Cooked/linux/game.rpak" --scene TitleBot --frames 1500 > "$OUT/desktop_menus.log" 2>&1 || true
  check_order "$OUT/desktop_menus.log" "desktop menus" "${EXPECT_MENUS[@]}"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$OUT/desktop_menus.log" || fail "desktop menus: alloc-check is not allocs=0"
  trace "$OUT/desktop_menus.log" > "$OUT/desktop_menus.trace"
  step "desktop: hud_options (run, restart)"
  host_tools hud "$HUD"; cook hud "$HUD" linux
  SAVES="$(mktemp -d)"
  XDG_DATA_HOME="$SAVES" GALLIUM_DRIVER=softpipe "${XV[@]}" "$CI_OUT/linux-host-hud/engine/runtime/rensga_desktop" --pack "$HUD/Library/Cooked/linux/game.rpak" --scene HudBot --frames 1500 > "$OUT/desktop_hud1.log" 2>&1 || true
  XDG_DATA_HOME="$SAVES" GALLIUM_DRIVER=softpipe "${XV[@]}" "$CI_OUT/linux-host-hud/engine/runtime/rensga_desktop" --pack "$HUD/Library/Cooked/linux/game.rpak" --scene OptionsVerify --frames 300 > "$OUT/desktop_hud2.log" 2>&1 || true
  check_hud "desktop hud" "$OUT/desktop_hud1.log" "$OUT/desktop_hud2.log"
  trace "$OUT/desktop_hud1.log" > "$OUT/desktop_hud.trace"
  echo "ui_menus desktop: OK"
fi

psp_run() {  # <name> <project> <scene> <log> [HOME dir]: builds the PSP ELF for the sample with that autostart scene and runs it in PPSSPPHeadless
  local name="$1" proj="$2" scene="$3" log="$4" home="${5:-$OUT/psp_home_$1}"
  local B="$CI_OUT/ui-menus-build/psp-$name-$scene${PSP_TAG:-}" GENDIR="$proj/Library/Generated/psp" R
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$CI_OUT/ui-menus-build/reflect-$name" -DRENSGA_PROJECT_DIR="$proj" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE="$scene" -DRENSGA_EXIT_AFTER_FRAMES=1500 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES="${PSP_DUMP_FRAMES:-}" -DRENSGA_GU_SELFTEST=OFF >/dev/null
  rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
  R="$OUT/psp_${name}_$scene${PSP_TAG:-}"; rm -rf "$R"; mkdir -p "$R" "$home"
  cp "$B/engine/runtime/rensga_game.elf" "$proj/Library/Cooked/psp/game.rpak" "$R/"
  # the PSP build keeps its save store (the "prefs" file) next to the data root, which is this run's folder: a restart carries it over
  [ -z "${PSP_CARRY:-}" ] || cp "$PSP_CARRY" "$R/prefs" || fail "psp $name: run 1 left no prefs file to carry over to the restart"
  ( cd "$R" && HOME="$home" timeout -s KILL 400 "${XV[@]}" PPSSPPHeadless "$R/rensga_game.elf" -r "$R" -l --graphics=software --timeout=360 > "$R/emu.log" 2>&1 || true )
  cp "$R/rensga.log" "$log" 2>/dev/null || : > "$log"
}

if echo " $MODES " | grep -q " psp "; then
  step "psp: PPSSPPHeadless"
  command -v PPSSPPHeadless >/dev/null || { echo "skip: PPSSPPHeadless not installed"; exit 77; }
  host_tools menus "$MENUS"; host_tools hud "$HUD"
  cook menus "$MENUS" psp --gen-header "$MENUS/Library/Generated/psp/console_target.gen.h"
  cook hud "$HUD" psp --gen-header "$HUD/Library/Generated/psp/console_target.gen.h"
  mkdir -p "$CI_OUT/ui-menus-build"
  reflect_prescan "$CI_OUT/ui-menus-build/reflect-menus" "$MENUS"; reflect_prescan "$CI_OUT/ui-menus-build/reflect-hud" "$HUD"
  . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
  GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
  psp_run menus "$MENUS" TitleBot "$OUT/psp_menus.log" "$OUT/psp_home_menus"
  check_order "$OUT/psp_menus.log" "psp menus" "${EXPECT_MENUS[@]}"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$OUT/psp_menus.log" || fail "psp menus: alloc-check is not allocs=0"
  console_trace "$OUT/psp_menus.log" > "$OUT/psp_menus.trace"
  [ ! -s "$OUT/desktop_menus.trace" ] || diff "$OUT/desktop_menus.trace" "$OUT/psp_menus.trace" > "$OUT/menus_trace.diff" || fail "the PSP navigation trace differs from the desktop one (see $OUT/menus_trace.diff)"
  rm -rf "$OUT/psp_home_hud"
  psp_run hud "$HUD" HudBot "$OUT/psp_hud1.log" "$OUT/psp_home_hud"
  PSP_CARRY="$OUT/psp_hud_HudBot/prefs" psp_run hud "$HUD" OptionsVerify "$OUT/psp_hud2.log" "$OUT/psp_home_hud"
  check_hud "psp hud" "$OUT/psp_hud1.log" "$OUT/psp_hud2.log"
  if [ -n "${UI_SHOTS:-}" ]; then  # UI_SHOTS=1: frame dumps of the bots' menus, hud and options (PNG in $OUT/psp-shots)
    mkdir -p "$OUT/psp-shots"
    for pair in "menus:$MENUS:TitleBot:20,140,330,420,560" "hud:$HUD:HudBot:60,400,700,900,1100"; do
      IFS=: read -r name proj scene fr <<<"$pair"
      PSP_TAG=-shots PSP_DUMP_FRAMES="$fr" psp_run "$name" "$proj" "$scene" "$OUT/psp_shots_$name.log" "$OUT/psp_home_shots"
      for f in "$OUT/psp_${name}_${scene}-shots"/rensga_shot_*.bmp; do
        python3 "$REPO/tools/ci/bmp2png.py" "$f" "$OUT/psp-shots/${name}_$(basename "$f" .bmp | sed 's/rensga_shot_//').png" || :
      done
    done
  fi
  echo "ui_menus psp: OK"
fi

if echo " $MODES " | grep -q " ps2 "; then
  step "ps2 (LOCAL ONLY, PCSX2 + your BIOS)"
  "$REPO/tools/emulators/pcsx2_bin.sh" >/dev/null || { echo "ui_menus: PCSX2 not installed, skipped"; exit 77; }
  host_tools menus "$MENUS"; host_tools hud "$HUD"
  rm -f "$OUT/ps2_card_hud.ps2"
  # tag:sample:scene:frames: hud runs twice on the SAME memory card (RENSGA_PCSX2_CARD), the second one is the restart that must read the values back
  for pair in "menus:$MENUS:TitleBot:2400" "hud:$HUD:HudBot:2400" "hud2:$HUD:OptionsVerify:900"; do
    IFS=: read -r tag proj scene frames <<<"$pair"; name="${tag%2}"
    P2="$CI_OUT/ui-menus-ps2-$name"
    if [ "$tag" = "$name" ]; then
      rm -rf "$P2"; python3 "$proj/tools/make_sample.py" --profile ps2 --out "$P2" >/dev/null
      host_tools "$name-ps2" "$P2"; cook "$name-ps2" "$P2" ps2
    fi
    RENSGA_PS2_PROJECT="$P2" RENSGA_PS2_BUILD="$OUT/ps2-$tag" RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_AUTOSTART=$scene -DRENSGA_PS2_FORMAT_BLANK_CARD=ON -DRENSGA_PS2_CHECK_FRAMES=$frames" \
      "$REPO/tools/ci/ps2.sh" || fail "ps2 $tag: ISO build failed"
    CARD=(); [ "$name" != hud ] || CARD=(env RENSGA_PCSX2_CARD="$OUT/ps2_card_hud.ps2")
    RENSGA_PCSX2_DONE="rensga: quit after [0-9]+ frames" RENSGA_PCSX2_WORK="$OUT/ps2-$tag/pcsx2" "${CARD[@]}" "$REPO/tools/emulators/pcsx2_boot.sh" "$OUT/ps2-$tag/rensga.iso" --frames "$((frames + 60))" --timeout 900 \
      --log "$OUT/ps2_$tag.log" --shot "$OUT/ps2_$tag.png" || fail "ps2 $tag: boot failed (see $OUT/ps2_$tag.log)"
  done
  if [ -n "${UI_SHOTS:-}" ]; then  # UI_SHOTS=1: the scenes without a bot, a screenshot after 150 frames (the bots quit the game, which would leave the BIOS on screen)
    for pair in "menus:Title" "hud:Hud" "hud:Options"; do
      IFS=: read -r name scene <<<"$pair"; P2="$CI_OUT/ui-menus-ps2-$name"
      RENSGA_PS2_PROJECT="$P2" RENSGA_PS2_BUILD="$OUT/ps2-shot-$name-$scene" RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_AUTOSTART=$scene -DRENSGA_PS2_FORMAT_BLANK_CARD=ON -DRENSGA_PS2_CHECK_FRAMES=240" \
        "$REPO/tools/ci/ps2.sh" >/dev/null || fail "ps2 shot $name $scene: ISO build failed"
      RENSGA_PCSX2_WORK="$OUT/ps2-shot-$name-$scene/pcsx2" "$REPO/tools/emulators/pcsx2_boot.sh" "$OUT/ps2-shot-$name-$scene/rensga.iso" --frames 180 --timeout 600 \
        --log "$OUT/ps2_shot_${name}_$scene.log" --shot "$OUT/ps2_shot_${name}_$scene.png" || fail "ps2 shot $name $scene: boot failed"
    done
  fi
  check_order "$OUT/ps2_menus.log" "ps2 menus" "${EXPECT_MENUS[@]}"
  console_trace "$OUT/ps2_menus.log" > "$OUT/ps2_menus.trace"
  [ ! -s "$OUT/desktop_menus.trace" ] || diff "$OUT/desktop_menus.trace" "$OUT/ps2_menus.trace" > "$OUT/menus_ps2_trace.diff" || fail "the PS2 navigation trace differs (see $OUT/menus_ps2_trace.diff)"
  need "rensga: hud: tick=[0-9]+ score=[0-9]+ hp=[0-9]+/100 bar=[0-9]+" "$OUT/ps2_hud.log" "ps2 hud: HUD lines"
  check_hud "ps2 hud" "$OUT/ps2_hud.log" "$OUT/ps2_hud2.log"
  echo "ui_menus ps2: OK (shots $OUT/ps2_menus.png, $OUT/ps2_hud.png)"
fi
echo "ui_menus: all requested modes passed"
