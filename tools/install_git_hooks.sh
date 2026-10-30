#!/usr/bin/env bash
# Installs the repo's git hooks (tools/git-hooks/*) into .git/hooks as symlinks. Safe to run again.
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[ -d "$REPO/.git" ] || { echo "not a git checkout: $REPO" >&2; exit 1; }
mkdir -p "$REPO/.git/hooks"
for h in "$REPO"/tools/git-hooks/*; do
  chmod +x "$h"
  ln -sfn "../../tools/git-hooks/$(basename "$h")" "$REPO/.git/hooks/$(basename "$h")"
  echo "installed $(basename "$h")"
done
