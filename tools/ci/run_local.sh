#!/usr/bin/env bash
# Runs every CI job locally, in the same order and with the same scripts as .github/workflows/ci.yml.
# Usage: tools/ci/run_local.sh [job...]   (default: linux_host psp psp_gpu_check windows android ps2)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
jobs=("$@"); [ ${#jobs[@]} -eq 0 ] && jobs=(linux_host psp psp_gpu_check windows android ps2)
fail=0; summary=()
for j in "${jobs[@]}"; do
  echo "######## $j"
  if "$HERE/$j.sh"; then summary+=("PASS $j"); else rc=$?; if [ "$rc" = 77 ]; then summary+=("SKIP $j"); else summary+=("FAIL $j"); fail=1; fi; fi
done
printf '%s\n' "${summary[@]}"; exit $fail
