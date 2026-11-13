add_executable(vstudio-cli main.cpp)
target_link_libraries(vstudio-cli PRIVATE vstudio::core vstudio::ffmpeg)
if(TARGET vstudio_windows)
  target_link_libraries(vstudio-cli PRIVATE vstudio::windows)  # D3D11 compositor for export
endif()
if(TARGET vstudio_libraw)
  target_link_libraries(vstudio-cli PRIVATE vstudio::libraw)
endif()
if(TARGET vstudio_apple)
  target_link_libraries(vstudio-cli PRIVATE vstudio::apple)  # Metal compositor for export
endif()
