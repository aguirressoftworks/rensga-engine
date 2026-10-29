#!/usr/bin/env bash
# CI job ps2: build the EE ELF (gsKit) and a bootable ISO (SYSTEM.CNF + RENSGA.ELF). Build only: PS2 boot is gate F8.G2.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"; B="$CI_OUT/ps2"
tools/docker/ps2.sh sh -c "cmake --preset ps2 -B '$B' >/dev/null && cmake --build '$B' -j$JOBS --target rensga_hello"
tools/docker/ps2.sh tools/ps2/make_iso.sh "$B/engine/runtime/rensga_hello.elf" "$B/rensga.iso"
ls -la "$B/engine/runtime/rensga_hello.elf" "$B/rensga.iso"
