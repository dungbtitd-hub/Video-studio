# Example plugin (Plugin API beta): a shared library + plugin.json, copied together into
# <build>/plugins/example so tests and the desktop app can load it.
add_library(vstudio_example_plugin MODULE example/example_plugin.c)
target_include_directories(vstudio_example_plugin PRIVATE ${PROJECT_SOURCE_DIR}/capi/include)
set_target_properties(vstudio_example_plugin PROPERTIES
  C_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/example
  LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/plugins/example
  LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/plugins/example
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/plugins/example)
if(APPLE)
  set_target_properties(vstudio_example_plugin PROPERTIES SUFFIX ".dylib")
endif()
configure_file(example/plugin.json ${CMAKE_BINARY_DIR}/plugins/example/plugin.json COPYONLY)
