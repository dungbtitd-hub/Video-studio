#!/usr/bin/env bash
# Pushes the Android test binaries to a running emulator/device and runs them.
#   scripts/run_android_tests.sh <build-dir>
set -euo pipefail
B=${1:?build dir}
D=/data/local/tmp/vstudio
adb shell "rm -rf $D && mkdir -p $D/tmp"
adb push "$B/core/tests/vstudio_core_tests" "$B/adapters/android/vstudio_android_tests" $D/ > /dev/null
adb shell "cd $D && chmod +x vstudio_core_tests vstudio_android_tests"
rc=0
timeout 600 adb shell "cd $D && TMPDIR=$D/tmp ./vstudio_core_tests; echo exit=\$?" | tee core.txt || true
# Google's software codecs: the emulator's default H.264 decoder is a host-side shim.
timeout 900 adb shell "cd $D && TMPDIR=$D/tmp VSTUDIO_ANDROID_SOFTWARE_CODECS=1 ./vstudio_android_tests; echo exit=\$?" | tee android.txt || true
grep -q "exit=0" core.txt || rc=1
grep -q "exit=0" android.txt || rc=1
exit $rc
