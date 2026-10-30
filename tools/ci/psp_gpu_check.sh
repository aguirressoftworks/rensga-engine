#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# PSP GPU-renderer check (the MVP black-screen bug class): run the PSP ELF in PPSSPPHeadless --graphics=gles under xvfb
# and fail on any "Unknown syscall" in the full emulator log. Builds the ELF via tools/ci/psp.sh when missing.
# Exit 77 (ctest SKIP_RETURN_CODE) when the PSP Build Support module (or Docker fallback) or PPSSPPHeadless / xvfb-run are unavailable.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
ROOT="$CI_OUT/psp-root"
[ -f "$ROOT/rensga_game.elf" ] || tools/ci/psp.sh
rm -f "$ROOT/rensga_shot.bmp"
# PPSSPP can hang on GL teardown after the game ran to its exit frame, hence KILL.
log="$(cd "$ROOT" && timeout -s KILL 90 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics=gles --timeout=60 2>&1 || true)"
n="$(echo "$log" | grep -c "Unknown syscall" || true)"
echo "gles run: $(echo "$log" | wc -l) log lines, Unknown syscall x$n"
if [ "$n" != 0 ]; then echo "$log" | grep "Unknown syscall" | sort | uniq -c | head; exit 1; fi
[ -f "$ROOT/rensga_shot.bmp" ] || { echo "game did not reach its exit frame under gles" >&2; exit 1; }
echo "psp gpu check: OK"
