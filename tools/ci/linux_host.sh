#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
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
# F4.01 / F4.G2 (Linux): the 2D slice (samples/platformer2d) on the desktop player. A host tree with the slice scripts built in
# (rensga-cooker + rensga_desktop), the slice cooked for the ps2 profile (the pack Windows and Android share), then TitleBot (the bot plays
# all three rooms) under xvfb: "game complete", "bot finished", alloc-check allocs=0 and a screenshot of the Results scene.
# GALLIUM_DRIVER=softpipe: llvmpipe's shader JIT allocates through operator new (a Mesa artefact, not the engine); softpipe does not.
step "2D slice on the desktop player"
slice_tools rensga-cooker rensga_desktop
slice_cook ps2 >/dev/null
SP="$(SLICE_PACK ps2)"; slice_pack_check "$SP"
SD="$CI_OUT/linux-slice-run"; rm -rf "$SD"; mkdir -p "$SD/xdg"
run_slice() {  # run_slice <log> <desktop args...>: empty save folder, software GL, private X server
  local log="$1"; shift
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 GALLIUM_DRIVER=softpipe LIBGL_ALWAYS_SOFTWARE=1 XDG_DATA_HOME="$SD/xdg" \
    timeout -s KILL "${SLICE_TIMEOUT:-300}" xvfb-run -a -s "-screen 0 1280x720x24" "$SLICE_TOOLS/engine/runtime/rensga_desktop" --pack "$SP" "$@" >"$log" 2>&1
}
run_slice "$SD/title.log" --scene Title --frames 120 --screenshot "$SD/linux_title.png" || { cat "$SD/title.log" >&2; echo "slice Title did not run (exit $?)" >&2; exit 1; }
run_slice "$SD/bot.log" --scene TitleBot --frames "${SLICE_FRAMES:-1500}" --screenshot "$SD/linux_results.png" || { tail -20 "$SD/bot.log" >&2; echo "slice TitleBot run failed (exit $?; 4 = alloc-check not 0)" >&2; exit 1; }
grep -E "room cleared|game complete|bot finished|alloc-check|quit after" "$SD/bot.log" | tail -7
# coins: 19 of 19 when frames are fast; a very slow software-GL frame can cost the bot a coin (wine run: 18), so any count passes
grep -q "rensga: game complete coins=[0-9]" "$SD/bot.log" || { echo "slice: the bot did not complete the game" >&2; exit 1; }
grep -q "rensga: bot finished the game" "$SD/bot.log" || { echo "slice: the bot did not reach the results scene" >&2; exit 1; }
grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$SD/bot.log" || { echo "slice: alloc-check is not allocs=0" >&2; exit 1; }
! grep -qE "upload failed|ERROR \[" "$SD/bot.log" || { grep -E "upload failed|ERROR \[" "$SD/bot.log" | head -5 >&2; echo "slice: errors in the log" >&2; exit 1; }
[ -s "$SD/linux_title.png" ] && [ -s "$SD/linux_results.png" ] || { echo "slice: no screenshot" >&2; exit 1; }
echo "slice (linux): ok, screenshots in $SD"
