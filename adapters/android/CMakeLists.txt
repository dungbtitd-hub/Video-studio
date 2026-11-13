# Android modules (API 26+): media (NDK MediaCodec / MediaExtractor / MediaMuxer)
# and the OpenGL ES 3 compositor.
add_library(vstudio_android STATIC src/mediacodec_module.cpp src/gles_compositor.cpp)
add_library(vstudio::android ALIAS vstudio_android)
target_include_directories(vstudio_android PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_android PUBLIC vstudio::core PRIVATE mediandk log EGL GLESv3)
target_compile_definitions(vstudio_android PUBLIC VSTUDIO_HAS_ANDROID_MEDIA=1)
# minSdk is 26: newer NDK calls (AMediaCodec_getName, API 28) are weak and
# guarded with __builtin_available; the warning makes an unguarded call an error.
target_compile_definitions(vstudio_android PRIVATE __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
target_compile_options(vstudio_android PRIVATE -Werror=unguarded-availability)

if(VSTUDIO_BUILD_TESTS)
  # Needs a device or emulator: CI pushes it with adb and runs it from /data/local/tmp.
  add_executable(vstudio_android_tests tests/test_mediacodec.cpp tests/test_gles_compositor.cpp
    ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_android_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(vstudio_android_tests PRIVATE vstudio::android dl)
endif()
