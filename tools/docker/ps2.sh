#!/usr/bin/env bash
# Runs a command inside the pinned ps2 toolchain image (see tools/docker/_run.sh). Usage: tools/docker/ps2.sh <command...>
NAME=ps2
RENSGA_WRAPPER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/ps2.sh"
RENSGA_DOCKER_ENV=()
. "$(dirname "${BASH_SOURCE[0]}")/_run.sh"
