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
  src/grade.cpp
  src/scopes.cpp
  src/stills.cpp
  src/transfer.cpp
    src/pixel.cpp
    src/color_space.cpp
    src/keyframes.cpp
    src/cover.cpp
    src/tracker.cpp
  src/transitions.cpp
  src/vector.cpp
  src/motion.cpp
  src/generators.cpp
  src/fonts.cpp
  src/text.cpp
  src/stickers.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/embedded_fonts.cpp
)
add_library(vstudio::core ALIAS vstudio_core)

# Bundled OFL fonts (core/assets/fonts), compiled in as byte arrays.
file(GLOB VSTUDIO_FONT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf)
list(SORT VSTUDIO_FONT_FILES)
# '|' separated: a ';' list would be split into several arguments of the command.
string(REPLACE ";" "|" VSTUDIO_FONT_LIST "${VSTUDIO_FONT_FILES}")
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_fonts.cpp
  COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/embedded_fonts.cpp "-DFILES=${VSTUDIO_FONT_LIST}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${VSTUDIO_FONT_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Embedding fonts"
  VERBATIM)
target_include_directories(vstudio_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_include_directories(vstudio_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
target_compile_features(vstudio_core PUBLIC cxx_std_20)

if(VSTUDIO_BUILD_TESTS)
  add_subdirectory(tests)
endif()
