#!/usr/bin/env bash
# CI job ps2: build the EE ELFs (gsKit) and a bootable ISO (SYSTEM.CNF + RENSGA.ELF + GAME.RPK). Build only: PS2 boot is gate
# F8.G2 / local (tools/emulators/pcsx2_boot.sh); PCSX2 cannot run in public CI.
# Toolchain: the ps2-build module (ps2dev), installed with `rensga modules install ps2-build`; no Docker needed.
#      RENSGA_TOOLCHAIN=auto|native|docker (docker = optional contributor fallback, tools/docker/ps2.sh).
# Env: RENSGA_PS2_BUILD=<dir> (default build/ci/ps2), RENSGA_PS2_PACK=<game.rpak> (default templates/psp-3d cooked pack if present),
#      RENSGA_PS2_PROJECT=<project dir> (default templates/psp-3d): the game whose scripts are linked into rensga_ps2, e.g.
#        RENSGA_PS2_PROJECT=samples/platformer2d RENSGA_PS2_PACK=samples/platformer2d/Library/Cooked/ps2/game.rpak (cook it first)
#      Streamed music: <project>/Streaming/music.wav -> <pack dir>/music.wav (cooker) -> MUSIC.WAV on the disc.
#      RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_SHOT_FRAME=200 ..." extra cache entries (stress count, capture frame).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="${RENSGA_PS2_BUILD:-$CI_OUT/ps2}"
PROJ="${RENSGA_PS2_PROJECT:-templates/psp-3d}"; PROJ="$(cd "$PROJ" && pwd)"
reflect_prescan "$B/reflect" "$PROJ"
# Reset the test-only cache entries every run so an earlier stress/autostart build in this dir cannot leak into this one.
PS2_DEFAULTS="-DRENSGA_PS2_CHECK_FRAMES=300 -DRENSGA_PS2_AUTOSTART= -DRENSGA_PS2_VRAM_STRESS=OFF -DRENSGA_PS2_SAVE_SELFTEST=OFF -DRENSGA_PS2_FORMAT_BLANK_CARD=OFF -DRENSGA_PS2_AUDIO_STRESS=0 -DRENSGA_PS2_SHOT_FRAME=0"
. "$REPO/tools/modules/toolchain_env.sh" ps2-build ps2 || exit 1
BD="$B"; GEN=""; [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && { GEN="-G Ninja"; BD="$B/native"; }   # native: Ninja from build-tools, in its own tree (cache of the other backend)
rensga_tc sh -c "cmake --preset ps2 $GEN -B '$BD' -DRENSGA_REFLECT_DIR='$B/reflect' -DRENSGA_PROJECT_DIR='$PROJ' $PS2_DEFAULTS ${RENSGA_PS2_CMAKE_ARGS:-} >/dev/null && cmake --build '$BD' -j$JOBS --target rensga_hello rensga_ps2"
PACK="${RENSGA_PS2_PACK:-templates/psp-3d/Library/Cooked/psp/game.rpak}"
[ -f "$PACK" ] || PACK=""
# Streamed audio (Streaming/*.wav, copied next to the pack by the cooker) goes on the disc root too.
STREAMED=""
if [ -n "$PACK" ]; then for f in "$(dirname "$PACK")"/*.wav; do [ -f "$f" ] && STREAMED="$STREAMED $f"; done; fi
tools/ps2/make_iso.sh "$BD/engine/runtime/rensga_ps2.elf" "$B/rensga.iso" $PACK $STREAMED
ls -la "$BD/engine/runtime/rensga_hello.elf" "$BD/engine/runtime/rensga_ps2.elf" "$B/rensga.iso"
