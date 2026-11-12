cmake_minimum_required(VERSION 3.21)
project(VideoStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(VSTUDIO_BUILD_TESTS "Build core unit tests" ON)
option(VSTUDIO_BUILD_BENCH "Build benchmarks" ON)
option(VSTUDIO_WITH_FFMPEG "Build the FFmpeg media module (desktop)" ON)
option(VSTUDIO_BUILD_CLI "Build the vstudio-cli tool" ON)
option(VSTUDIO_BUILD_DESKTOP "Build the Qt 6 desktop app" OFF)
option(VSTUDIO_BUILD_CAPI_SHARED "Build libvstudio_capi as a shared library (Flutter FFI)" ON)

include(VStudioDeps)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

if(VSTUDIO_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(core)

if(VSTUDIO_WITH_FFMPEG)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)
  endif()
  if(FFMPEG_FOUND)
    add_subdirectory(adapters/ffmpeg)
  else()
    message(WARNING "FFmpeg development files not found: building without the FFmpeg module")
    set(VSTUDIO_WITH_FFMPEG OFF)
  endif()
endif()

add_subdirectory(capi)

if(VSTUDIO_BUILD_CLI AND VSTUDIO_WITH_FFMPEG)
  add_subdirectory(tools/vstudio-cli)
endif()

if(VSTUDIO_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(VSTUDIO_BUILD_DESKTOP)
  add_subdirectory(desktop)
endif()
