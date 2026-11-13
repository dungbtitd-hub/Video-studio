#!/usr/bin/env bash
# Installs the Flutter APK on a running emulator/device, starts it and checks
# that the app loaded the native core (libvstudio.so) and is still running.
#   scripts/smoke_android_app.sh <apk>
set -euo pipefail
APK=${1:?apk}
PKG=com.videostudio.video_studio
adb install -r "$APK" > /dev/null
adb logcat -c
adb shell am start -W -n $PKG/.MainActivity
ok=0
for _ in $(seq 1 30); do
  if adb logcat -d -s flutter:* | grep -q "vstudio: core .* ready"; then ok=1; break; fi
  sleep 2
done
sleep 3
adb exec-out screencap -p > android-app.png || true
adb logcat -d -s flutter:* AndroidRuntime:E DEBUG:* | tail -n 40
alive=$(adb shell pidof $PKG | tr -d '\r' || true)
echo "core ready: $ok, process: ${alive:-none}"
[ "$ok" = 1 ] && [ -n "$alive" ]
