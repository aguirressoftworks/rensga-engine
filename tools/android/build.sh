#!/usr/bin/env bash
# Builds the Engine 0 APK with the android-build module's environment (JAVA_HOME, ANDROID_HOME, SDL2_ROOT, gradle on PATH).
# Native:  . tools/modules/toolchain_env.sh android-build android && tools/android/build.sh   (tools/ci/android.sh does this)
# Docker (contributors):  tools/docker/android.sh tools/android/build.sh
# Output: tools/android/app/build/outputs/apk/debug/app-debug.apk
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$HERE/provision_sdk.sh"
printf 'sdk.dir=%s\ncmake.dir=%s\n' "$ANDROID_HOME" "${RENSGA_ANDROID_CMAKE_DIR:-/opt/cmake-3.28.6}" > "$HERE/local.properties"
# Reflection codegen needs Python, which the image lacks: the host runs it (tools/ci/android.sh) before this script.
cd "$HERE" && gradle --no-daemon -q assembleDebug
ls -la app/build/outputs/apk/debug/app-debug.apk
