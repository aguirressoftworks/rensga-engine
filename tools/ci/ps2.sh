#!/usr/bin/env bash
# CI job ps2: build the EE ELFs (gsKit) and a bootable ISO (SYSTEM.CNF + RENSGA.ELF + GAME.RPK) of the 2D SLICE (samples/platformer2d,
# F4.01): the slice is cooked here for the ps2 profile with a host cooker that has its scripts built in (tools/ci/lib.sh slice_cook).
# Build only in public CI: PS2 boot is local (tools/ci/ps2_check.sh, tools/emulators/pcsx2_boot.sh); PCSX2 cannot run in public CI.
# Toolchain: the ps2-build module (ps2dev), installed with `rensga modules install ps2-build`; no Docker needed.
#      RENSGA_TOOLCHAIN=auto|native|docker (docker = optional contributor fallback, tools/docker/ps2.sh).
# Env: RENSGA_PS2_BUILD=<dir> (default build/ci/ps2), RENSGA_PS2_PROJECT=<project dir> (default samples/platformer2d): the game whose
#      scripts are linked into rensga_ps2, RENSGA_PS2_PACK=<game.rpak> (default: the slice cooked for ps2 by this script; for another
#      project its <project>/Library/Cooked/ps2/game.rpak, cook it first). The pack is refused when GS cannot draw it (swizzled PSP
#      textures, MSHX meshes; tools/ci/pack_check.py): the 3D template's PSP pack is not a PS2 pack.
#      Streamed music: <project>/Streaming/music.wav -> <pack dir>/music.wav (cooker) -> MUSIC.WAV on the disc.
#      RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_SHOT_FRAME=200 ..." extra cache entries (stress count, capture frame).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="${RENSGA_PS2_BUILD:-$CI_OUT/ps2}"
PROJ="${RENSGA_PS2_PROJECT:-$SLICE_PROJ}"; PROJ="$(cd "$PROJ" && pwd)"
# Cook (system cmake, before the PS2 module's PATH is set).
if [ -n "${RENSGA_PS2_PACK:-}" ]; then PACK="$RENSGA_PS2_PACK"
elif [ "$PROJ" = "$SLICE_PROJ" ]; then step "cook the slice for ps2"; slice_cook ps2; PACK="$(SLICE_PACK ps2)"
else PACK="$PROJ/Library/Cooked/ps2/game.rpak"; fi
[ -f "$PACK" ] || { echo "ps2: no cooked pack at $PACK (cook the project for the ps2 target first)" >&2; exit 1; }
slice_pack_check "$PACK" || exit 1
reflect_prescan "$B/reflect" "$PROJ"
# Reset the test-only cache entries every run so an earlier stress/autostart build in this dir cannot leak into this one.
PS2_DEFAULTS="-DRENSGA_PS2_CHECK_FRAMES=300 -DRENSGA_PS2_AUTOSTART= -DRENSGA_PS2_VRAM_STRESS=OFF -DRENSGA_PS2_SAVE_SELFTEST=OFF -DRENSGA_PS2_FORMAT_BLANK_CARD=OFF -DRENSGA_PS2_AUDIO_STRESS=0 -DRENSGA_PS2_SHOT_FRAME=0"
. "$REPO/tools/modules/toolchain_env.sh" ps2-build ps2 || exit 1
BD="$B"; GEN=""; [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && { GEN="-G Ninja"; BD="$B/native"; }   # native: Ninja from build-tools, in its own tree (cache of the other backend)
rensga_tc sh -c "cmake --preset ps2 $GEN -B '$BD' -DRENSGA_REFLECT_DIR='$B/reflect' -DRENSGA_PROJECT_DIR='$PROJ' $PS2_DEFAULTS ${RENSGA_PS2_CMAKE_ARGS:-} >/dev/null && cmake --build '$BD' -j$JOBS --target rensga_hello rensga_ps2"
# Streamed audio (Streaming/*.wav, copied next to the pack by the cooker) goes on the disc root too.
STREAMED=""
if [ -n "$PACK" ]; then for f in "$(dirname "$PACK")"/*.wav; do [ -f "$f" ] && STREAMED="$STREAMED $f"; done; fi
tools/ps2/make_iso.sh "$BD/engine/runtime/rensga_ps2.elf" "$B/rensga.iso" $PACK $STREAMED
ls -la "$BD/engine/runtime/rensga_hello.elf" "$BD/engine/runtime/rensga_ps2.elf" "$B/rensga.iso"
