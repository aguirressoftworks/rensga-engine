#!/usr/bin/env bash
# Runs a command inside the pinned android toolchain image (see tools/docker/_run.sh). Usage: tools/docker/android.sh <command...>
NAME=android
RENSGA_WRAPPER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/android.sh"
GC="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/gradle-cache"; mkdir -p "$GC"
RENSGA_DOCKER_ENV=(-e GRADLE_USER_HOME=/tmp/gradle -v "$GC:/tmp/gradle")
. "$(dirname "${BASH_SOURCE[0]}")/_run.sh"
