# Camera RAW photos with LibRaw (LGPL 2.1 / CDDL). The system library is used when there
# is one (Linux packages, Homebrew); otherwise LibRaw is built from source as a static
# library (Windows, Android, iOS), with no optional dependencies.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND AND NOT VSTUDIO_LIBRAW_FROM_SOURCE)
  pkg_check_modules(LIBRAW IMPORTED_TARGET libraw_r)
endif()
if(LIBRAW_FOUND)
  add_library(vstudio_libraw_deps INTERFACE)
  target_link_libraries(vstudio_libraw_deps INTERFACE PkgConfig::LIBRAW)
else()
  include(FetchContent)
  FetchContent_Declare(libraw_src
    GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
    GIT_TAG 0.21.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(libraw_src)  # no CMakeLists.txt upstream: this only downloads it
  file(GLOB LIBRAW_SOURCES ${libraw_src_SOURCE_DIR}/src/*.cpp ${libraw_src_SOURCE_DIR}/src/*/*.cpp)
  list(FILTER LIBRAW_SOURCES EXCLUDE REGEX "_ph\\.cpp$")  # placeholders, not part of the library
  add_library(vstudio_libraw_deps STATIC ${LIBRAW_SOURCES})
  target_include_directories(vstudio_libraw_deps SYSTEM PUBLIC ${libraw_src_SOURCE_DIR})
  target_compile_definitions(vstudio_libraw_deps PUBLIC LIBRAW_NODLL PRIVATE _CRT_SECURE_NO_WARNINGS)
  set_target_properties(vstudio_libraw_deps PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
  if(MSVC)
    target_compile_options(vstudio_libraw_deps PRIVATE /W0)
  else()
    target_compile_options(vstudio_libraw_deps PRIVATE -w)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(vstudio_libraw_deps PUBLIC Threads::Threads)
endif()

add_library(vstudio_libraw STATIC src/libraw_module.cpp)
add_library(vstudio::libraw ALIAS vstudio_libraw)
target_include_directories(vstudio_libraw PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(vstudio_libraw PUBLIC vstudio::core PRIVATE vstudio_libraw_deps)
target_compile_definitions(vstudio_libraw PUBLIC VSTUDIO_HAS_LIBRAW=1)

if(VSTUDIO_BUILD_TESTS)
  add_executable(vstudio_libraw_tests tests/test_libraw.cpp ${PROJECT_SOURCE_DIR}/core/tests/main.cpp)
  target_include_directories(vstudio_libraw_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/tests)
  target_link_libraries(vstudio_libraw_tests PRIVATE vstudio::libraw)
  if(NOT ANDROID AND NOT IOS)
    add_test(NAME libraw_tests COMMAND vstudio_libraw_tests)
  endif()
endif()
