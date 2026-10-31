#!/bin/bash
# F5.19-F5.22 in the editor under xvfb with a real PPSSPPSDL: Play on console, Pause, Step 5 (the frame counter advances exactly 5),
# Resume, Stop (the status stays Running until the process is gone), Play/Stop x10 with no orphan process, a deliberately crashing
# and a hanging game reported in the Console (screenshots), console filters / search, Relaunch with debugger.
# Exit 77 = skipped (no xvfb-run, editor, PPSSPPSDL or psp-build module).
# usage: emu_play_ui.sh <rensga-editor> <project template dir> <work dir>
EDITOR=$1; TPL=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
PPSSPP=${RENSGA_PPSSPP:-$(command -v PPSSPPSDL || true)}
[ -n "$PPSSPP" ] || { echo "skip: no PPSSPPSDL"; exit 77; }
CLI=$(dirname "$EDITOR")/../../tools/cli/rensga
if [ -x "$CLI" ]; then "$CLI" modules verify psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed"; exit 77; }; fi
export RENSGA_TOOLCHAIN=${RENSGA_TOOLCHAIN:-native} RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK/cfg"; WORK=$(cd "$WORK" && pwd)
cp -r "$TPL" "$WORK/proj"; rm -rf "$WORK/proj/Library"
PROJ=$WORK/proj
alive() { pgrep -f "PPSSPP.*$WORK/proj" >/dev/null; }
fail() { echo "FAIL: $*"; [ -f "$1.log" ] || true; tail -30 "$WORK/last.log" 2>/dev/null | cut -c1-240; pkill -f "PPSSPP.*$WORK/proj" 2>/dev/null; exit 1; }
trap 'pkill -f "PPSSPP.*$WORK/proj" 2>/dev/null; true' EXIT
ed() {  # ed <name> <ui-event>...   runs the editor with these events and ends with `quit`
  local name=$1; shift
  local args=() e
  for e in "$@"; do args+=(--ui-event "$e"); done
  LOG=$WORK/$name.log; ln -sf "$LOG" "$WORK/last.log"
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$PROJ" --no-splash --no-auto-rebuild --focus Console \
    --screenshot "$WORK/$name-end.png" --screenshot-frames 40000 "${args[@]}" --ui-event quit > "$LOG" 2>&1 || true
}
val() { grep -o "$2 frame=[0-9]*" "$1" | tail -1 | sed 's/.*=//'; }

# ---- 1. Play, Pause, Step 5, Resume, Stop ----------------------------------------------------------------------------------------------
ed run1 playconsole "consolewait:running|300" "consolewait:log=rensga: frame 60|120" consolestate shot:"$WORK/running.png" \
   consolepause "consolewait:log=devctl paused|20" consolestate consolestep:5 "consolewait:log=devctl stepped 5|20" consolestate shot:"$WORK/paused.png" \
   consolestep:1 "consolewait:log=devctl stepped 1|20" consoleresume "consolewait:log=devctl resumed|20" "consolewait:log=rensga: frame 360|60" \
   consolectl:"apppause 1" "consolewait:log=devctl apppause 1|20" consolestop consolestate "consolewait:stopped|30" consolestate
L=$WORK/run1.log
grep -q '^console: status=Running paused=0 exit=running' "$L" || fail "status was not Running while the game ran"
grep -q '^console: pause ok=1' "$L" && grep -q '^console: status=Running paused=1 exit=running' "$L" || fail "pause did not show Running (paused)"
B=$(val "$L" "devctl step 5"); A=$(val "$L" "devctl stepped 5")
[ -n "$B" ] && [ "$((A - B))" -eq 5 ] || fail "step 5 advanced $((A - B)) frames ($B -> $A)"
B1=$(val "$L" "devctl step 1"); A1=$(val "$L" "devctl stepped 1")
[ "$((A1 - B1))" -eq 1 ] && [ "$B1" -eq "$A" ] || fail "step 1: $B1 -> $A1 (after step 5 at $A)"
R=$(val "$L" "devctl resumed"); [ "$R" -eq "$A1" ] || fail "resumed at $R, expected $A1"
# Stop: the toolbar status is still Running the moment Stop is pressed, and only Stopped once the process is gone
grep -q '^console: status=Running paused=0 exit=running' "$L" || fail "no Running state right before Stop"
grep -q '^console: status=Stopped paused=0 exit=exited code=0 detail=.stopped by the editor' "$L" || fail "Stop did not end as Stopped / stopped by the editor"
alive && fail "the emulator is still alive after Stop"
[ -s "$PROJ/Library/Logs/last-play.log" ] || fail "last-play.log not saved"
echo "run1: pause/step/resume/stop ok: step 5 $B..$A, step 1 $B1..$A1, resumed at $R; screenshots running.png paused.png"

# ---- 2. Play / Stop x10 ----------------------------------------------------------------------------------------------------------------------------
i=0; EV=()
while [ $i -lt 10 ]; do
  i=$((i + 1))
  EV+=(playconsole "consolewait:log=rensga: boot|120" consolestop "consolewait:stopped|30" consolestate)
done
ed loop10 "${EV[@]}"
L=$WORK/loop10.log
[ "$(grep -c '^console: status=Stopped paused=0 exit=exited' "$L")" -eq 10 ] || fail "not every one of the 10 Plays ended Stopped"
[ "$(grep -c '^console: wait .* TIMEOUT' "$L")" -eq 0 ] || fail "a wait timed out in the Play/Stop loop"
alive && fail "an emulator process is left after Play/Stop x10"
echo "loop10: Play/Stop x10 ok, no process left"

# ---- 3. a game that crashes: reported in the Console -------------------------------------------------------------------------------------------
ed crash playconsole "consolewait:log=rensga: frame 60|300" consolectl:crash "consolewait:crashed|40" consolestate shot:"$WORK/crashed.png" \
   consolesearch:crash shot:"$WORK/crash_search.png" consolesearch: "consolefilter:game=0" shot:"$WORK/crash_filter.png" "consolefilter:game=1" \
   consolestop "consolewait:stopped|30" consolestate
L=$WORK/crash.log
grep -q '^console: wait crashed done' "$L" || fail "the crash was not detected"
grep -q "^console: status=Running paused=0 exit=crashed .*detail='crashed:" "$L" || fail "the crash state/detail is missing"
alive && fail "emulator alive after crash test"
grep -q 'devctl crash' "$PROJ/Library/Logs/last-play.log" || fail "the crash log was not saved"
echo "crash: reported in the Console (crashed.png, crash_search.png, crash_filter.png)"

# ---- 4. a game that hangs ---------------------------------------------------------------------------------------------------------------------
RENSGA_HANG_TIMEOUT=4 ed hang playconsole "consolewait:log=rensga: frame 60|300" consolectl:hang "consolewait:hung|40" consolestate shot:"$WORK/hung.png" \
   consolestop "consolewait:stopped|30" consolestate
L=$WORK/hang.log
grep -q '^console: wait hung done' "$L" || fail "the hang was not detected"
grep -q "^console: status=Running paused=0 exit=hung .*detail='hung: no" "$L" || fail "the hang state/detail is missing"
alive && fail "emulator alive after hang test"
grep -q 'hung: no' "$PROJ/Library/Logs/last-play.log" || fail "the hang log was not saved"
echo "hang: reported in the Console (hung.png)"

# ---- 5. Relaunch with debugger ----------------------------------------------------------------------------------------------------------------
ed debugger playconsole "consolewait:log=rensga: frame 60|300" consoledebugger "consolewait:log=emulator debugger on|60" "consolewait:running|240" consolestate consolestop "consolewait:stopped|30"
INI=$PROJ/Library/Emulators/ppsspp/PSP/SYSTEM/ppsspp.ini
grep -q 'RemoteDebuggerOnStartup = True' "$INI" || fail "the debugger flag did not reach the sandbox ppsspp.ini"
grep -q '^console: relaunch-debugger ok=1' "$WORK/debugger.log" || fail "relaunch with debugger refused"
alive && fail "emulator alive after the debugger test"
echo "debugger: relaunch ok (RemoteDebuggerOnStartup = True)"
echo "emu_play_ui: ok"
