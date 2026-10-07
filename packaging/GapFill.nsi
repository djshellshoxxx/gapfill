; GapFill Windows installer (NSIS 3).
; Built by packaging/package_release.sh; expects these defines:
;   VERSION  - display version, e.g. "0.0.1 beta"
;   SRCDIR   - folder holding GapFill.exe, GapFill.clap and GapFill.vst3/
;   OUTFILE  - installer path to write
Target amd64-unicode
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!include "MUI2.nsh"
!include "x64.nsh"

Name "GapFill ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Circuit Drift Labs\GapFill"
BrandingText "Circuit Drift Labs - GapFill ${VERSION}"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\GapFill"
!define MUI_ICON "${SRCDIR}\GapFill.ico"
!define MUI_UNICON "${SRCDIR}\GapFill.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "GapFill requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "VST3 plugin" SecVST3
  SetOutPath "$COMMONFILES64\VST3"
  File /r "${SRCDIR}\GapFill.vst3"
SectionEnd

Section "CLAP plugin" SecCLAP
  SetOutPath "$COMMONFILES64\CLAP"
  File "${SRCDIR}\GapFill.clap"
SectionEnd

Section "Standalone application" SecApp
  SetOutPath "$INSTDIR"
  File "${SRCDIR}\GapFill.exe"
  CreateDirectory "$SMPROGRAMS\GapFill"
  CreateShortcut "$SMPROGRAMS\GapFill\GapFill.lnk" "$INSTDIR\GapFill.exe"
  CreateShortcut "$SMPROGRAMS\GapFill\Uninstall GapFill.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "-Common"
  SetOutPath "$INSTDIR"
  File "${SRCDIR}\LICENSE.txt"
  File "${SRCDIR}\README.txt"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "GapFill ${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "Circuit Drift Labs"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\GapFill.exe"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

LangString DESC_VST3 ${LANG_ENGLISH} "Installs GapFill.vst3 into Common Files\VST3."
LangString DESC_CLAP ${LANG_ENGLISH} "Installs GapFill.clap into Common Files\CLAP."
LangString DESC_APP  ${LANG_ENGLISH} "Installs the standalone GapFill application with a Start menu shortcut."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} $(DESC_VST3)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecCLAP} $(DESC_CLAP)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecApp} $(DESC_APP)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetRegView 64
  RMDir /r "$COMMONFILES64\VST3\GapFill.vst3"
  Delete "$COMMONFILES64\CLAP\GapFill.clap"
  Delete "$INSTDIR\GapFill.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  RMDir /r "$SMPROGRAMS\GapFill"
  DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
