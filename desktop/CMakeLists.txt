# Qt 6 desktop app (Windows, macOS; Linux for development/CI): Qt Widgets
# (chosen over Qt Quick in P0, see docs/benchmarks/qml-vs-widgets.md).
#   common/  UI-independent controller: core session, preview, audio
#   widgets/ the app -> bin/VideoStudio
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Multimedia Network)
qt_standard_project_setup()

# UI-independent glue: owns the core session, preview engine and audio output.
qt_add_library(vstudio_desktop_common STATIC
  common/EditorController.cpp common/EditorController.h
  common/AudioOutput.cpp common/AudioOutput.h
  common/TranslationClient.cpp common/TranslationClient.h
  common/VoiceRecorder.cpp common/VoiceRecorder.h
  common/EditorControllerTools.cpp
)
target_include_directories(vstudio_desktop_common PUBLIC common)
target_link_libraries(vstudio_desktop_common PUBLIC Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Network vstudio::core)
if(VSTUDIO_WITH_FFMPEG)
  target_link_libraries(vstudio_desktop_common PUBLIC vstudio::ffmpeg)
endif()
if(TARGET vstudio_libraw)
  target_link_libraries(vstudio_desktop_common PUBLIC vstudio::libraw)  # camera RAW photos
endif()
if(TARGET vstudio_apple)
  target_link_libraries(vstudio_desktop_common PUBLIC vstudio::apple)  # Metal compositor
endif()
if(TARGET vstudio_windows)
  target_link_libraries(vstudio_desktop_common PUBLIC vstudio::windows)  # D3D11 compositor
endif()

add_subdirectory(widgets)

if(VSTUDIO_BUILD_TESTS)
  add_subdirectory(tests)
endif()
