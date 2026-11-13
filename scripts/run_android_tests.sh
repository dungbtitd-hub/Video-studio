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
# One process per test with its own timeout, so a hang names the test and the rest still run.
: > android.txt
for t in "registers" "android: probe" "frame-accurate" "audio reads" "P0 flow" "proxy"; do
  echo "--- $t" | tee -a android.txt
  timeout 240 adb shell "cd $D && TMPDIR=$D/tmp VSTUDIO_ANDROID_SOFTWARE_CODECS=1 ./vstudio_android_tests '$t'; echo exit=\$?" \
    | tee -a android.txt || echo "TIMEOUT: $t" | tee -a android.txt
done
adb logcat -d -s vstudio:* | tail -n 80
grep -q "exit=0" core.txt || rc=1
test "$(grep -c "exit=0" android.txt)" -eq 6 || rc=1
exit $rc
