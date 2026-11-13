# Qt 6 desktop shells (Windows, macOS; Linux for development/CI).
# Two P0 front-ends share one controller (common/) so they can be compared
# on equal terms before choosing the desktop UI technology:
#   quick/   Qt Quick (QML)   -> bin/VideoStudio
#   widgets/ Qt Widgets       -> bin/VideoStudioWidgets
# See docs/benchmarks/qml-vs-widgets.md.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Multimedia)
qt_standard_project_setup()

# UI-independent glue: owns the core session, preview engine and audio output.
qt_add_library(vstudio_desktop_common STATIC
  common/EditorController.cpp common/EditorController.h
  common/AudioOutput.cpp common/AudioOutput.h
)
target_include_directories(vstudio_desktop_common PUBLIC common)
target_link_libraries(vstudio_desktop_common PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia vstudio::core)
if(VSTUDIO_WITH_FFMPEG)
  target_link_libraries(vstudio_desktop_common PUBLIC vstudio::ffmpeg)
endif()

add_subdirectory(quick)
add_subdirectory(widgets)
