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

## First-time setup

The platform folders are generated, not committed:

```bash
cd mobile
flutter create --platforms=android,ios --org com.videostudio .
flutter pub get
```

Then:

* **Android** – add to `android/app/build.gradle` inside `android { }`:
  `externalNativeBuild { cmake { path "../../native/CMakeLists.txt" } }`
* **iOS** – build the core as a static framework with `scripts/build_ios_core.sh` and add it to the Runner
  target; `DynamicLibrary.process()` finds the symbols.

## Status (P0)

* Done: FFI bindings, editor screen, timeline gestures, preview polling, export trigger, autosave.
* Pending in P0: MediaCodec module (Android) and the Apple AVFoundation/VideoToolbox module shared with
  macOS. Until they land, importing media on a phone reports "no media module available".
* Preview frames are copied once per frame through FFI. The plan's target is a native texture with no copy;
  that replaces `takeFrame()` once the platform modules exist.
