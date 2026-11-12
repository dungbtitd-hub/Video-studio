find_package(Qt6 6.4 REQUIRED COMPONENTS Quick QuickControls2)

qt_add_executable(VideoStudio
  main.cpp
  PreviewItem.cpp PreviewItem.h
)
# Must be set before qt_add_qml_module.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(VideoStudio
  URI VideoStudio
  VERSION 1.0
  QML_FILES qml/Main.qml qml/Theme.qml qml/TimelineView.qml
)
target_include_directories(VideoStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(VideoStudio PRIVATE vstudio_desktop_common Qt6::Quick Qt6::QuickControls2)
# The QML module folder is also called VideoStudio, so the binary goes to bin/.
set_target_properties(VideoStudio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
