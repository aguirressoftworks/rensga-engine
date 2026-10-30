#!/usr/bin/env bash
# CI job linux-host: configure + build everything, ctest, editor-core console-name check, then the Engine 0 hello under xvfb.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
B="$CI_OUT/linux-host"
cd "$REPO"
step "linux-host build"; cmake --preset linux-host -B "$B" >/dev/null; cmake --build "$B" -j"$JOBS"
step "ctest"; ctest --test-dir "$B" --output-on-failure
# F2 host checks must stay registered (a missing CMake guard would silently drop them): touch (logic + SDL events), desktop pack hot
# reload, per-asset cook cache (inside rensga_cooker_tests), desktop --audio-stress with SDL_AUDIODRIVER=disk (non-silent).
step "F2 checks registered"
for t in touch_pad_logic touch_sdl_events hot_reload_desktop audio_sdl_disk audio_stress_desktop rensga_cooker_tests; do
  ctest --test-dir "$B" -N | grep -q " $t\$" || { echo "ctest: $t is not registered" >&2; exit 1; }
done
# Build support modules (no Docker for users) and the ISO writer: tests use temp RENSGA_HOME + file:// fixtures, never the network.
step "module + iso tests registered"
for t in rensga_modules_tests iso_tests; do
  ctest --test-dir "$B" -N | grep -q " $t\$" || { echo "ctest: $t is not registered" >&2; exit 1; }
done
[ -x "$B/tools/modules/rensga-modules" ] || { echo "linux-host: rensga-modules was not built" >&2; exit 1; }
# F3 exercises (ctest -L f3_exercises, run by the ctest above): every roadmap exercise stays registered.
step "F3 exercises registered"
for t in exercise_scene_roundtrip exercise_hot_reload exercise_play_revert exercise_door exercise_input_desktop exercise_patrol editor_assets_ui; do
  ctest --test-dir "$B" -N -L f3_exercises | grep -q " $t\$" || { echo "ctest: $t is not in the f3_exercises label" >&2; exit 1; }
done
"$B/tests/cooker/rensga_cooker_tests" assets_cook_cache_reuses_unchanged_assets
# ctest above includes alloc_check_linux (headless + desktop player allocs=0) and determinism_linux_trace.
step "check_editor_core"; tools/check_editor_core.sh
step "hello under xvfb"
out="$(xvfb-run -a "$B/engine/runtime/rensga_hello" --frames 130 2>&1)"; echo "$out"
echo "$out" | grep -q "rensga hello: boot" && echo "$out" | grep -q "frame 120" || { echo "hello did not log boot + frame heartbeats" >&2; exit 1; }
