#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F4.02: the shared hello world (engine/runtime/main_hello.cpp) as a PSP EBOOT.PBP, built and launched from this one script:
# psp preset with RENSGA_PSP_PRX=ON -> rensga_hello.elf.prx -> mksfoex + pack-pbp -> EBOOT.PBP -> PPSSPPHeadless, then the
# script greps the hello log line. The build quits after RENSGA_EXIT_AFTER_FRAMES frames (PPSSPPHeadless cannot pass --frames).
# Exit 77 when the PSP Build Support module, PPSSPPHeadless or xvfb-run is missing, or when the tree has no PSP rensga_hello target yet.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
PROJ="$REPO/templates/psp-3d"; B="$CI_OUT/psp-hello"; W="$CI_OUT/psp-hello-run"; FRAMES=120
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
step "psp build (rensga_hello, PRX)"
reflect_prescan "$B/reflect" "$PROJ"
rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" \
  -DRENSGA_GENERATED_DIR="$PROJ/Library/Generated/psp" -DRENSGA_PSP_PRX=ON -DRENSGA_EXIT_AFTER_FRAMES=$FRAMES >/dev/null
if ! rensga_tc cmake --build "$B" --target help | grep -q "rensga_hello"; then
  echo "psp_hello: no PSP rensga_hello target in this tree yet"; exit 77
fi
rensga_tc cmake --build "$B" --target rensga_hello -j"$JOBS"
PRX="$B/engine/runtime/rensga_hello.elf.prx"
[ -f "$PRX" ] || PRX="$B/engine/runtime/rensga_hello.prx"
[ -f "$PRX" ] || { echo "psp_hello: no rensga_hello PRX in $B/engine/runtime" >&2; exit 1; }
step "EBOOT.PBP"
rm -rf "$W"; mkdir -p "$W/host" "$W/home"
rensga_tc mksfoex -d MEMSIZE=1 "Rensga Hello" "$W/PARAM.SFO"
rensga_tc pack-pbp "$W/host/EBOOT.PBP" "$W/PARAM.SFO" "$REPO/editor/targets/psp/assets/ICON0.png" NULL NULL NULL NULL "$PRX" NULL >/dev/null
ls -l "$W/host/EBOOT.PBP" | awk '{print "  " $5 " bytes  " $9}'
step "headless boot"
log="$(cd "$W" && HOME="$W/home" QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 timeout -s KILL 90 xvfb-run -a \
  PPSSPPHeadless "$W/host/EBOOT.PBP" -r "$W/host" --graphics=software --timeout=60 2>&1 || true)"
echo "$log" | tail -15
# The hello main logs through the engine logger: stdout (shown by PPSSPPHeadless) and host0:/rensga.log when the PSP sinks are installed.
all="$log"; [ -f "$W/host/rensga.log" ] && all="$log"$'\n'"$(cat "$W/host/rensga.log")"
echo "$all" | grep -q "rensga hello: boot" || { echo "psp_hello: no boot line" >&2; exit 1; }
echo "$all" | grep -qE "rensga hello: done frames=$FRAMES" || { echo "psp_hello: did not reach frame $FRAMES" >&2; exit 1; }
echo "$all" | grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" || { echo "psp_hello: no alloc-check allocs=0 line" >&2; exit 1; }
echo "$all" | grep -E "hello: (boot|done)|alloc-check" | head -4
echo "psp_hello: hello EBOOT.PBP booted to frame $FRAMES"
