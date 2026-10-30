#!/usr/bin/env bash
# Checks (and, in the Docker image only, installs) the pinned Android SDK components in $ANDROID_HOME.
# Native: the android-build module installs them once, after the user accepts Google's Android SDK license
# (Hub > Modules, or: rensga modules install android-build). This script then only verifies them.
# Docker (contributors): the first run downloads them into the mounted build/android-sdk; set RENSGA_ACCEPT_ANDROID_SDK_LICENSE=1
# to accept Google's license non-interactively.
set -eu
if [ -d "$ANDROID_HOME/ndk/26.1.10909125" ] && [ -d "$ANDROID_HOME/platforms/android-34" ] && [ -d "$ANDROID_HOME/cmake/3.22.1" ] && [ -d "$ANDROID_HOME/build-tools/34.0.0" ]; then exit 0; fi
SM="$(command -v sdkmanager || true)"; [ -n "$SM" ] || SM=/opt/cmdline-tools/cmdline-tools/bin/sdkmanager
if [ "${RENSGA_ACCEPT_ANDROID_SDK_LICENSE:-}" != 1 ]; then
  echo "android: the Android SDK components are missing in $ANDROID_HOME. Install the module (it asks for Google's SDK license): rensga modules install android-build" >&2
  exit 1
fi
yes | "$SM" --sdk_root="$ANDROID_HOME" --licenses >/dev/null || true
"$SM" --sdk_root="$ANDROID_HOME" "platforms;android-34" "build-tools;34.0.0" "ndk;26.1.10909125" "cmake;3.22.1"
