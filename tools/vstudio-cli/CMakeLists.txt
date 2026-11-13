add_executable(vstudio-cli main.cpp)
target_link_libraries(vstudio-cli PRIVATE vstudio::core vstudio::ffmpeg)
if(TARGET vstudio_apple)
  target_link_libraries(vstudio-cli PRIVATE vstudio::apple)  # Metal compositor for export
endif()
