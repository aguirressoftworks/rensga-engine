#!/usr/bin/env bash
# Runs a command inside the pinned windows toolchain image (see tools/docker/_run.sh). Usage: tools/docker/windows.sh <command...>
NAME=windows
RENSGA_WRAPPER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/windows.sh"
RENSGA_DOCKER_ENV=()
. "$(dirname "${BASH_SOURCE[0]}")/_run.sh"
