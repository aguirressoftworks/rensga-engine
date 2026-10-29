#!/usr/bin/env bash
# One-time: installs the pinned Android SDK components into $ANDROID_HOME. Run inside tools/docker/android.sh.
set -eu
SM=/opt/cmdline-tools/cmdline-tools/bin/sdkmanager
if [ -d "$ANDROID_HOME/ndk/26.1.10909125" ] && [ -d "$ANDROID_HOME/platforms/android-34" ] && [ -d "$ANDROID_HOME/cmake/3.22.1" ] && [ -d "$ANDROID_HOME/build-tools/34.0.0" ]; then exit 0; fi
yes | "$SM" --sdk_root="$ANDROID_HOME" --licenses >/dev/null || true
"$SM" --sdk_root="$ANDROID_HOME" "platforms;android-34" "build-tools;34.0.0" "ndk;26.1.10909125" "cmake;3.22.1"
