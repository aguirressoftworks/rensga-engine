#!/usr/bin/env bash
# Runs every CI job locally, in the same order and with the same scripts as .github/workflows/ci.yml.
# Usage: tools/ci/run_local.sh [job...]   (default: linux_host psp editor_tour determinism psp_gpu_check windows windows_run android android_device ps2 ps2_check)
# linux_host also runs the F2 host checks (touch, hot reload, cook cache, audio-stress); VRAM and PS2 audio stress stay PS2-local (ps2_check).
# windows_run (wine), android_device (phone via adb, "built, not run" without one) and ps2_check (PCSX2 + your own BIOS, LOCAL ONLY)
# exit 77 (SKIP) when their prerequisites are missing.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
jobs=("$@"); [ ${#jobs[@]} -eq 0 ] && jobs=(linux_host psp editor_tour determinism psp_gpu_check windows windows_run android android_device ps2 ps2_check)
fail=0; summary=()
for j in "${jobs[@]}"; do
  echo "######## $j"
  if "$HERE/$j.sh"; then summary+=("PASS $j"); else rc=$?; if [ "$rc" = 77 ]; then summary+=("SKIP $j"); else summary+=("FAIL $j"); fail=1; fi; fi
done
printf '%s\n' "${summary[@]}"; exit $fail
