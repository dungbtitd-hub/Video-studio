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
for t in "registers" "android: probe" "frame-accurate" "audio reads" "P0 flow" "proxy" "gles" "media library"; do
  echo "--- $t" | tee -a android.txt
  timeout 240 adb shell "cd $D && TMPDIR=$D/tmp VSTUDIO_ANDROID_SOFTWARE_CODECS=1 ./vstudio_android_tests '$t'; echo exit=\$?" \
    | tee -a android.txt || echo "TIMEOUT: $t" | tee -a android.txt
done
# All module tests in one process, as an app uses the module (many extractors and
# codecs created and destroyed). If it is still running after 90 s, dump its
# native stacks so a hang shows where it waits.
echo "--- all in one process" | tee single.txt
adb root > /dev/null || true; sleep 3; adb wait-for-device  # debuggerd needs root
adb logcat -c
( sleep 90; pid=$(adb shell pidof vstudio_android_tests | tr -d '\r'); \
  if [ -n "$pid" ]; then echo "STILL RUNNING: stacks of $pid"; adb shell debuggerd -b "$pid"; fi ) > stacks.txt 2>&1 &
watch=$!
timeout 180 adb shell "cd $D && TMPDIR=$D/tmp VSTUDIO_ANDROID_SOFTWARE_CODECS=1 ./vstudio_android_tests; echo exit=\$?" \
  | tee -a single.txt || echo "TIMEOUT: one process" | tee -a single.txt
wait $watch || true
cat stacks.txt
adb logcat -d -v time | grep -E "vstudio|CCodec|Codec2|MediaCodec|MPEG4Writer|MediaMuxer|AndroidRuntime|DEBUG" | tail -n 200
grep -q "exit=0" core.txt || rc=1
test "$(grep -c "exit=0" android.txt)" -eq 8 || rc=1
grep -q "exit=0" single.txt || rc=1
exit $rc
