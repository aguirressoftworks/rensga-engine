#!/usr/bin/env bash
# CI job ps2: build the EE ELFs (gsKit) and a bootable ISO (SYSTEM.CNF + RENSGA.ELF + GAME.RPK). Build only: PS2 boot is gate
# F8.G2 / local (tools/emulators/pcsx2_boot.sh); PCSX2 cannot run in public CI.
# Env: RENSGA_PS2_BUILD=<dir> (default build/ci/ps2), RENSGA_PS2_PACK=<game.rpak> (default templates/psp cooked pack if present),
#      RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_SHOT_FRAME=200 ..." extra cache entries (stress count, capture frame).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="${RENSGA_PS2_BUILD:-$CI_OUT/ps2}"
reflect_prescan "$B/reflect"
tools/docker/ps2.sh sh -c "cmake --preset ps2 -B '$B' -DRENSGA_REFLECT_DIR='$B/reflect' ${RENSGA_PS2_CMAKE_ARGS:-} >/dev/null && cmake --build '$B' -j$JOBS --target rensga_hello rensga_ps2"
PACK="${RENSGA_PS2_PACK:-templates/psp/Library/Cooked/psp/game.rpak}"
[ -f "$PACK" ] || PACK=""
tools/docker/ps2.sh tools/ps2/make_iso.sh "$B/engine/runtime/rensga_ps2.elf" "$B/rensga.iso" $PACK
ls -la "$B/engine/runtime/rensga_hello.elf" "$B/engine/runtime/rensga_ps2.elf" "$B/rensga.iso"
