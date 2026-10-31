#!/bin/sh
# F5.19-F5.22 on a real PPSSPP (PPSSPPSDL under xvfb), through the CLI: Play, Pause, Step N (exactly N frames), Resume, Stop;
# Play/Stop x10 leaves no process; a game that crashes and one that hangs are reported (exit 3 / 4) and the log is saved;
# the second Play after no change and after one asset change is fast.
# Exit 77 = skipped (no xvfb-run, no PPSSPPSDL, no psp-build module).
# usage: emu_play_cli.sh <rensga cli> <project template dir> <work dir>
CLI=$1; TPL=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$CLI" ] || { echo "skip: no rensga cli"; exit 77; }
PPSSPP=${RENSGA_PPSSPP:-$(command -v PPSSPPSDL || true)}
[ -n "$PPSSPP" ] || { echo "skip: no PPSSPPSDL"; exit 77; }
"$CLI" modules verify psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed"; exit 77; }
export RENSGA_TOOLCHAIN=${RENSGA_TOOLCHAIN:-native} RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK"; WORK=$(cd "$WORK" && pwd)
cp -r "$TPL" "$WORK/proj"; rm -rf "$WORK/proj/Library"
PROJ=$WORK/proj
fail() { echo "FAIL: $*"; [ -f "$WORK/last.txt" ] && tail -25 "$WORK/last.txt" | cut -c1-240; cleanup; exit 1; }
# the emulator of this test is the only process whose command line holds both "PPSSPP" and the work dir (the ELF path)
alive() { pgrep -f "PPSSPP.*$WORK/proj" >/dev/null; }
cleanup() { pkill -f "PPSSPP.*$WORK/proj" 2>/dev/null; true; }
trap cleanup EXIT
GUI="env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a"
now_ms() { date +%s%3N; }

# play_bg <out> <fifo> [extra cli args]: starts the CLI with stdin control on a fifo; sets CLIPID-less (we poll the out file).
play_bg() {
  OUT=$1; FIFO=$2; shift 2
  rm -f "$FIFO"; mkfifo "$FIFO"
  ( $GUI "$CLI" play --target psp --json --stdin-control "$@" "$PROJ" < "$FIFO" > "$OUT" 2>&1; echo "EXIT $?" >> "$OUT" ) &
  exec 7> "$FIFO"
}
waitfor() {  # waitfor <file> <regex> <seconds>
  i=0; while [ $i -lt $(($3 * 4)) ]; do grep -Eq "$2" "$1" 2>/dev/null && return 0; sleep 0.25; i=$((i + 1)); done; return 1
}
send() { echo "$1" >&7; }
# start_until <out> <fifo> <regex> <seconds> [cli args]: starts the CLI and waits for <regex> in its output. PPSSPP sometimes cannot open its
# OpenGL context under a loaded xvfb ("Failed graphics backends"): that is the emulator, not the pipeline, so the start is retried (3 times).
start_until() {
  SU_OUT=$1; SU_FIFO=$2; SU_RE=$3; SU_SECS=$4; shift 4; SU_TRIES=0
  while :; do
    play_bg "$SU_OUT" "$SU_FIFO" "$@"
    waitfor "$SU_OUT" "$SU_RE|Failed graphics backends" "$SU_SECS" || return 1
    grep -q 'Failed graphics backends' "$SU_OUT" || return 0
    SU_TRIES=$((SU_TRIES + 1)); echo "note: PPSSPP could not open OpenGL, retrying ($SU_TRIES)"
    [ $SU_TRIES -le 3 ] || return 1
    send stop; waitfor "$SU_OUT" '^EXIT' 30; exec 7>&-
  done
}
finish() {  # finish <file>: stop and wait for the CLI to exit
  send stop; waitfor "$1" '^EXIT' 20 || fail "the CLI did not exit after stop"; exec 7>&-
}

# ---- 1. first Play (cold: cook + compile), Pause, Step N, Resume, Stop ----------------------------------------------------------------
LAST=$WORK/last.txt; OUT1=$WORK/play1.txt; ln -sf "$OUT1" "$LAST"
start_until "$OUT1" "$WORK/ctl1" 'rensga: frame 60' 300 || fail "first Play: the game never reached frame 60"
grep -q 'pipeline: stage compile' "$OUT1" || fail "no compile timing line"
send pause
waitfor "$OUT1" 'devctl paused frame=' 10 || fail "pause: the game did not acknowledge"
P=$(grep -o 'devctl paused frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
sleep 1.5
send "step 5"
waitfor "$OUT1" 'devctl stepped 5 frame=' 10 || fail "step 5: no 'stepped' line"
B=$(grep -o 'devctl step 5 frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
A=$(grep -o 'devctl stepped 5 frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
[ "$((A - B))" -eq 5 ] || fail "step 5 advanced $((A - B)) frames (before=$B after=$A)"
[ "$B" -eq "$P" ] || fail "the game moved while paused (paused at $P, step began at $B)"
send "step 1"
waitfor "$OUT1" 'devctl stepped 1 frame=' 10 || fail "step 1: no 'stepped' line"
B1=$(grep -o 'devctl step 1 frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
A1=$(grep -o 'devctl stepped 1 frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
[ "$((A1 - B1))" -eq 1 ] && [ "$B1" -eq "$A" ] || fail "step 1: before=$B1 after=$A1 (previous step ended at $A)"
echo "pause/step: paused at frame $P, step 5 -> $B..$A (+$((A - B))), step 1 -> $B1..$A1 (+$((A1 - B1)))"
# the heartbeat keeps running while paused (the loop still presents frames), so a paused game is not a hung game
HB=$(grep -c 'rensga: frame ' "$OUT1"); sleep 2.5; HB2=$(grep -c 'rensga: frame ' "$OUT1")
[ "$HB2" -gt "$HB" ] || fail "no heartbeat while paused"
send resume
waitfor "$OUT1" 'devctl resumed frame=' 10 || fail "resume: no acknowledgement"
R=$(grep -o 'devctl resumed frame=[0-9]*' "$OUT1" | tail -1 | sed 's/.*=//')
[ "$R" -eq "$A1" ] || fail "resume began at frame $R, expected $A1"
sleep 1.5
send "ctl apppause 1"; waitfor "$OUT1" 'devctl apppause 1' 10 || fail "apppause 1 not acknowledged"
send "ctl apppause 0"; waitfor "$OUT1" 'devctl apppause 0' 10 || fail "apppause 0 not acknowledged"
finish "$OUT1"
grep -q '"state":"exited"' "$OUT1" && grep -q '"detail":"stopped by the editor"' "$OUT1" || fail "stop was not reported as a clean stop"
[ -s "$PROJ/Library/Logs/last-play.log" ] || fail "last-play.log was not saved"
grep -q 'rensga: boot' "$PROJ/Library/Logs/last-play.log" || fail "last-play.log has no game lines"
[ "$(grep -c 'rensga: boot' "$OUT1")" -eq 1 ] || fail "the boot line appears more than once (stdout and file copies not merged)"
alive && fail "a process of this session is still alive after Stop"
echo "play1: ok"

# ---- 2. Play / Stop x10, no orphans ----------------------------------------------------------------------------------------------------------
n=0
while [ $n -lt 10 ]; do
  n=$((n + 1)); O=$WORK/loop$n.txt; ln -sf "$O" "$LAST"
  start_until "$O" "$WORK/ctl2" 'rensga: boot' 90 || fail "Play $n: no boot line"
  finish "$O"
  alive && fail "Play $n: a process is still alive after Stop"
done
echo "play/stop x10: ok (no process left)"

# ---- 3. timings: second Play with no change, then after one asset change -------------------------------------------------------------------
# "ready" = the pipeline is done and the emulator launched; "boot" = wall time from starting the CLI to the game's own 'rensga: boot' line.
ready() { grep -o 'pipeline: ready [0-9]* ms' "$1" | tail -1 | grep -o '[0-9]*'; }
timed_play() {  # timed_play <name>: sets BOOT_MS and READY_MS
  O=$WORK/t_$1.txt; ln -sf "$O" "$LAST"
  T0=$(now_ms); start_until "$O" "$WORK/ctl3" 'rensga: boot' 90 || fail "$1: no boot line"
  BOOT_MS=$(($(now_ms) - T0)); finish "$O"; READY_MS=$(ready "$O")
}
timed_play same
grep -q 'compile skipped' "$WORK/t_same.txt" || fail "no change, but the compile was not skipped"
R1=$READY_MS; B1T=$BOOT_MS
[ "$B1T" -lt 10000 ] || fail "second Play took ${B1T} ms to boot"
SCENE=$(find "$PROJ/Assets" -name '*.scene' | head -1)
python3 - "$SCENE" <<'PY' || fail "could not change the scene"
import json, sys
p = sys.argv[1]
d = json.load(open(p))
for o in d["objects"]:
    for c in o.get("components", []):
        if c["type"] == "MeshRenderer" and "color" in c.get("fields", {}):
            c["fields"]["color"][0] = (c["fields"]["color"][0] + 17) % 256
            json.dump(d, open(p, "w"), indent=1)
            sys.exit(0)
sys.exit(1)
PY
timed_play asset
grep -q '"message":"cooked: pack' "$WORK/t_asset.txt" || fail "the changed scene was not re-cooked"
grep -q 'compile skipped' "$WORK/t_asset.txt" || fail "an asset change recompiled the game"
R2=$READY_MS; B2T=$BOOT_MS
[ "$B2T" -lt 10000 ] || fail "Play after an asset change took ${B2T} ms to boot"
echo "timings: Play after no change: ready ${R1} ms, game booted ${B1T} ms; after one asset change: ready ${R2} ms, game booted ${B2T} ms"

# ---- 4. a game that crashes ----------------------------------------------------------------------------------------------------------------------
ln -sf "$WORK/crash.txt" "$LAST"; rm -f "$PROJ/Library/Logs/last-play.log"
start_until "$WORK/crash.txt" "$WORK/ctl4" 'rensga: frame 60' 90 --hang-timeout 20 || fail "crash run: the game never reached frame 60"
send "ctl crash"
waitfor "$WORK/crash.txt" '^EXIT' 40 || fail "the crash was not detected"
exec 7>&-
grep -q '^EXIT 3' "$WORK/crash.txt" || fail "crash: expected exit code 3"
grep -q '"state":"crashed"' "$WORK/crash.txt" || fail "crash: no crashed state in the exit line"
grep -q 'devctl crash' "$PROJ/Library/Logs/last-play.log" || fail "the crash log was not saved with the game's last line"
alive && fail "crash: a process is still alive"
echo "crash: reported ($(grep -o '"detail":"[^"]*"' "$WORK/crash.txt" | tail -1))"

# ---- 5. a game that hangs ------------------------------------------------------------------------------------------------------------------------
ln -sf "$WORK/hang.txt" "$LAST"; rm -f "$PROJ/Library/Logs/last-play.log"
start_until "$WORK/hang.txt" "$WORK/ctl5" 'rensga: frame 60' 90 --hang-timeout 4 || fail "hang run: the game never reached frame 60"
send "ctl hang"
waitfor "$WORK/hang.txt" '^EXIT' 40 || fail "the hang was not detected"
exec 7>&-
grep -q '^EXIT 4' "$WORK/hang.txt" || fail "hang: expected exit code 4"
grep -q '"state":"hung"' "$WORK/hang.txt" || fail "hang: no hung state in the exit line"
grep -q 'hung: no' "$PROJ/Library/Logs/last-play.log" || fail "the hang log was not saved"
alive && fail "hang: a process is still alive"
echo "hang: reported ($(grep -o '"detail":"[^"]*"' "$WORK/hang.txt" | tail -1))"
echo "emu_play_cli: ok"
