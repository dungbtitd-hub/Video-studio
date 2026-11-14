find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

qt_add_executable(VideoStudio
  main.cpp
  Theme.h
  MainWindow.cpp MainWindow.h
  PreviewWidget.cpp PreviewWidget.h
  TimelineWidget.cpp TimelineWidget.h
  LooksPanel.cpp LooksPanel.h
  CoverPanel.cpp CoverPanel.h
)
target_link_libraries(VideoStudio PRIVATE vstudio_desktop_common Qt6::Widgets)
set_target_properties(VideoStudio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
