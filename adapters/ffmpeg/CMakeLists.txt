add_library(vstudio_ffmpeg STATIC src/ffmpeg_module.cpp)
add_library(vstudio::ffmpeg ALIAS vstudio_ffmpeg)
target_include_directories(vstudio_ffmpeg PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_ffmpeg PUBLIC vstudio::core PRIVATE PkgConfig::FFMPEG)
target_compile_definitions(vstudio_ffmpeg PUBLIC VSTUDIO_HAS_FFMPEG=1)

if(VSTUDIO_BUILD_TESTS)
  add_executable(vstudio_ffmpeg_tests tests/test_ffmpeg.cpp ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_ffmpeg_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  # The test writes a 10-bit source with libav directly (FFV1 is always built in).
  target_link_libraries(vstudio_ffmpeg_tests PRIVATE vstudio::ffmpeg PkgConfig::FFMPEG)
  add_test(NAME ffmpeg_tests COMMAND vstudio_ffmpeg_tests)
endif()
