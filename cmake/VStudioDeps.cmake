# Third-party dependencies of the core. nlohmann_json is header-only (MIT).
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  set(_vs_ts)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(_vs_ts DOWNLOAD_EXTRACT_TIMESTAMP TRUE)  # unknown to the Android SDK's CMake 3.22
  endif()
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    ${_vs_ts})
  FetchContent_MakeAvailable(nlohmann_json)
endif()
find_package(Threads REQUIRED)
