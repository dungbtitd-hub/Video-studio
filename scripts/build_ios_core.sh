#!/usr/bin/env bash
# Builds the core + C API as static libraries for iOS (device arm64 and
# simulator arm64) and bundles them into VStudioCore.xcframework.
# Usage: scripts/build_ios_core.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build-ios}"
COMMON=(-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
        -DCMAKE_BUILD_TYPE=Release -DVSTUDIO_BUILD_TESTS=OFF -DVSTUDIO_BUILD_BENCH=OFF -DVSTUDIO_BUILD_CLI=OFF
        -DVSTUDIO_WITH_FFMPEG=OFF -DVSTUDIO_BUILD_CAPI_SHARED=OFF)

for SDK in iphoneos iphonesimulator; do
  cmake -S "$ROOT" -B "$OUT/$SDK" "${COMMON[@]}" -DCMAKE_OSX_SYSROOT=$SDK
  cmake --build "$OUT/$SDK" --target vstudio_capi vstudio_core
  libtool -static -o "$OUT/$SDK/libVStudioCore.a" "$OUT/$SDK/capi/libvstudio.a" "$OUT/$SDK/core/libvstudio_core.a"
done

rm -rf "$OUT/VStudioCore.xcframework"
xcodebuild -create-xcframework \
  -library "$OUT/iphoneos/libVStudioCore.a" -headers "$ROOT/capi/include" \
  -library "$OUT/iphonesimulator/libVStudioCore.a" -headers "$ROOT/capi/include" \
  -output "$OUT/VStudioCore.xcframework"
echo "Built $OUT/VStudioCore.xcframework"
