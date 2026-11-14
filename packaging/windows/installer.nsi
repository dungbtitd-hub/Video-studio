; Video Studio installer for Windows (NSIS 3, preinstalled on the GitHub Windows runners).
; makensis /DVERSION=1.2.3 /DSTAGE=<folder with VideoStudio.exe and its DLLs> /DOUT=<setup.exe> /DICON=<videostudio.ico> installer.nsi
Unicode true
!include "MUI2.nsh"

Name "Video Studio"
OutFile "${OUT}"
InstallDir "$PROGRAMFILES64\Video Studio"
InstallDirRegKey HKLM "Software\VideoStudio" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
; ICON: absolute path of videostudio.ico (makensis resolves ${__FILEDIR__} against the working
; folder when the script is given by a relative path, so the workflow passes it).
!ifndef ICON
  !define ICON "${__FILEDIR__}\..\icons\videostudio.ico"
!endif
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Video Studio"
VIAddVersionKey "CompanyName" "ITD Viet Nam"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Video Studio installer"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\VideoStudio.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Vietnamese"
!insertmacro MUI_LANGUAGE "English"

Section "Video Studio" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Video Studio.lnk" "$INSTDIR\VideoStudio.exe"
  CreateShortcut "$DESKTOP\Video Studio.lnk" "$INSTDIR\VideoStudio.exe"
  WriteRegStr HKLM "Software\VideoStudio" "InstallDir" "$INSTDIR"
  !define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoStudio"
  WriteRegStr HKLM "${UNINST}" "DisplayName" "Video Studio"
  WriteRegStr HKLM "${UNINST}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST}" "Publisher" "ITD Viet Nam"
  WriteRegStr HKLM "${UNINST}" "DisplayIcon" "$INSTDIR\VideoStudio.exe"
  WriteRegStr HKLM "${UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Video Studio.lnk"
  Delete "$DESKTOP\Video Studio.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoStudio"
  DeleteRegKey HKLM "Software\VideoStudio"
SectionEnd
