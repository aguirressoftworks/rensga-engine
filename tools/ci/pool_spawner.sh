#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job pool_spawner (F6.13, API 12): samples/pool_spawner, a projectile spawner with and without GameObjectPool, measured with the
# Profiler::BeginSample markers "Spawn" and "Move" that the runtime prints as "rensga: prof sample ..." lines.
#
#   tools/ci/pool_spawner.sh [mode...]       default mode: headless desktop
#     headless  PPSSPPHeadless, two builds: scene Plain (Instantiate + Destroy per shot) and scene Pooled (GameObjectPool<96>). The
#               pooled run must show allocs=0, no refused spawns and the same number of live shots; the table compares the samples.
#               PPSSPPHeadless is the software renderer on your PC: the microseconds are EMULATOR time (the host CPU running the
#               emulated PSP), only the ratio between the two runs means something.
#     desktop   the same two scenes on the desktop player (native time, softpipe renderer under xvfb).
# Output: $RENSGA_CI_OUT/pool-spawner/ (logs, the comparison table in summary.txt). Exit 77 when a needed tool is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
MODES="${*:-headless desktop}"
for t in xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
PROJ="$REPO/samples/pool_spawner"
OUT="$CI_OUT/pool-spawner"; HB="$CI_OUT/linux-host-pool"; B="$CI_OUT/pool-spawner-build"
mkdir -p "$OUT"; SUMMARY="$OUT/summary.txt"; : > "$SUMMARY"
XV=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1280x800x24")
fail() { echo "pool_spawner: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
HOST_PATH="$PATH"

# summarize <label> <log>: one table row from the sample lines (the first window is warm-up and skipped)
summarize() {
  local label="$1" log="$2"
  local row
  row="$(awk -v L="$label" '
    /rensga: prof sample name=Spawn/ { for (i=1;i<=NF;i++) { split($i,a,"="); if (a[1]=="avg_us") s[++ns]=a[2]; if (a[1]=="max_us") sm[ns]=a[2]; if (a[1]=="calls") sc=a[2] } }
    /rensga: prof sample name=Move/  { for (i=1;i<=NF;i++) { split($i,a,"="); if (a[1]=="avg_us") m[++nm]=a[2]; if (a[1]=="max_us") mm[nm]=a[2] } }
    /rensga: prof frames=/           { for (i=1;i<=NF;i++) { split($i,a,"="); if (a[1]=="total_us") { split(a[2],t,"/"); tt[++nt]=t[1] } if (a[1]=="update_us") { split(a[2],u,"/"); uu[++nu]=u[1] } } }
    function mean(x,n,   i,sum,c) { sum=0; c=0; for (i=2;i<=n;i++) { sum+=x[i]; c++ } return c? sum/c : (n? x[1] : 0) }
    function peak(x,n,   i,p) { p=0; for (i=2;i<=n;i++) if (x[i]+0>p) p=x[i]+0; return p }
    END { printf "%-10s Spawn avg %7.0f us (worst frame %6d)   Move avg %7.0f us (worst %6d)   Update avg %7.0f us   frame total avg %7.0f us   windows=%d\n", L, mean(s,ns), peak(sm,ns), mean(m,nm), peak(mm,nm), mean(uu,nu), mean(tt,nt), ns }' "$log")"
  echo "$row" | tee -a "$SUMMARY"
}

check_run() { # <log> <mode> <label>
  need "rensga: pool: mode=$2 frames=3[0-9][0-9] spawned=[0-9]+ refused=0 live_max=" "$1" "$3: the run did not finish or refused spawns"
  need "rensga: prof sample name=Spawn frames=60 calls=[0-9]+" "$1" "$3: no Spawn profiler samples"
  need "rensga: prof sample name=Move frames=60 calls=[0-9]+" "$1" "$3: no Move profiler samples"
  grep -qE "alloc-check frames=[0-9]+ allocs=0" "$1" || fail "$3: alloc-check is not allocs=0 (see $1)"
  grep -E "rensga: pool:|alloc-check" "$1" | sed "s/^/  $3: /"
}

host_tools() {
  env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker rensga_desktop 2>&1 | tail -1
}
cook() { ( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target "$1" --force "${@:2}" ) 9>"/tmp/rensga-cook-pool_spawner.lock" | tail -2; }

if echo " $MODES " | grep -q " headless "; then
  step "headless: PPSSPPHeadless, Plain vs Pooled"
  command -v PPSSPPHeadless >/dev/null || { echo "skip: PPSSPPHeadless not installed"; exit 77; }
  host_tools
  GENDIR="$PROJ/Library/Generated/psp"
  cook psp --gen-header "$GENDIR/console_target.gen.h"
  PACK="$PROJ/Library/Cooked/psp/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
  reflect_prescan "$B/reflect" "$PROJ"
  . "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
  GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
  for v in Plain Pooled; do
    auto=""; [ "$v" = Pooled ] && auto="Pooled"
    rensga_tc cmake --preset psp "${GEN[@]}" -B "$B/psp-$v" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
      -DRENSGA_PSP_AUTOSTART_SCENE="$auto" -DRENSGA_EXIT_AFTER_FRAMES=600 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
      -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF >/dev/null
    rensga_tc cmake --build "$B/psp-$v" --target rensga_game -j"$JOBS" 2>&1 | tail -1
    R="$OUT/psp_$v"; HH="$OUT/psp_home_$v"; rm -rf "$R" "$HH"; mkdir -p "$R" "$HH"
    cp "$B/psp-$v/engine/runtime/rensga_game.elf" "$PACK" "$R/"
    ( cd "$R" && HOME="$HH" timeout -s KILL 240 "${XV[@]}" PPSSPPHeadless "$R/rensga_game.elf" -r "$R" -l --graphics=software --timeout=200 > "$OUT/psp_$v.emu" 2>&1 || true )
    cp "$R/rensga.log" "$OUT/psp_$v.log" 2>/dev/null || : > "$OUT/psp_$v.log"
  done
  check_run "$OUT/psp_Plain.log" plain "psp plain"
  check_run "$OUT/psp_Pooled.log" pooled "psp pooled"
  need "rensga: pool: init made=96 of 96" "$OUT/psp_Pooled.log" "psp pooled: the pool did not make 96 clones"
  echo "PSP (PPSSPPHeadless, emulator time on this PC):" | tee -a "$SUMMARY"
  summarize "plain" "$OUT/psp_Plain.log"
  summarize "pooled" "$OUT/psp_Pooled.log"
  echo "pool_spawner headless: OK"
fi

if echo " $MODES " | grep -q " desktop "; then
  step "desktop: Plain vs Pooled on the Linux player"
  host_tools
  cook linux
  PACK="$PROJ/Library/Cooked/linux/game.rpak"; [ -s "$PACK" ] || fail "no $PACK"
  for v in Plain Pooled; do
    env XDG_DATA_HOME="$(mktemp -d)" GALLIUM_DRIVER=softpipe "${XV[@]}" "$HB/engine/runtime/rensga_desktop" --pack "$PACK" --scene "$v" --frames 600 > "$OUT/desktop_$v.log" 2>&1 || true
  done
  check_run "$OUT/desktop_Plain.log" plain "desktop plain"
  check_run "$OUT/desktop_Pooled.log" pooled "desktop pooled"
  echo "Desktop (native time, softpipe renderer):" | tee -a "$SUMMARY"
  summarize "plain" "$OUT/desktop_Plain.log"
  summarize "pooled" "$OUT/desktop_Pooled.log"
  echo "pool_spawner desktop: OK"
fi
echo "pool_spawner: all requested modes passed"
