#!/usr/bin/env bash
# Source this from a CI or dev script to get a console/cross toolchain without requiring Docker:
#
#   . tools/modules/toolchain_env.sh <module-id> [<docker-wrapper-name>]
#   rensga_tc cmake --preset psp -B build/psp          # runs on the chosen backend
#
# RENSGA_TOOLCHAIN=auto (default) | native | docker, the same switch the pipeline uses.
#   native  the build support module is installed: its environment is exported into this shell and
#           rensga_tc simply runs the command. CI installs the same modules users install.
#   docker  contributor fallback: rensga_tc runs the command through tools/docker/<wrapper>.sh.
#   none    prints how to install the module and returns 1.
# Afterwards RENSGA_TOOLCHAIN_BACKEND is "native" or "docker".
#
# Run directly (not sourced) it prints the environment as shell code:  eval "$(tools/modules/toolchain_env.sh psp-build)"

_rtc_here="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_rtc_repo="$(cd "$_rtc_here/../.." && pwd)"

_rtc_cli() {   # prints the module CLI as a command line
  if [ -n "${RENSGA_MODULES_CLI:-}" ]; then echo "$RENSGA_MODULES_CLI"; return 0; fi
  if command -v rensga-modules >/dev/null 2>&1; then echo "rensga-modules"; return 0; fi
  for b in "$_rtc_repo"/build/*/tools/modules/rensga-modules; do [ -x "$b" ] && { echo "$b"; return 0; }; done
  for b in "$_rtc_repo"/build/*/tools/cli/rensga; do [ -x "$b" ] && { echo "$b modules"; return 0; }; done
  return 1
}

rensga_tc_setup() {
  local id="${1:?module id}" wrapper="${2:-}"
  local mode="${RENSGA_TOOLCHAIN:-auto}" cli env_code
  RENSGA_TOOLCHAIN_BACKEND=""
  if [ "$mode" != docker ]; then
    if cli="$(_rtc_cli)"; then
      if env_code="$($cli env "$id" --shell sh 2>/dev/null)"; then
        eval "$env_code"
        RENSGA_TOOLCHAIN_BACKEND=native
        export RENSGA_TOOLCHAIN_BACKEND
        return 0
      fi
    elif [ "$mode" = native ]; then
      echo "toolchain_env: no rensga module CLI found. Build it: cmake --build build/host --target rensga-modules" >&2
      return 1
    fi
  fi
  if [ "$mode" != native ] && [ -n "$wrapper" ] && [ -x "$_rtc_repo/tools/docker/$wrapper.sh" ] \
     && "$_rtc_repo/tools/docker/$wrapper.sh" true >/dev/null 2>&1; then
    RENSGA_TOOLCHAIN_BACKEND=docker
    RENSGA_DOCKER_WRAPPER="$_rtc_repo/tools/docker/$wrapper.sh"
    export RENSGA_TOOLCHAIN_BACKEND RENSGA_DOCKER_WRAPPER
    return 0
  fi
  echo "toolchain_env: the toolchain module '$id' is not installed. Install it from Hub > Modules or run: rensga modules install $id" >&2
  return 1
}

rensga_tc() {
  if [ "${RENSGA_TOOLCHAIN_BACKEND:-}" = docker ]; then "$RENSGA_DOCKER_WRAPPER" "$@"; else "$@"; fi
}

if [ "${BASH_SOURCE[0]:-$0}" = "$0" ]; then
  # executed, not sourced: print the environment
  id="${1:?usage: toolchain_env.sh <module-id>}"
  cli="$(_rtc_cli)" || { echo "toolchain_env: no rensga module CLI found" >&2; exit 1; }
  exec $cli env "$id" --shell sh
elif [ "$#" -gt 0 ]; then
  rensga_tc_setup "$@"
fi
