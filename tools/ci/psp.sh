#!/usr/bin/env bash
# CI job psp: cook templates/psp with the host CLI (builds it if needed), build the PSP ELF in the pspdev image
# (exits after 240 frames), boot it in PPSSPPHeadless (software renderer) and require the boot log line.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
CLI="$CI_OUT/linux-host/tools/cli/rensga"
if [ ! -x "$CLI" ]; then
  step "host CLI"; cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null; cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga
fi
PROJ="$REPO/templates/psp"; B="$CI_OUT/psp"; ROOT="$CI_OUT/psp-root"
step "cook"; "$CLI" cook "$PROJ"
step "psp build"
tools/docker/psp.sh cmake --preset psp -B "$B" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$PROJ/Library/Generated/psp" -DRENSGA_EXIT_AFTER_FRAMES=240 >/dev/null
tools/docker/psp.sh cmake --build "$B" --target rensga_game -j"$JOBS"
rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$PROJ/Library/Cooked/psp/game.rpak" "$ROOT/"
step "headless boot smoke test"
log="$(cd "$ROOT" && timeout -s KILL 90 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" --graphics=software --timeout=60 2>&1 || true)"
echo "$log" | tail -15
[ -f "$ROOT/rensga_shot.bmp" ] || { echo "PSP game did not reach its exit frame (no rensga_shot.bmp)" >&2; exit 1; }
echo "psp: ELF built and booted to frame 240"
