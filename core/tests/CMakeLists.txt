add_executable(vstudio_core_tests main.cpp test_core.cpp test_grade.cpp test_subtitles.cpp)
target_link_libraries(vstudio_core_tests PRIVATE vstudio::core)
add_test(NAME core_tests COMMAND vstudio_core_tests)
