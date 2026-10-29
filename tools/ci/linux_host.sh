#!/usr/bin/env bash
# CI job linux-host: configure + build everything, ctest, editor-core console-name check, then the Engine 0 hello under xvfb.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
B="$CI_OUT/linux-host"
cd "$REPO"
step "linux-host build"; cmake --preset linux-host -B "$B" >/dev/null; cmake --build "$B" -j"$JOBS"
step "ctest"; ctest --test-dir "$B" --output-on-failure
step "check_editor_core"; tools/check_editor_core.sh
step "hello under xvfb"
out="$(xvfb-run -a "$B/engine/runtime/rensga_hello" --frames 130 2>&1)"; echo "$out"
echo "$out" | grep -q "rensga hello: boot" && echo "$out" | grep -q "frame 120" || { echo "hello did not log boot + frame heartbeats" >&2; exit 1; }
