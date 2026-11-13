# Video Studio Core: no UI, no OS media APIs, no network (plan 3.4).
add_library(vstudio_core STATIC
  src/time.cpp
  src/model.cpp
  src/timeline.cpp
  src/history.cpp
  src/editor.cpp
  src/project_io.cpp
  src/frame_cache.cpp
  src/render.cpp
  src/scheduler.cpp
  src/preview.cpp
  src/modules.cpp
  src/proxy.cpp
  src/media_library.cpp
  src/export_presets.cpp
  src/looks.cpp
  src/transfer.cpp
)
add_library(vstudio::core ALIAS vstudio_core)
target_include_directories(vstudio_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(vstudio_core PUBLIC cxx_std_20)

if(VSTUDIO_BUILD_TESTS)
  add_subdirectory(tests)
endif()
