Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!ifndef STAGE
 !error "Pass /DSTAGE=<portable staging folder>"
!endif
!ifndef OUTPUT
 !error "Pass /DOUTPUT=<installer output path>"
!endif
Name "Indic NotePad (Beta)"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\IndicStudio\Indic NotePad Beta"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!define MUI_ABORTWARNING
!define MUI_ICON "..\assets\icons\app.ico"
!define MUI_UNICON "..\assets\icons\app.ico"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Function .onInit
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP "Indic NotePad requires 64-bit Windows 10 or later."
  Abort
 ${EndIf}
FunctionEnd
Section "Indic NotePad"
 SetShellVarContext current
 SetOutPath "$INSTDIR"
 File "${STAGE}\indic-notepad.exe"
 File "${STAGE}\LICENSE"
 File "${STAGE}\THIRD_PARTY_NOTICES.md"
 File "${STAGE}\HELP.md"
 File "${STAGE}\README.txt"
 SetOutPath "$INSTDIR\licenses"
 File "${STAGE}\licenses\*"
 SetOutPath "$INSTDIR"
 WriteUninstaller "$INSTDIR\uninstall.exe"
 CreateDirectory "$SMPROGRAMS\IndicStudio"
 CreateShortcut "$SMPROGRAMS\IndicStudio\Indic NotePad Beta.lnk" "$INSTDIR\indic-notepad.exe"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "DisplayName" "Indic NotePad (Beta)"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "DisplayVersion" "0.1.0-beta.1"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "Publisher" "IndicStudio"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "URLInfoAbout" "https://indicstudio.com/tools/indic-notepad/"
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "NoModify" 1
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta" "NoRepair" 1
SectionEnd
Section "Uninstall"
 SetShellVarContext current
 Delete "$INSTDIR\indic-notepad.exe"
 Delete "$INSTDIR\LICENSE"
 Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
 Delete "$INSTDIR\HELP.md"
 Delete "$INSTDIR\README.txt"
 Delete "$INSTDIR\licenses\Scintilla.txt"
 Delete "$INSTDIR\licenses\Lexilla.txt"
 Delete "$INSTDIR\licenses\PCRE2.md"
 RMDir "$INSTDIR\licenses"
 Delete "$INSTDIR\uninstall.exe"
 RMDir "$INSTDIR"
 Delete "$SMPROGRAMS\IndicStudio\Indic NotePad Beta.lnk"
 RMDir "$SMPROGRAMS\IndicStudio"
 DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\IndicStudio.IndicNotePad.Beta"
 ; User documents, portable data folders and normal recovery/settings are retained.
SectionEnd
