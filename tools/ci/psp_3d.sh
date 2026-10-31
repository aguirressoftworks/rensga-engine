#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job psp_3d (F5.01 / F5.14 exercise, F5.11): samples/psp3d_walk (a lit room, a skinned character walking, fog, a nub-driven third-person camera
# script) cooked for psp, built as a PSP ELF with its scripts, run in PPSSPPHeadless (software renderer) with frame dumps; the same project on the
# Linux desktop player (softpipe) for a side-by-side PNG.
#
#   tools/ci/psp_3d.sh [mode...]       default: room
#     room      scene RoomBot: "rensga: gu draws= lights=4" lines, allocs=0, budget lines, pixel probes (lit vs shadow, the character moves
#               between dumps, fog), PNGs in $RENSGA_CI_OUT/psp3d/shots, desktop screenshot and a side-by-side
#     lights12  scene Lights12: lights=12 in the frame, at most 4 per object (hwlights<=4), allocs=0
#     perf      F5.11: the room at 222 and at 333 MHz (RENSGA_PSP_CPU_MHZ): prof table (emulator virtual time, not hardware timing)
#     det       tools/ci/determinism.sh 3dlit on the cooked samples/psp3d_room pack (DET_3DLIT_PACK)
#     all       room lights12 perf det
# Env: RENSGA_PSP_3D_PROJECT (default samples/psp3d_walk). Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
HOST_PATH="$PATH"
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
MODES="${*:-room}"; [ "$MODES" = all ] && MODES="room lights12 perf det"
PROJ="$(cd "${RENSGA_PSP_3D_PROJECT:-$REPO/samples/psp3d_walk}" && pwd)"
OUT="$CI_OUT/psp3d"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/psp3d-build"; HB="$CI_OUT/linux-host-walk"
mkdir -p "$OUT" "$SHOTS"
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)
fail() { echo "psp_3d: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }

step "host tree with the project scripts (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_desktop 2>&1 | tail -1
GENDIR="$PROJ/Library/Generated/psp"
step "cook for psp"
( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --force --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -3
PACK="$PROJ/Library/Cooked/psp/game.rpak"
[ -s "$PACK" ] || fail "no $PACK"
reflect_prescan "$B/reflect" "$PROJ"

psp_build() { # <cmake -D args...>
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE= -DRENSGA_EXIT_AFTER_FRAMES=0 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF "$@" >/dev/null
  rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -1
}
boot() { # <label> <timeout s> : runs the ELF in a clean host0 root; logs $OUT/<label>.log (+ .emu)
  local l="$1" t="$2"
  rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
  ( cd "$ROOT" && timeout -s KILL $((t + 90)) "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=software --timeout="$t" > "$OUT/$l.log.emu" 2>&1 || true )
  cp "$ROOT/rensga.log" "$OUT/$l.log" 2>/dev/null || : > "$OUT/$l.log"
  need "rensga: boot" "$OUT/$l.log" "$l: the game did not start"
  need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/$l.log" "$l: no alloc-check allocs=0"
  if grep -q "Unknown syscall" "$OUT/$l.log.emu"; then fail "$l: Unknown syscall in the emulator log"; fi
}
shots() { # <prefix>
  local f n
  for f in "$ROOT"/rensga_shot*.bmp; do
    [ -f "$f" ] || continue
    n="$(basename "$f" .bmp)"; n="${n#rensga_shot}"; n="${n#_}"; [ -n "$n" ] || n=end
    python3 -I "$REPO/tools/ci/bmp2png.py" "$f" "$SHOTS/$1_$n.png" > /dev/null || fail "$1: frame $n is a single color (blank)"
    cp "$f" "$OUT/$1_$n.bmp"
  done
}
profline() { grep -E "rensga: prof" "$1" | tail -1 | sed 's/.*rensga: prof //'; }

for m in $MODES; do
  case "$m" in
    room)
      step "room: RoomBot at frame 30, dumps at 60 120 180 240 (+124/136 around the fog switch), PPSSPPHeadless (software)"
      psp_build -DRENSGA_PSP_AUTOSTART_SCENE=RoomBot -DRENSGA_EXIT_AFTER_FRAMES=250 -DRENSGA_GU_DUMP_FRAMES="60,120,180,240,124,136"
      boot room 120
      need "rensga: gu draws=[0-9]+ lights=4 " "$OUT/room.log" "room: no 'rensga: gu draws= lights=4' line"
      need "rensga: rig ready player=1 auto=1" "$OUT/room.log" "room: the RoomRig script did not find the Character"
      need "rensga: rig fog off at frame 100" "$OUT/room.log" "room: the fog toggle did not run"
      need "rensga: budget scene=RoomBot " "$OUT/room.log" "room: no budget line for the scene"
      if grep -E "rensga: budget scene=RoomBot" "$OUT/room.log" | grep -q "status=OVER"; then fail "room: the scene is over budget"; fi
      grep -E "rensga: (gu draws|budget|rig ready|rig player)|vram tex" "$OUT/room.log" | sort -u | head -12 | sed 's/^/  /'
      shots room
      ls "$SHOTS"/room_*.png | sed 's/^/  shot /'
      python3 -I "$REPO/tests/psp_api/probe_room.py" "$OUT/room_60.bmp" "$OUT/room_120.bmp" "$OUT/room_180.bmp" "$OUT/room_240.bmp" "$OUT/room_124.bmp" "$OUT/room_136.bmp" | tee "$OUT/room_probes.txt"
      grep -q "probes: OK" "$OUT/room_probes.txt" || fail "room: pixel probes failed"
      # the same project on the Linux desktop player (softpipe) for a side-by-side picture
      step "room: desktop player (softpipe), scene RoomBot"
      env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga_desktop 2>&1 | tail -1
      "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target linux --force >/dev/null
      env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe SDL_VIDEODRIVER=x11 XDG_DATA_HOME="$OUT/desktop-save" xvfb-run -a -s "-screen 0 800x600x24" \
        "$HB/engine/runtime/rensga_desktop" --pack "$PROJ/Library/Cooked/linux/game.rpak" --scene RoomBot --frames 180 --screenshot "$SHOTS/room_desktop.png" > "$OUT/desktop.log" 2>&1 \
        || fail "room: rensga_desktop failed (see $OUT/desktop.log)"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/desktop.log" "room: desktop alloc-check not 0"
      convert "$SHOTS/room_180.png" "$SHOTS/room_desktop.png" -background '#202020' -gravity center -resize 'x272' +append "$SHOTS/room_side_by_side.png" 2>/dev/null \
        || montage "$SHOTS/room_180.png" "$SHOTS/room_desktop.png" -tile 2x1 -geometry +4+4 "$SHOTS/room_side_by_side.png"
      echo "  shot $SHOTS/room_side_by_side.png (left: PSP GU in PPSSPP, right: desktop GL)"
      echo "psp_3d room: OK";;
    lights12)
      step "lights12: scene Lights12 (12 point lights, 4 per object on the PSP)"
      psp_build -DRENSGA_PSP_AUTOSTART_SCENE=Lights12 -DRENSGA_EXIT_AFTER_FRAMES=190 -DRENSGA_GU_DUMP_FRAMES="150"
      boot lights12 120
      need "rensga: gu draws=[0-9]+ lights=12 " "$OUT/lights12.log" "lights12: no 'lights=12' line"
      grep -E "rensga: gu draws" "$OUT/lights12.log" | grep "lights=12" | tail -1 | sed 's/^/  /'
      shots lights12; ls "$SHOTS"/lights12_*.png | sed 's/^/  shot /'
      echo "psp_3d lights12: OK";;
    perf)
      step "perf: RoomBot at 222 and 333 MHz (emulator virtual time)"
      : > "$OUT/perf.md"; echo "| CPU MHz | frames | total_us avg/max | busy_us (total - present) | update_us | render_us (submit) | present_us (vsync wait) | audio_us |" >> "$OUT/perf.md"; echo "| --- | --- | --- | --- | --- | --- | --- | --- |" >> "$OUT/perf.md"
      for mhz in 222 333; do
        psp_build -DRENSGA_PSP_AUTOSTART_SCENE=RoomBot -DRENSGA_EXIT_AFTER_FRAMES=420 -DRENSGA_PSP_CPU_MHZ=$mhz
        boot perf$mhz 150
        pl="$(profline "$OUT/perf$mhz.log")"; [ -n "$pl" ] || fail "perf $mhz: no prof line"
        f() { echo "$pl" | tr ' ' '\n' | sed -n "s/^$1=//p" | head -1; }
        busy=$(( $(f total_us | cut -d/ -f1) - $(f present_us | cut -d/ -f1) ))
        echo "| $mhz | $(f frames) | $(f total_us) | $busy | $(f update_us) | $(f render_us) | $(f present_us) | $(f audio_us) |" >> "$OUT/perf.md"
        grep -E "clock set" "$OUT/perf$mhz.log" | head -1 | sed 's/^/  /'
      done
      cat "$OUT/perf.md"
      echo "  PPSSPP caps the frame at 60 fps (total_us ~16667) and runs the CPU in virtual time: the table shows relative cost only, not PSP hardware timing."
      echo "psp_3d perf: OK";;
    det)
      step "determinism 3dlit on the cooked samples/psp3d_room pack"
      "$HB/tools/cooker/rensga-cooker" cook --project "$REPO/samples/psp3d_room" --target psp --force >/dev/null 2>&1 || true
      RPACK="$REPO/samples/psp3d_room/Library/Cooked/psp/game.rpak"
      [ -s "$RPACK" ] || fail "no cooked psp3d_room pack ($RPACK)"
      env PATH="$HOST_PATH" DET_3DLIT_PACK="$RPACK" "$REPO/tools/ci/determinism.sh" 3dlit | tee "$OUT/det.log" | tail -8
      grep -q MATCH "$OUT/det.log" || fail "determinism 3dlit did not match"
      echo "psp_3d det: OK";;
    *) fail "unknown mode $m";;
  esac
done
