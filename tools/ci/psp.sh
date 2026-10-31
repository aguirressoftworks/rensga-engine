#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job psp: cook templates/psp-3d with the host CLI (builds it if needed), build the PSP ELF with the PSP Build Support module (native; Docker only as contributor fallback)
# (exits after 240 frames), boot it in PPSSPPHeadless (software renderer) and require the boot log line.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
CLI="$CI_OUT/linux-host/tools/cli/rensga"
if [ ! -x "$CLI" ]; then
  step "host CLI"; cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null; cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga
fi
PROJ="$REPO/templates/psp-3d"; B="$CI_OUT/psp"; ROOT="$CI_OUT/psp-root"
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || exit 1
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
step "cook"; "$CLI" cook "$PROJ"
step "psp build"
reflect_prescan "$B/reflect" "$PROJ"
rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$PROJ/Library/Generated/psp" -DRENSGA_EXIT_AFTER_FRAMES=240 >/dev/null
rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS"
rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PROJ/Library/Cooked/psp/game.rpak" "$ROOT/"
step "headless boot smoke test"
log="$(cd "$ROOT" && timeout -s KILL 90 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" --graphics=software --timeout=60 2>&1 || true)"
echo "$log" | tail -15
[ -f "$ROOT/rensga_shot.bmp" ] || { echo "PSP game did not reach its exit frame (no rensga_shot.bmp)" >&2; exit 1; }
# the PSP log sink writes host0:/rensga.log; contract 7 (F2.G1): no operator new after the first frame
grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$ROOT/rensga.log" 2>/dev/null \
  || { echo "PSP game did not log alloc-check allocs=0 (see $ROOT/rensga.log)" >&2; exit 1; }
grep -h "alloc-check" "$ROOT/rensga.log" | tail -1
echo "psp: ELF built and booted to frame 240"
