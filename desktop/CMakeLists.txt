# Qt 6 desktop shell (Windows, macOS; Linux for development/CI).
# P0 builds the Qt Quick (QML) variant; the plan also asks for a Widgets
# prototype before choosing - see docs/p0-status.md.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Multimedia)
qt_standard_project_setup()

qt_add_executable(VideoStudio
  src/main.cpp
  src/EditorController.cpp src/EditorController.h
  src/PreviewItem.cpp src/PreviewItem.h
  src/AudioOutput.cpp src/AudioOutput.h
)
# Must be set before qt_add_qml_module.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(VideoStudio
  URI VideoStudio
  VERSION 1.0
  QML_FILES qml/Main.qml qml/Theme.qml qml/TimelineView.qml
)
target_include_directories(VideoStudio PRIVATE src)
target_link_libraries(VideoStudio PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia vstudio::core)
if(VSTUDIO_WITH_FFMPEG)
  target_link_libraries(VideoStudio PRIVATE vstudio::ffmpeg)
endif()
# The QML module folder is also called VideoStudio, so the binary goes to bin/.
set_target_properties(VideoStudio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
