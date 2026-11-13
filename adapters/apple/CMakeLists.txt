# Apple media module (AVFoundation / VideoToolbox), shared by macOS and iOS.
enable_language(OBJCXX)
add_library(vstudio_apple STATIC src/avfoundation_module.mm)
add_library(vstudio::apple ALIAS vstudio_apple)
target_include_directories(vstudio_apple PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_options(vstudio_apple PRIVATE -fobjc-arc)
target_link_libraries(vstudio_apple PUBLIC vstudio::core
  "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
target_compile_definitions(vstudio_apple PUBLIC VSTUDIO_HAS_APPLE_MEDIA=1)

if(VSTUDIO_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(vstudio_apple_tests tests/test_avfoundation.cpp ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_apple_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(vstudio_apple_tests PRIVATE vstudio::apple)
  add_test(NAME apple_tests COMMAND vstudio_apple_tests)
endif()
