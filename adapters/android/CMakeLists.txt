# Android media module (NDK MediaCodec / MediaExtractor / MediaMuxer, API 26+).
add_library(vstudio_android STATIC src/mediacodec_module.cpp)
add_library(vstudio::android ALIAS vstudio_android)
target_include_directories(vstudio_android PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_android PUBLIC vstudio::core PRIVATE mediandk log)
target_compile_definitions(vstudio_android PUBLIC VSTUDIO_HAS_ANDROID_MEDIA=1)
