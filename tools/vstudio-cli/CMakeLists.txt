add_executable(vstudio-cli main.cpp)
target_link_libraries(vstudio-cli PRIVATE vstudio::core vstudio::ffmpeg)
