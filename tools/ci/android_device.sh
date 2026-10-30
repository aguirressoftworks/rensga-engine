#!/usr/bin/env bash
# F2.G1 (Android): runs the built APK on an attached phone if there is one (tools/android/run_device_check.sh); otherwise
# prints "android: built, not run (no device)" and exits 0. Needs tools/ci/android.sh to have built the APK.
exec "$(dirname "${BASH_SOURCE[0]}")/../android/run_device_check.sh" "$@"
