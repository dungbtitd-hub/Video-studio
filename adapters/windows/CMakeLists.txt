# Windows modules: the Direct3D 11 compositor.
add_library(vstudio_windows STATIC src/d3d11_compositor.cpp)
add_library(vstudio::windows ALIAS vstudio_windows)
target_include_directories(vstudio_windows PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
# d3dcompiler_47.dll is loaded at run time (LoadLibrary), only d3d11 is linked.
target_link_libraries(vstudio_windows PUBLIC vstudio::core d3d11)
target_compile_definitions(vstudio_windows PUBLIC VSTUDIO_HAS_D3D11=1)

if(VSTUDIO_BUILD_TESTS)
  add_executable(vstudio_windows_tests tests/test_d3d11_compositor.cpp ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_windows_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(vstudio_windows_tests PRIVATE vstudio::windows)
  add_test(NAME windows_tests COMMAND vstudio_windows_tests)
endif()
