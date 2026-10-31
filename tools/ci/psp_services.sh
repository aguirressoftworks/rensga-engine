#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job psp_services (F5.13, F5.15, F5.16): samples/psp_services on PSP, in the emulators that exist on a PC.
#
#   tools/ci/psp_services.sh [mode...]       default mode: all
#     headless  PPSSPPHeadless (software renderer, no input): ServicesAuto scene. Language / battery / clock=333 / memory lines,
#               savedata AUTOSAVE (the file appears under the private HOME's PSP/SAVEDATA and reads back), injected
#               OnApplicationPause(true) then (false), alloc-check allocs=0, a frame of the scene.
#     sdl       PPSSPPSDL under xvfb with a sandbox XDG_CONFIG_HOME: ppsspp.ini GameLanguage / ButtonPreference reach the game
#               (language=..., confirm=east|south); the OSK is driven with pad keys through rensga-xtool and gives name=<text>, then
#               the save goes through the system dialog (LISTSAVE) and writes the SAVEDATA dir. Window captures of both.
#     input     PPSSPPSDL: the InputCam scene; xtool holds the nub (W) and the shoulders (Left/Right arrows, mapped in controls.ini)
#               and the logged camera yaw / player position change. Also the F4.03 Home-key check (reported, not asserted).
# Output: $RENSGA_CI_OUT/psp-services/ (logs, shots/*.png). Exit 77 when the PSP Build Support module, an emulator, xvfb-run,
# rensga-xtool or ImageMagick is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless PPSSPPSDL xvfb-run python3 import; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
XTOOL="${RENSGA_XTOOL:-}"
if [ -z "$XTOOL" ]; then
  for c in "$CI_OUT"/linux-host/tools/xtool/rensga-xtool "$CI_OUT"/linux-host-svc/tools/xtool/rensga-xtool "$REPO"/build/host/tools/xtool/rensga-xtool; do
    [ -x "$c" ] && { XTOOL="$c"; break; }
  done
fi
[ -x "${XTOOL:-/nonexistent}" ] || { echo "skip: rensga-xtool not built (cmake --build <host tree> --target rensga-xtool)"; exit 77; }
HOST_PATH="$PATH"   # the module's cmake (3.28) cannot configure the host tree: host cmake runs with the PATH from before the module env
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
MODES="${*:-all}"; [ "$MODES" = all ] && MODES="headless sdl input"
PROJ="$REPO/samples/psp_services"
OUT="$CI_OUT/psp-services"; SHOTS="$OUT/shots"; B="$CI_OUT/psp-services-build"; HB="$CI_OUT/linux-host-svc"
mkdir -p "$OUT" "$SHOTS"
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
XV=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x800x24")
fail() { echo "psp_services: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
CLEAN_PIDS=""
cleanup() { for p in $CLEAN_PIDS; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT

if [ -z "${PSP_SERVICES_INNER:-}" ]; then
step "host cooker (RENSGA_PROJECT_DIR=psp_services)"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker
GENDIR="$PROJ/Library/Generated/psp"
step "cook for psp"
( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-psp_services.lock" | tail -3
PACK="$PROJ/Library/Cooked/psp/game.rpak"
[ -s "$PACK" ] || fail "no $PACK"
reflect_prescan "$B/reflect" "$PROJ"

fi

psp_build() { # <name> <cmake -D args...> -> $OUT/<name>/ with the ELF and the pack
  local name="$1"; shift
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE= -DRENSGA_EXIT_AFTER_FRAMES=0 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF "$@" >/dev/null
  rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -2
  rm -rf "$OUT/$name"; mkdir -p "$OUT/$name"
  cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$OUT/$name/"
}
# sandbox <dir> <GameLanguage> <ButtonPreference>: private HOME / XDG_CONFIG_HOME for PPSSPPSDL, with the project's key bindings
sandbox() {
  local d="$1"; rm -rf "$d"; mkdir -p "$d/home" "$d/cfg/ppsspp/PSP/SYSTEM"
  cat > "$d/cfg/ppsspp/PSP/SYSTEM/ppsspp.ini" <<EOF
[General]
FirstRun = False
CheckForNewVersion = False
AutoRun = True
RunCount = 10
RemoteDebuggerOnStartup = False
[SystemParam]
GameLanguage = $2
ButtonPreference = $3
EOF
  cp "$PROJ/ProjectSettings/Emulators/ppsspp/controls.ini" "$d/cfg/ppsspp/PSP/SYSTEM/controls.ini"
}
# run_sdl <sandbox dir> <elf dir>: starts PPSSPPSDL in the background (inside the xvfb the caller already holds); sets SDL_PID
run_sdl() {
  rm -f "$2/rensga.log"  # the game appends to host0:/rensga.log: start every run with an empty one
  ( cd "$2" && exec env XDG_CONFIG_HOME="$1/cfg" HOME="$1/home" SDL_AUDIODRIVER=dummy /usr/bin/PPSSPPSDL "$2/rensga_game.elf" > "$1/emu.out" 2>&1 ) &
  SDL_PID=$!
  CLEAN_PIDS="$CLEAN_PIDS $SDL_PID"
  "$XTOOL" wait-window "$SDL_PID" 30 > "$1/win" || fail "PPSSPPSDL opened no window (see $1/emu.out)"
  WIN="$(head -1 "$1/win")"
  "$XTOOL" focus "$WIN"
}
wait_log() { # <logfile> <regex> <seconds>
  local i; for i in $(seq 1 $(($3 * 5))); do grep -qE "$2" "$1" 2>/dev/null && return 0; sleep 0.2; done; return 1
}
snap() { import -window root "$1" 2>/dev/null || true; }
stop_sdl() { kill "$SDL_PID" 2>/dev/null || true; wait "$SDL_PID" 2>/dev/null || true; }

# ---- inner modes run inside one xvfb display --------------------------------------------------------------------------------------
if [ -n "${PSP_SERVICES_INNER:-}" ]; then
  case "$PSP_SERVICES_INNER" in
  sdl)
    for cfg in "2 0 french_east" "4 1 german_south"; do
      set -- $cfg; lang="$1" pref="$2" tag="$3"
      d="$OUT/sdl_$tag"; sandbox "$d" "$lang" "$pref"
      run_sdl "$d" "$OUT/svc_sdl"
      LOG="$OUT/svc_sdl/rensga.log"
      wait_log "$LOG" "services clock=" 40 || fail "sdl $tag: the Services scene never logged the clock line"
      cp "$LOG" "$d/boot.log"
      want_conf=south; [ "$pref" = 0 ] && want_conf=east
      need "services language=$lang " "$d/boot.log" "sdl $tag: GameLanguage=$lang did not reach the game"
      need "services confirm=$want_conf" "$d/boot.log" "sdl $tag: ButtonPreference=$pref should give confirm=$want_conf"
      grep -E "services (language|confirm|battery|memory|clock)" "$d/boot.log" | sed 's/^/  /'
      if [ "$tag" = french_east ]; then
        # the OSK opens at frame 60; keys are pad keys (arrows move, x = Circle selects, Start = Return finishes; the sample controls.ini maps Space to Cross), see the F5 probe notes
        wait_log "$LOG" "services osk open=1" 20 || fail "sdl: the OSK did not open"
        sleep 2; snap "$SHOTS/osk_open.png"
        for k in x Right x Down x; do "$XTOOL" key "$k" tap 80; sleep 0.5; done
        sleep 1; snap "$SHOTS/osk_typed.png"
        "$XTOOL" key Return tap 100
        wait_log "$LOG" "services name=" 15 || fail "sdl: no name= line after the OSK"
        grep "services name=" "$LOG" | tail -1 | sed 's/^/  /'
        grep -qE "services name=Ab" "$LOG" || fail "sdl: the OSK text does not start with the initial text 'Ab'"
        # the save dialog (LISTSAVE) opens next: Circle chooses the slot, Circle confirms the overwrite question if any
        wait_log "$LOG" "savedata save" 10 || fail "sdl: the save dialog did not start"
        sleep 2; snap "$SHOTS/save_dialog.png"
        # NEW DATA (Circle) saves at once and shows "Save completed." with only Back (Cross = Z in controls.ini) to close it
        "$XTOOL" key x tap 80; sleep 2.5; snap "$SHOTS/save_completed.png"
        "$XTOOL" key z tap 80
        # the load goes through the system dialog too (LISTLOAD): Circle picks the slot, Circle confirms, Cross closes the result
        wait_log "$LOG" "savedata load" 10 || fail "sdl: the load dialog did not start"
        n=0
        for k in x x z x z; do
          sleep 2; n=$((n + 1)); snap "$OUT/load_step_$n.png"
          grep -q "services load bytes=" "$LOG" && break
          "$XTOOL" key "$k" tap 80
        done
        cp "$OUT/load_step_1.png" "$SHOTS/load_dialog.png" 2>/dev/null || true
        wait_log "$LOG" "services done" 15 || { tail -5 "$LOG" >&2; fail "sdl: the save through the system dialog did not finish "; }
        grep -E "savedata|services (save|load|done)" "$LOG" | sed 's/^/  /'
        sleep 1; snap "$SHOTS/services_scene.png"
        sd="$d/cfg/ppsspp/PSP/SAVEDATA"
        [ -f "$sd"/RENSGASLOT0/DATA.BIN ] || { find "$sd" -type f | head; fail "sdl: no SAVEDATA/RENSGASLOT0/DATA.BIN"; }
        echo "  savedata file: $(ls -l "$sd"/RENSGASLOT0/DATA.BIN | awk '{print $5}') bytes in ${sd#$OUT/}/RENSGASLOT0"
      fi
      stop_sdl
    done
    echo "psp_services sdl: OK";;
  input)
    d="$OUT/sdl_input"; sandbox "$d" 1 1
    run_sdl "$d" "$OUT/inp_sdl"
    LOG="$OUT/inp_sdl/rensga.log"
    wait_log "$LOG" "cam ready" 40 || fail "input: the InputCam scene did not start"
    sleep 2; snap "$SHOTS/inputcam_0.png"
    y0="$(grep -o 'cam yaw=[-0-9]* ' "$LOG" | tail -1 || true)"
    # hold L (Left arrow) for 1.5 s, then R (Right arrow) for 3 s, then the nub up (W) for 2 s
    "$XTOOL" key Left down; sleep 1.5; "$XTOOL" key Left up; sleep 0.5; snap "$SHOTS/inputcam_L.png"
    y1="$(grep -o 'cam yaw=[-0-9]* ' "$LOG" | tail -1)"
    "$XTOOL" key Right down; sleep 3; "$XTOOL" key Right up; sleep 0.5
    y2="$(grep -o 'cam yaw=[-0-9]* ' "$LOG" | tail -1)"
    p0="$(grep -o 'player=[-0-9,]*' "$LOG" | tail -1)"
    "$XTOOL" key w down; sleep 2; "$XTOOL" key w up; sleep 0.5; snap "$SHOTS/inputcam_nub.png"
    p1="$(grep -o 'player=[-0-9,]*' "$LOG" | tail -1)"
    echo "  yaw: start=[${y0:-none}] after L=[$y1] after R=[$y2]"
    echo "  player: before nub=[$p0] after nub=[$p1]"
    [ "$y1" != "${y0:-}" ] || fail "input: holding L did not change the camera yaw"
    [ "$y2" != "$y1" ] || fail "input: holding R did not change the camera yaw"
    [ "$p1" != "$p0" ] || fail "input: holding the nub did not move the player"
    # F4.03: Home. The key is sent in this display; nothing maps it in controls.ini (PPSSPP has no PSP Home mapping).
    "$XTOOL" key Home tap 150; sleep 1
    if grep -q "input home bit seen" "$LOG"; then echo "  Home: the game saw PSP_CTRL_HOME"; else echo "  Home: nothing reached the game (no PSP_CTRL_HOME bit)"; fi
    kill -0 "$SDL_PID" 2>/dev/null && echo "  Home: emulator still running" || echo "  Home: emulator exited"
    stop_sdl
    echo "psp_services input: OK";;
  esac
  exit 0
fi

# ---- headless ---------------------------------------------------------------------------------------------------------------------------
if echo " $MODES " | grep -q " headless "; then
  step "headless: ServicesAuto, savedata AUTOSAVE, injected pause"
  psp_build svc_headless -DRENSGA_PSP_AUTOSTART_SCENE=ServicesAuto -DRENSGA_EXIT_AFTER_FRAMES=400 -DRENSGA_GU_DUMP_FRAMES="300"
  R="$OUT/svc_headless"; HH="$OUT/headless_home"; rm -rf "$HH"; mkdir -p "$HH"
  printf 'pause_at=150\nsavedata=auto\n' > "$R/rensga_test.txt"
  ( cd "$R" && HOME="$HH" timeout -s KILL 150 "${XV[@]}" PPSSPPHeadless "$R/rensga_game.elf" -r "$R" -l --graphics=software --timeout=90 > "$OUT/headless.emu" 2>&1 || true )
  cp "$R/rensga.log" "$OUT/headless.log" 2>/dev/null || : > "$OUT/headless.log"
  L="$OUT/headless.log"
  need "rensga: boot" "$L" "headless: the game did not start"
  need "services language=0 \(English\)" "$L" "headless: language line (PPSSPPHeadless always reports English)"
  need "services confirm=south" "$L" "headless: confirm line"
  need "services battery=" "$L" "headless: battery line"
  need "services clock=333 ok=1" "$L" "headless: clock=333 not read back"
  need "services memory system=[1-9]" "$L" "headless: memory line"
  need "services name=Rensga" "$L" "headless: name line"
  need "services save ok=1" "$L" "headless: savedata AUTOSAVE failed"
  need "services load bytes=7 name=Rensga" "$L" "headless: savedata did not read back"
  need "OnApplicationPause\(true\)" "$L" "headless: no OnApplicationPause(true)"
  need "OnApplicationPause\(false\)" "$L" "headless: no OnApplicationPause(false)"
  need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$L" "headless: alloc-check allocs=0"
  f="$(find "$HH" -name DATA.BIN | head -1)"
  [ -n "$f" ] || fail "headless: no DATA.BIN under the private HOME ($HH)"
  # PPSSPP stores DATA.BIN in the system's secure form (16 bytes of header + the 7 payload bytes), so the content check is the load
  # line above (AUTOLOAD decrypted it back to 'Rensga'); here only the file and its size.
  [ "$(stat -c %s "$f")" -ge 7 ] || fail "headless: DATA.BIN is smaller than the saved text"
  [ -f "$(dirname "$f")/PARAM.SFO" ] || fail "headless: no PARAM.SFO next to DATA.BIN"
  grep -E "services|OnApplicationPause|savedata|clock|alloc-check|test flags|input home" "$L" | sed 's/^/  /'
  echo "  savedata: ${f#$HH/}"
  for b in "$R"/rensga_shot_300.bmp; do [ -f "$b" ] && python3 -I "$REPO/tools/ci/bmp2png.py" "$b" "$SHOTS/services_auto.png" && echo "  shot $SHOTS/services_auto.png"; done
  echo "psp_services headless: OK"
fi

# ---- PPSSPPSDL modes (one xvfb display each) --------------------------------------------------------------------------------------
if echo " $MODES " | grep -q " sdl "; then
  step "sdl: language, confirm button, OSK, system save dialog"
  psp_build svc_sdl -DRENSGA_PSP_AUTOSTART_SCENE=
  PSP_SERVICES_INNER=sdl "${XV[@]}" "$0" sdl
fi
if echo " $MODES " | grep -q " input "; then
  step "input: InputCam with the nub and the shoulders"
  psp_build inp_sdl -DRENSGA_PSP_AUTOSTART_SCENE=InputCam
  PSP_SERVICES_INNER=input "${XV[@]}" "$0" input
fi
echo "psp_services: all requested modes passed"
