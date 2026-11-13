# Video Studio – mobile (Flutter)

Flutter UI for Android and iOS. All editing logic lives in the shared C++ core and is reached through the
C API (`capi/include/vstudio/vstudio_c.h`) with `dart:ffi`.

## Layout

| Path | What |
|---|---|
| `lib/core/vstudio_bindings.dart` | raw `dart:ffi` bindings (mirror of `vstudio_c.h`) |
| `lib/core/vstudio.dart` | Dart facade: strings, errors, frame polling |
| `lib/core/project_model.dart` | read-only view of the project JSON for drawing |
| `lib/ui/timeline.dart` | touch timeline (CustomPainter; drag is visual until release) |
| `lib/main.dart` | editor screen: preview, transport, timeline, tools |
| `native/CMakeLists.txt` | builds `libvstudio.so` for Android via Gradle |

## Build and run

The Android and iOS projects are committed (`android/`, `ios/`).

* **Android**: `flutter run` or `flutter build apk`. Gradle builds the core, the C API and the MediaCodec
  module from `native/CMakeLists.txt` into `libvstudio.so` (minSdk 26, arm64-v8a + x86_64).
* **iOS**: first build the core once from the repository root with `scripts/build_ios_core.sh` (writes
  `build-ios/`), then `flutter run` / `flutter build ios`. `ios/Flutter/VStudioCore.xcconfig` force-loads the
  static core into the Runner app; `DynamicLibrary.process()` finds the symbols.

CI builds both apps. On Android it also installs the APK on the emulator and checks that the core loads
(`scripts/smoke_android_app.sh`).

## Status (P0)

* Done: FFI bindings, editor screen, timeline gestures, preview polling, export trigger, autosave.
* Done: MediaCodec module (Android) and the Apple AVFoundation module shared with macOS, linked into the apps.
* Preview frames are copied once per frame through FFI. The plan's target is a native texture with no copy;
  that replaces `takeFrame()` once the platform modules exist.
