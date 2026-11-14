# Apple modules, shared by macOS and iOS: media (AVFoundation / VideoToolbox),
# the Metal compositor and camera RAW (Core Image).
enable_language(OBJCXX)
add_library(vstudio_apple STATIC src/avfoundation_module.mm src/metal_compositor.mm src/apple_raw.mm)
add_library(vstudio::apple ALIAS vstudio_apple)
target_include_directories(vstudio_apple PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_options(vstudio_apple PRIVATE -fobjc-arc)
target_link_libraries(vstudio_apple PUBLIC vstudio::core
  "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation" "-framework Metal"
  "-framework CoreImage" "-framework CoreGraphics" "-framework ImageIO")
target_compile_definitions(vstudio_apple PUBLIC VSTUDIO_HAS_APPLE_MEDIA=1 VSTUDIO_HAS_METAL=1)

if(VSTUDIO_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(vstudio_apple_tests tests/test_avfoundation.cpp tests/test_metal_compositor.cpp tests/test_apple_raw.cpp
    ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_apple_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(vstudio_apple_tests PRIVATE vstudio::apple)
  add_test(NAME apple_tests COMMAND vstudio_apple_tests)
endif()
