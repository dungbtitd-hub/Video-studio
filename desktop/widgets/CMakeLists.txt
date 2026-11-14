find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

# App icon (packaging/icons, drawn by make_icons.py): .ico through a resource script on
# Windows, .icns in the bundle on macOS, a PNG for the window icon everywhere.
set(_icons ${PROJECT_SOURCE_DIR}/packaging/icons)
set(VSTUDIO_APP_ICON_SOURCES app.qrc)
if(WIN32)
  configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/VideoStudio.rc.in ${CMAKE_CURRENT_BINARY_DIR}/VideoStudio.rc @ONLY)
  list(APPEND VSTUDIO_APP_ICON_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/VideoStudio.rc)
elseif(APPLE)
  set_source_files_properties(${_icons}/videostudio.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  list(APPEND VSTUDIO_APP_ICON_SOURCES ${_icons}/videostudio.icns)
endif()

qt_add_executable(VideoStudio
  main.cpp
  Theme.h
  MainWindow.cpp MainWindow.h
  PreviewWidget.cpp PreviewWidget.h
  TimelineWidget.cpp TimelineWidget.h
  LooksPanel.cpp LooksPanel.h
  GradePanel.cpp GradePanel.h
  ScopesPanel.cpp ScopesPanel.h
  GradeWidgets.cpp GradeWidgets.h
  CoverPanel.cpp CoverPanel.h
  TransitionsPanel.cpp TransitionsPanel.h
  AnimationBox.cpp AnimationBox.h
  TextPanel.cpp TextPanel.h
  StickerPanel.cpp StickerPanel.h
  OverlayPanel.cpp OverlayPanel.h
  RecentStrip.cpp RecentStrip.h
  VideoFxPanel.cpp VideoFxPanel.h
  SubtitlePanel.cpp SubtitlePanel.h
  ${VSTUDIO_APP_ICON_SOURCES}
)
target_compile_definitions(VideoStudio PRIVATE VSTUDIO_VERSION="${PROJECT_VERSION}")
target_link_libraries(VideoStudio PRIVATE vstudio_desktop_common Qt6::Widgets)
set_target_properties(VideoStudio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  MACOSX_BUNDLE_BUNDLE_NAME "Video Studio"
  MACOSX_BUNDLE_GUI_IDENTIFIER "vn.com.itdvietnam.videostudio"
  MACOSX_BUNDLE_ICON_FILE videostudio.icns
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  MACOSX_BUNDLE_COPYRIGHT "Công ty CP Công nghệ ITD Việt Nam")

# ---- install / release packaging (see packaging/README.md) ----
include(GNUInstallDirs)
if(APPLE)
  install(TARGETS VideoStudio BUNDLE DESTINATION .)
else()
  install(TARGETS VideoStudio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(UNIX AND NOT APPLE)
  install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/videostudio.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${_icons}/videostudio.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
endif()
# Windows/macOS: the release workflow copies Qt and FFmpeg next to the app with
# windeployqt / macdeployqt and builds the installer (.github/workflows/release.yml).
