#!/usr/bin/env bash
# CI job editor_tour (F3 exercises on the console side): cook samples/editor_tour, `rensga play --target psp` boots the dev build in
# PPSSPP (heartbeats, alloc-check allocs=0), `rensga build --target psp` writes an EBOOT.PBP. Needs the PSP Build Support module (or the Docker fallback) and PPSSPP, like psp.sh.
# Exit 77 = skipped (no PSP module / no PPSSPP).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "editor_tour: PSP Build Support module not installed, skipped"; exit 77; }
command -v xvfb-run >/dev/null 2>&1 || { echo "editor_tour: no xvfb-run, skipped"; exit 77; }
# The CLI registers the script types of the project it was built for (MVP limitation, see TASKLIST), so it gets its own tree.
PROJ="$REPO/samples/editor_tour"; H="$CI_OUT/editor_tour-host"; CLI="$H/tools/cli/rensga"
step "host CLI for editor_tour"
cmake --preset linux-host -B "$H" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
cmake --build "$H" -j"$JOBS" --target rensga
W="$CI_OUT/editor_tour"; rm -rf "$W"; mkdir -p "$W"
step "cook"; "$CLI" cook "$PROJ"
step "play (PPSSPP)"
xvfb-run -a "$CLI" play --target psp "$PROJ" --json --quit-after 25 > "$W/play.json" 2>&1 || { tail -30 "$W/play.json"; exit 1; }
grep -q '"type":"gamelog"' "$W/play.json" || { echo "editor_tour: the game logged nothing" >&2; tail -20 "$W/play.json"; exit 1; }
grep -qE 'rensga: frame [1-9][0-9]*[ "]' "$W/play.json" || { echo "editor_tour: no frame heartbeat" >&2; exit 1; }
grep -qE 'alloc-check frames=[1-9][0-9]* allocs=0' "$W/play.json" || { echo "editor_tour: no alloc-check allocs=0" >&2; exit 1; }
step "release build"
"$CLI" build --target psp "$PROJ"
[ -s "$PROJ/Library/Build/psp-release/EBOOT.PBP" ] || { echo "editor_tour: no EBOOT.PBP" >&2; exit 1; }
echo "editor_tour: played in PPSSPP and built EBOOT.PBP"
