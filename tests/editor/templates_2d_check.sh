#!/bin/sh
# The committed 2D templates must equal the output of tools/templates/make_2d_template.py (which calls the sample generator).
# usage: templates_2d_check.sh <repo root>. Exit 77 = skipped (no python3).
ROOT=$1
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
for p in psp ps2; do
  python3 -I "$ROOT/tools/templates/make_2d_template.py" --profile $p --check "$ROOT/templates/$p-2d" || exit 1
done
