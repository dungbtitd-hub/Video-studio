#!/usr/bin/env bash
# Installs the Flutter APK on a running emulator/device, starts it and checks
# that the app loaded the native core (libvstudio.so), that the core drew a frame
# into the native preview texture, and that the app is still running.
#   scripts/smoke_android_app.sh <apk>
set -euo pipefail
APK=${1:?apk}
PKG=com.videostudio.video_studio
adb install -r "$APK" > /dev/null
adb logcat -c
# The app opens on its start screen; vstudio.openEditor opens a new empty project instead,
# so the native preview has something to draw.
adb shell am start -W -n $PKG/.MainActivity --ez vstudio.openEditor true
ok=0
tex=0
for _ in $(seq 1 30); do
  adb logcat -d -s flutter:* | grep -q "vstudio: core .* ready" && ok=1
  adb logcat -d -s vstudio:* | grep -q "native preview: first frame" && tex=1
  [ "$ok" = 1 ] && [ "$tex" = 1 ] && break
  sleep 2
done
sleep 3
adb exec-out screencap -p > android-app.png || true
adb logcat -d -s flutter:* vstudio:* AndroidRuntime:E DEBUG:* | tail -n 40
alive=$(adb shell pidof $PKG | tr -d '\r' || true)
echo "core ready: $ok, native preview frame: $tex, process: ${alive:-none}"
[ "$ok" = 1 ] && [ "$tex" = 1 ] && [ -n "$alive" ]
