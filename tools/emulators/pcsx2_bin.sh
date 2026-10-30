#!/usr/bin/env bash
# Prints the PCSX2 executable to use, or nothing (exit 1). Order: $RENSGA_PCSX2, the `pcsx2` module installed with
# `rensga modules install pcsx2` (official portable build, unmodified, separate process), then pcsx2-qt on PATH.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${RENSGA_PCSX2:-}" ] && [ -x "$RENSGA_PCSX2" ]; then echo "$RENSGA_PCSX2"; exit 0; fi
env_code="$("$here/../modules/toolchain_env.sh" pcsx2 2>/dev/null)" && {
  p="$(eval "$env_code"; echo "${RENSGA_PCSX2:-}")"
  [ -n "$p" ] && [ -x "$p" ] && { echo "$p"; exit 0; }
}
command -v pcsx2-qt && exit 0
exit 1
