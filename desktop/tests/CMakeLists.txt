# Widget interaction tests, run headless (offscreen platform).
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test)
qt_add_executable(timeline_widget_tests
  test_timeline_widget.cpp
  ../widgets/TimelineWidget.cpp ../widgets/TimelineWidget.h ../widgets/Theme.h
)
target_include_directories(timeline_widget_tests PRIVATE ../widgets)
target_link_libraries(timeline_widget_tests PRIVATE vstudio_desktop_common Qt6::Widgets Qt6::Test)
add_test(NAME timeline_widget_tests COMMAND timeline_widget_tests)
set_tests_properties(timeline_widget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(main_window_tests
  test_main_window.cpp
  ../widgets/MainWindow.cpp ../widgets/MainWindow.h
  ../widgets/PreviewWidget.cpp ../widgets/PreviewWidget.h
  ../widgets/TimelineWidget.cpp ../widgets/TimelineWidget.h ../widgets/Theme.h
)
target_include_directories(main_window_tests PRIVATE ../widgets)
target_link_libraries(main_window_tests PRIVATE vstudio_desktop_common Qt6::Widgets Qt6::Test)
add_test(NAME main_window_tests COMMAND main_window_tests)
set_tests_properties(main_window_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
