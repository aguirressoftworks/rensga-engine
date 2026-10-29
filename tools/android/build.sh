#!/usr/bin/env bash
# Builds the Engine 0 APK. Run inside tools/docker/android.sh:   tools/docker/android.sh tools/android/build.sh
# Output: tools/android/app/build/outputs/apk/debug/app-debug.apk
set -eu
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$HERE/provision_sdk.sh"
printf 'sdk.dir=%s\ncmake.dir=/opt/cmake-3.28.6\n' "$ANDROID_HOME" > "$HERE/local.properties"
cd "$HERE" && gradle --no-daemon -q assembleDebug
ls -la app/build/outputs/apk/debug/app-debug.apk
