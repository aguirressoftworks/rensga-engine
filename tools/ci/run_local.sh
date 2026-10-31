#!/usr/bin/env bash
# Runs every CI job locally, in the same order and with the same scripts as .github/workflows/ci.yml.
# Usage: tools/ci/run_local.sh [--install] [--docker] [job...]
#   (default jobs: linux_host psp psp_eboot psp_hello psp_2d templates editor_tour determinism psp_gpu_check psp_3d psp_api psp_services api_profile windows windows_run android android_device ps2 ps2_check)
# Native first, like CI: RENSGA_TOOLCHAIN=native and the build support modules (the toolchains users install from the Hub) in a shared
# RENSGA_HOME (default build/rensga-home, so each download happens once). Install what a job needs with `--install` (runs
# `rensga modules install` for the jobs you asked for) or by hand: build/ci/linux-host/tools/modules/rensga-modules install <id>.
#   --docker  contributor fallback: RENSGA_TOOLCHAIN=docker (pinned images under tools/docker/)
#   --install install the modules of the selected jobs first (android-build needs --accept-licenses: set RENSGA_ACCEPT_LICENSES=1 once you read Google's SDK license)
# F4.01: windows, android and ps2 build and cook the 2D slice (samples/platformer2d); linux_host plays it (TitleBot) on the desktop player and
# windows_run plays it under wine; ps2_check boots its ISO in PCSX2 (game complete, card save, allocs=0); psp_2d runs it in PPSSPP.
# linux_host also runs the F2 host checks (touch, hot reload, cook cache, audio-stress); VRAM and PS2 audio stress stay PS2-local (ps2_check).
# windows_run (wine), android_device (phone via adb, "built, not run" without one) and ps2_check (PCSX2 + your own BIOS, LOCAL ONLY)
# exit 77 (SKIP) when their prerequisites are missing (android also when the android-build module is not installed: it needs Google's SDK
# license, which this script never accepts for you). The summary at the end prints PASS / SKIP / FAIL per job.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
export RENSGA_TOOLCHAIN="${RENSGA_TOOLCHAIN:-native}"
export RENSGA_HOME="${RENSGA_HOME:-$REPO/build/rensga-home}"
install=0
while [ $# -gt 0 ]; do
  case "$1" in
    --docker) export RENSGA_TOOLCHAIN=docker; shift ;;
    --install) install=1; shift ;;
    *) break ;;
  esac
done
jobs=("$@"); [ ${#jobs[@]} -eq 0 ] && jobs=(linux_host psp psp_eboot psp_hello psp_2d templates editor_tour determinism psp_gpu_check psp_3d psp_api psp_services api_profile windows windows_run android android_device ps2 ps2_check)
echo "toolchain: $RENSGA_TOOLCHAIN   modules: $RENSGA_HOME/modules"
if [ "$install" = 1 ] && [ "$RENSGA_TOOLCHAIN" != docker ]; then
  mods=""
  for j in "${jobs[@]}"; do
    case "$j" in
      psp|psp_eboot|psp_hello|psp_2d|templates|editor_tour|determinism|psp_gpu_check|psp_3d|psp_api|psp_services) mods="$mods psp-build" ;;
      windows|windows_run) mods="$mods windows-build" ;;
      android|android_device) mods="$mods android-build" ;;
      ps2) mods="$mods ps2-build" ;;
      ps2_check) mods="$mods ps2-build pcsx2" ;;
    esac
  done
  mods="$(echo $mods | tr ' ' '\n' | sort -u | tr '\n' ' ')"
  if [ -n "$mods" ]; then
    MCLI="$REPO/build/ci/linux-host/tools/modules/rensga-modules"
    [ -x "$MCLI" ] || "$HERE/linux_host.sh" || exit 1
    flags=""; [ "${RENSGA_ACCEPT_LICENSES:-0}" = 1 ] && flags="--accept-licenses"
    echo "######## install modules: $mods"
    "$MCLI" install $mods $flags || echo "module install failed; jobs that need it will report it" >&2
  fi
fi
fail=0; summary=()
for j in "${jobs[@]}"; do
  echo "######## $j"
  if "$HERE/$j.sh"; then summary+=("PASS $j"); else rc=$?; if [ "$rc" = 77 ]; then summary+=("SKIP $j"); else summary+=("FAIL $j"); fi; [ "$rc" = 77 ] || fail=1; fi
done
printf '%s\n' "${summary[@]}"; exit $fail
