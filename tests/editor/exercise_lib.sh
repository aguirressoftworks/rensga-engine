# bash only.
# Shared helpers for the F3 roadmap exercise tests (label f3_exercises). Source with `. exercise_lib.sh`.
# Every test copies samples/editor_tour to a temp dir, runs rensga-editor under xvfb with --ui-event verbs and checks the log.
# Exit 77 = skipped (no xvfb-run / python3 / editor binary).
ex_init() {  # <editor> <tour dir> <work dir>
  EDITOR_BIN="$1"; TOUR="$2"; WORK="$3"
  command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
  command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
  [ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
  rm -rf "$WORK"; mkdir -p "$WORK/home"
  cp -r "$TOUR" "$WORK/proj" && rm -rf "$WORK/proj/Library"
  export RENSGA_HOME="$WORK/home" RENSGA_JOBS=${RENSGA_JOBS:-2}
  export RENSGA_EXTERNAL_EDITOR="$WORK/stub_editor.sh"   # never launch a real editor
  printf '#!/bin/sh\necho "$@" >> "%s/editor_argv.txt"\n' "$WORK" > "$RENSGA_EXTERNAL_EDITOR"; chmod +x "$RENSGA_EXTERNAL_EDITOR"
}
ex_fail() { echo "FAIL: $1"; [ -n "${LOG:-}" ] && tail -60 "$LOG"; exit 1; }
ex_run() {  # <log> <events...>: the editor runs with the background script build on and quits after the last event
  LOG="$1"; shift
  local args=() e
  for e in "$@"; do args+=(--ui-event "$e"); done
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR_BIN" --project "$WORK/proj" --no-splash --config-dir "$WORK/home/.config/rensga" --auto-rebuild --quit-after 2400 \
    "${args[@]}" > "$LOG" 2>&1
  EX_RC=$?
}
