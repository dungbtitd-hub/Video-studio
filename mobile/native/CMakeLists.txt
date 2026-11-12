# Builds libvstudio (core + C API) for Android through Gradle's externalNativeBuild:
#   android/app/build.gradle:
#     android { externalNativeBuild { cmake { path "../../native/CMakeLists.txt" } } }
# Media on Android goes through the MediaCodec module (adapters/android).
cmake_minimum_required(VERSION 3.21)
project(vstudio_mobile LANGUAGES C CXX)
set(VSTUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VSTUDIO_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(VSTUDIO_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(VSTUDIO_WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(VSTUDIO_BUILD_CAPI_SHARED ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_BINARY_DIR}/vstudio)
