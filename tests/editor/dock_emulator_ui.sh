#!/bin/bash
# F5.23 + F5.10 in the real editor under xvfb (no window manager) with a real PPSSPPSDL:
#   1. Play on console: the PPSSPP window becomes a child of the editor window (rensga-xtool tree), sits over the docked Emulator panel
#      (screenshot of the whole X screen), follows the pointer for keyboard focus, Undock gives it back, Dock takes it again.
#   2. The Profiler panel shows live numbers from the game's 'rensga: prof' lines (screenshot); the PPSSPP debugger answers cpu.status on
#      its WebSocket after Relaunch with debugger.
#   3. RENSGA_EMBED=float (what native Wayland falls back to): the emulator stays a top-level window moved over the panel.
# Exit 77 = skipped (no xvfb-run, editor, rensga-xtool, ImageMagick import, PPSSPPSDL or psp-build module).
# usage: dock_emulator_ui.sh <rensga-editor> <project template dir> <work dir> <rensga-xtool>
EDITOR=$1; TPL=$2; WORK=$3; XTOOL=$4
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -x "$XTOOL" ] || { echo "skip: no rensga-xtool"; exit 77; }
command -v import >/dev/null 2>&1 || { echo "skip: no ImageMagick import (X screenshots)"; exit 77; }
PPSSPP=${RENSGA_PPSSPP:-$(command -v PPSSPPSDL || true)}
[ -n "$PPSSPP" ] || { echo "skip: no PPSSPPSDL"; exit 77; }
CLI=$(dirname "$EDITOR")/../../tools/cli/rensga
if [ -x "$CLI" ]; then "$CLI" modules verify psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed"; exit 77; }; fi
export RENSGA_TOOLCHAIN=${RENSGA_TOOLCHAIN:-native} RENSGA_JOBS=${RENSGA_JOBS:-2} RENSGA_NO_OPEN=1
rm -rf "$WORK"; mkdir -p "$WORK/cfg"; WORK=$(cd "$WORK" && pwd)
cp -r "$TPL" "$WORK/proj"; rm -rf "$WORK/proj/Library"
PROJ=$WORK/proj
XTOOL=$(cd "$(dirname "$XTOOL")" && pwd)/$(basename "$XTOOL")
EDITOR=$(cd "$(dirname "$EDITOR")" && pwd)/$(basename "$EDITOR")
trap 'pkill -f "PPSSPP.*$WORK/proj" 2>/dev/null; true' EXIT
fail() { echo "FAIL: $*"; tail -25 "$WORK/last.log" 2>/dev/null | cut -c1-240; pkill -f "PPSSPP.*$WORK/proj" 2>/dev/null; exit 1; }

# One editor run inside ONE xvfb: events given as arguments, `capture <name> <nth dock check>` steps are done by this wrapper while the editor holds.
# usage: run <name> <capture spec: "<line pattern>#<nth>#<shot name>"...> -- <ui events...>
run() {
  local name=$1; shift
  local caps=()
  while [ "$1" != "--" ]; do caps+=("$1"); shift; done; shift
  local args=() e
  for e in "$@"; do args+=(--ui-event "$e"); done
  LOG=$WORK/$name.log; ln -sf "$LOG" "$WORK/last.log"
  export WORK EDITOR PROJ XTOOL LOG
  export CAPS; CAPS=$(printf "%s\n" "${caps[@]}")
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" bash -c '
    "$EDITOR" --config-dir "$WORK/cfg" --project "$PROJ" --no-splash --no-auto-rebuild --focus Console \
      --screenshot "$WORK/$0-end.png" --screenshot-frames 40000 "$@" > "$LOG" 2>&1 &
    EPID=$!
    while IFS= read -r spec; do
      [ -n "$spec" ] || continue
      pat=${spec%%#*}; rest=${spec#*#}; nth=${rest%%#*}; shot=${rest#*#}
      for i in $(seq 1 1500); do [ "$(grep -c -- "$pat" "$LOG")" -ge "$nth" ] && break; kill -0 $EPID 2>/dev/null || break; sleep 0.2; done
      sleep 0.5
      import -window root "$WORK/$shot.png" 2>/dev/null
      EDW=$("$XTOOL" find-pid $EPID | head -1)
      "$XTOOL" tree "${EDW:-root}" > "$WORK/$shot.tree.txt" 2>&1
      "$XTOOL" tree root > "$WORK/$shot.roottree.txt" 2>&1
    done <<< "$CAPS"
    wait $EPID
  ' "$name" "${args[@]}" || true
}
val() { grep -o "$2" "$1" | tail -1; }

# ---- 1. docked: window tree, screenshot, focus follows the pointer, Undock / Dock, profiler, debugger ---------------------------------------
run dock \
  "dock: check=#1#docked" "profiler: samples=#1#profiler" \
  -- playconsole "consolewait:log=embed: attached|300" "consolewait:log=rensga: frame 180|90" emulator dumpembed "consolewait:log=NEVER_A|5" \
     embedpointer:in "consolewait:log=NEVER_B|1" dumpembed embedpointer:out "consolewait:log=NEVER_C|1" dumpembed \
     undockemu dumpembed dockemu "consolewait:log=NEVER_D|1" dumpembed \
     profilerport profiler profilerdump "consolewait:log=NEVER_E|4" profilerdump \
     consoledebugger profilerwait:90 profilerport profilerdebugger "consolewait:log=NEVER_G|3" shot:"$WORK/profiler_debugger.png" \
     consolestop "consolewait:stopped|30" quit
L=$WORK/dock.log
grep -q '^console: wait log=embed: attached done' "$L" || fail "the emulator window was never attached"
[ "$(grep -c '^dock: check=ok' "$L")" -ge 5 ] || fail "dumpembed check failed: $(grep '^dock: check' "$L" | tr '\n' ' ')"
grep -q '^dock: emu=.* descendant=1 .* focus=emulator' "$L" || fail "keyboard focus did not follow the pointer into the panel"
FEMU=$(grep '^dock: emu=' "$L" | sed -n 3p)
echo "$FEMU" | grep -q 'focus=editor' || fail "keyboard focus did not return to the editor when the pointer left: $FEMU"
grep -q '^dock: emu=.* descendant=0 .*state=released' "$L" || fail "Undock did not release the window"
# independent check with rensga-xtool: the PPSSPP window is a descendant of the editor window
EMU=$(grep -m1 -o '^dock: emu=0x[0-9a-f]*' "$L" | sed 's/.*emu=//')
grep -qi "$EMU" "$WORK/docked.tree.txt" || fail "xtool tree: $EMU is not below the editor window ($(head -3 "$WORK/docked.tree.txt" | tr '\n' ';'))"
echo "docked: xtool tree shows PPSSPP window $EMU under the editor window:"; head -6 "$WORK/docked.tree.txt"
# Profiler: live numbers
grep -q '^profiler: samples=[1-9].* prof_lines=[1-9]' "$L" || fail "the profiler got no 'rensga: prof' lines"
grep -q 'cpu_mhz=[1-9]' "$L" || fail "the profiler did not read the CPU clock"
echo "profiler: $(grep '^profiler: samples' "$L" | tail -1)"
# PPSSPP debugger: off at first, on after Relaunch with debugger
grep -q '^profiler: debugger-probe ok=0' "$L" || fail "the debugger probe should fail while the debugger is off"
grep -q '^profiler: debugger-probe ok=1 port=[0-9]* cpu.status' "$L" && grep -q '^profiler: wait debugger done' "$L" || fail "the debugger WebSocket did not answer cpu.status after the relaunch"
echo "debugger: $(grep '^profiler: debugger-probe ok=1' "$L" | tail -1)"
pgrep -f "PPSSPP.*$WORK/proj" >/dev/null && fail "emulator alive after Stop"

# ---- 2. floating fallback (RENSGA_EMBED=float, what native Wayland uses where it can) --------------------------------------------------------
RENSGA_EMBED=float run float \
  "dock: check=#1#floating" \
  -- playconsole "consolewait:log=embed: attached|300" "consolewait:log=rensga: frame 120|90" emulator dumpembed "consolewait:log=NEVER_F|3" dumpembed consolestop "consolewait:stopped|30" quit
L=$WORK/float.log
grep -q 'embed: backend=x11 mode=floating' "$L" || fail "RENSGA_EMBED=float did not select the floating mode"
grep -q '^dock: check=ok' "$L" || fail "floating check failed: $(grep '^dock:' "$L" | tr '\n' ' ')"
grep -q '^dock: emu=.* descendant=0' "$L" || fail "in floating mode the emulator must stay a top-level window"
echo "floating: $(grep -m1 '^dock: emu=' "$L")"
pgrep -f "PPSSPP.*$WORK/proj" >/dev/null && fail "emulator alive after the floating run"
# ---- 3. native Wayland path (forced: RENSGA_EMBED=wayland = the editor has no X11 window): no reparenting, the emulator stays its own window ------
RENSGA_EMBED=wayland run wayland \
  "dock: check=#1#wayland" \
  -- playconsole "consolewait:running|300" "consolewait:log=rensga: frame 120|90" emulator "consolewait:log=NEVER_H|3" dumpembed shot:"$WORK/wayland_panel.png" consolestop "consolewait:stopped|30" quit
L=$WORK/wayland.log
grep -q 'embed: backend=x11 mode=floating (no X11 window for the editor (native Wayland)' "$L" || fail "the Wayland fallback was not selected: $(grep 'embed:' "$L" | head -2)"
grep -q '^dock: emu=0 (not attached)' "$L" || fail "nothing may be attached on the Wayland path"
grep -q '^console: wait stopped done' "$L" || fail "the run did not end on the Wayland path"
pgrep -f "PPSSPP.*$WORK/proj" >/dev/null && fail "emulator alive after the Wayland run"
echo "wayland: fallback ran, emulator stayed a separate window (wayland_panel.png)"
echo "dock_emulator_ui: ok (screenshots $WORK/docked.png profiler.png floating.png wayland.png)"
