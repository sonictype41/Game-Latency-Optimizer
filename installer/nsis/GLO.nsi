Unicode True

!ifndef STAGE_DIR
  !error "STAGE_DIR is required"
!endif
!ifndef OUT_FILE
  !error "OUT_FILE is required"
!endif
!ifndef RELEASE_VERSION
  !error "RELEASE_VERSION is required"
!endif
!ifndef PROTOCOL_VERSION
  !error "PROTOCOL_VERSION is required"
!endif
!ifndef DISPLAY_VERSION
  !error "DISPLAY_VERSION is required"
!endif
!ifndef OFFICIAL_URL
  !error "OFFICIAL_URL is required"
!endif
!ifndef GITHUB_URL
  !error "GITHUB_URL is required"
!endif
!ifndef FACEBOOK_URL
  !error "FACEBOOK_URL is required"
!endif
!ifndef HAS_GITHUB
  !error "HAS_GITHUB is required"
!endif
!ifndef HAS_FACEBOOK
  !error "HAS_FACEBOOK is required"
!endif
!ifndef INSTALLER_ICON
  !error "INSTALLER_ICON is required"
!endif

Name "GLO Client"
Caption "GLO Client Installer"
OutFile "${OUT_FILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\GLO"
SetCompressor /SOLID lzma
ShowInstDetails nevershow
ShowUninstDetails nevershow
BrandingText "GLO | Open-source game routing"

!define MUI_ICON "${INSTALLER_ICON}"
!define MUI_UNICON "${INSTALLER_ICON}"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_REGISTRY_ROOT "HKCU"
!define MUI_LANGDLL_REGISTRY_KEY "Software\GLO"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!define MUI_LANGDLL_ALWAYSSHOW
!define MUI_LANGDLL_WINDOWTITLE "GLO Client Installer"
!define MUI_LANGDLL_INFO "Choose setup language / Chọn ngôn ngữ cài đặt"

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"
!include "nsDialogs.nsh"

Var WelcomeDialog
Var WelcomeTitleControl
Var WelcomeOfficialLink
Var WelcomeGithubControl
Var WelcomeFacebookControl
Var OptionsDialog
Var DesktopCheckbox
Var CreateDesktopShortcut
Var BrandFont
Var FinishDialog
Var LaunchCheckbox
Var ExistingInstallDir
Var ExistingVersion
Var ExistingUninstaller

Page custom WelcomePageCreate

!define MUI_PAGE_HEADER_TEXT "$(LicenseHeader)"
!define MUI_PAGE_HEADER_SUBTEXT "$(LicenseSubtext)"
!insertmacro MUI_PAGE_LICENSE "${STAGE_DIR}\LICENSE.txt"

!define MUI_PAGE_HEADER_TEXT "$(DirectoryHeader)"
!define MUI_PAGE_HEADER_SUBTEXT "$(DirectorySubtext)"
!insertmacro MUI_PAGE_DIRECTORY

Page custom OptionsPageCreate OptionsPageLeave

!define MUI_PAGE_HEADER_TEXT "$(InstallingHeader)"
!define MUI_PAGE_HEADER_SUBTEXT "$(InstallingSubtext)"
!insertmacro MUI_PAGE_INSTFILES

Page custom FinishPageCreate FinishPageLeave

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Vietnamese"
!insertmacro MUI_RESERVEFILE_LANGDLL

LangString WelcomeHeader ${LANG_ENGLISH} "GLO Client Installer"
LangString WelcomeHeader ${LANG_VIETNAMESE} "Trình cài đặt GLO Client"
LangString WelcomeSubheader ${LANG_ENGLISH} "Official Windows client - per-user installation"
LangString WelcomeSubheader ${LANG_VIETNAMESE} "Client Windows chính thức - cài đặt cho tài khoản hiện tại"
LangString WelcomeTitle ${LANG_ENGLISH} "Install the official GLO Client for Windows"
LangString WelcomeTitle ${LANG_VIETNAMESE} "Cài đặt GLO Client chính thức cho Windows"
LangString WelcomeVersion ${LANG_ENGLISH} "Version: ${DISPLAY_VERSION}"
LangString WelcomeVersion ${LANG_VIETNAMESE} "Phiên bản: ${DISPLAY_VERSION}"
LangString WelcomeBody ${LANG_ENGLISH} "GLO routes supported game traffic through the GLO service while other traffic stays on your normal connection.$\r$\n$\r$\nOpen-source game routing. Free official service."
LangString WelcomeBody ${LANG_VIETNAMESE} "GLO định tuyến lưu lượng trò chơi được hỗ trợ qua mạng GLO, trong khi các lưu lượng khác vẫn dùng kết nối Internet bình thường.$\r$\n$\r$\nĐịnh tuyến game mã nguồn mở. Mạng chính thức miễn phí."
LangString OfficialLink ${LANG_ENGLISH} "Official Website"
LangString OfficialLink ${LANG_VIETNAMESE} "Trang web chính thức"
LangString GithubLink ${LANG_ENGLISH} "GitHub"
LangString GithubLink ${LANG_VIETNAMESE} "GitHub"
LangString FacebookLink ${LANG_ENGLISH} "Facebook"
LangString FacebookLink ${LANG_VIETNAMESE} "Facebook"
LangString LinksLabel ${LANG_ENGLISH} "Official links"
LangString LinksLabel ${LANG_VIETNAMESE} "Liên kết chính thức"
LangString GithubUnavailable ${LANG_ENGLISH} "GitHub"
LangString GithubUnavailable ${LANG_VIETNAMESE} "GitHub"
LangString FacebookUnavailable ${LANG_ENGLISH} "Facebook"
LangString FacebookUnavailable ${LANG_VIETNAMESE} "Facebook"
LangString LinkOpenError ${LANG_ENGLISH} "Windows could not open this link."
LangString LinkOpenError ${LANG_VIETNAMESE} "Windows không thể mở liên kết này."

LangString LicenseHeader ${LANG_ENGLISH} "Open-source license"
LangString LicenseHeader ${LANG_VIETNAMESE} "Giấy phép mã nguồn mở"
LangString LicenseSubtext ${LANG_ENGLISH} "Review the GLO license before continuing. Third-party notices are installed with the client."
LangString LicenseSubtext ${LANG_VIETNAMESE} "Vui lòng đọc giấy phép GLO trước khi tiếp tục. Thông báo giấy phép bên thứ ba sẽ được cài cùng client."

LangString DirectoryHeader ${LANG_ENGLISH} "Installation location"
LangString DirectoryHeader ${LANG_VIETNAMESE} "Vị trí cài đặt"
LangString DirectorySubtext ${LANG_ENGLISH} "GLO installs for your Windows account and does not require administrator permission."
LangString DirectorySubtext ${LANG_VIETNAMESE} "GLO được cài cho tài khoản Windows hiện tại và không yêu cầu quyền quản trị viên."

LangString OptionsHeader ${LANG_ENGLISH} "Installation options"
LangString OptionsHeader ${LANG_VIETNAMESE} "Tùy chọn cài đặt"
LangString OptionsSubtext ${LANG_ENGLISH} "Choose the shortcuts you want GLO to create."
LangString OptionsSubtext ${LANG_VIETNAMESE} "Chọn các lối tắt mà GLO sẽ tạo."
LangString OptionsPerUser ${LANG_ENGLISH} "This is a per-user install. GLO itself runs normally; Windows asks for UAC only when the privileged network worker is needed for Connect."
LangString OptionsPerUser ${LANG_VIETNAMESE} "Đây là cài đặt theo người dùng. GLO chạy bình thường; Windows chỉ hỏi UAC khi cần network worker có quyền cao lúc Connect."
LangString OptionsStartMenu ${LANG_ENGLISH} "Start Menu shortcut is always created."
LangString OptionsStartMenu ${LANG_VIETNAMESE} "Lối tắt Start Menu luôn được tạo."
LangString OptionsDesktop ${LANG_ENGLISH} "Create a Desktop shortcut"
LangString OptionsDesktop ${LANG_VIETNAMESE} "Tạo lối tắt ngoài Desktop"

LangString InstallingHeader ${LANG_ENGLISH} "Installing GLO Client"
LangString InstallingHeader ${LANG_VIETNAMESE} "Đang cài đặt GLO Client"
LangString InstallingSubtext ${LANG_ENGLISH} "Installing verified client files, Wintun runtime, shortcuts and uninstaller."
LangString InstallingSubtext ${LANG_VIETNAMESE} "Đang cài client đã xác minh, Wintun runtime, lối tắt và trình gỡ cài đặt."

LangString FinishTitle ${LANG_ENGLISH} "GLO Client is ready"
LangString FinishTitle ${LANG_VIETNAMESE} "GLO Client đã sẵn sàng"
LangString FinishSubheader ${LANG_ENGLISH} "Installation completed successfully"
LangString FinishSubheader ${LANG_VIETNAMESE} "Cài đặt đã hoàn tất thành công"
LangString FinishText ${LANG_ENGLISH} "GLO Client ${DISPLAY_VERSION} is installed for your Windows account.$\r$\n$\r$\nInstall location: $INSTDIR"
LangString FinishText ${LANG_VIETNAMESE} "GLO Client ${DISPLAY_VERSION} đã được cài cho tài khoản Windows hiện tại.$\r$\n$\r$\nVị trí cài đặt: $INSTDIR"
LangString FinishRun ${LANG_ENGLISH} "Launch GLO Client"
LangString FinishRun ${LANG_VIETNAMESE} "Khởi chạy GLO Client"
LangString FinishWebsite ${LANG_ENGLISH} "Official Website"
LangString FinishWebsite ${LANG_VIETNAMESE} "Trang web chính thức"
LangString FinishButton ${LANG_ENGLISH} "Finish"
LangString FinishButton ${LANG_VIETNAMESE} "Hoàn tất"

LangString CloseRunning ${LANG_ENGLISH} "GLO is still running. Close GLO before continuing."
LangString CloseRunning ${LANG_VIETNAMESE} "GLO vẫn đang chạy. Hãy đóng GLO trước khi tiếp tục."
LangString ExistingUninstallMissing ${LANG_ENGLISH} "An existing GLO installation was detected, but its uninstaller is missing. Uninstall the old GLO from Windows Settings, then run this installer again."
LangString ExistingUninstallMissing ${LANG_VIETNAMESE} "Đã phát hiện bản GLO đang cài nhưng không tìm thấy trình gỡ cài đặt. Hãy gỡ bản GLO cũ trong Windows Settings rồi chạy lại trình cài đặt này."
LangString ExistingUninstallFailed ${LANG_ENGLISH} "The existing GLO installation could not be removed cleanly. Setup stopped to avoid mixing old and new files."
LangString ExistingUninstallFailed ${LANG_VIETNAMESE} "Không thể gỡ sạch bản GLO đang cài. Trình cài đặt đã dừng để tránh trộn file cũ và mới."
LangString ProgressStopping ${LANG_ENGLISH} "Stopping GLO..."
LangString ProgressStopping ${LANG_VIETNAMESE} "Đang dừng GLO..."
LangString ProgressUninstalling ${LANG_ENGLISH} "Uninstalling old version..."
LangString ProgressUninstalling ${LANG_VIETNAMESE} "Đang gỡ phiên bản cũ..."
LangString ProgressInstalling ${LANG_ENGLISH} "Installing GLO..."
LangString ProgressInstalling ${LANG_VIETNAMESE} "Đang cài GLO..."
LangString ProgressRegistering ${LANG_ENGLISH} "Registering glo:// protocol..."
LangString ProgressRegistering ${LANG_VIETNAMESE} "Đang đăng ký giao thức glo://..."

Function .onInit
  StrCpy $CreateDesktopShortcut "0"
  StrCpy $ExistingInstallDir ""
  StrCpy $ExistingVersion ""
  StrCpy $ExistingUninstaller ""
  ReadRegStr $ExistingInstallDir HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "InstallLocation"
  ReadRegStr $ExistingVersion HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayVersion"
  ${If} $ExistingInstallDir == ""
    IfFileExists "$LOCALAPPDATA\GLO\GLO.exe" 0 existing_detect_done
    StrCpy $ExistingInstallDir "$LOCALAPPDATA\GLO"
  ${EndIf}
  StrCpy $ExistingUninstaller "$ExistingInstallDir\uninstall0000.exe"
existing_detect_done:
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

Function un.onInit
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Function OpenOfficial
  ExecShell "open" "${OFFICIAL_URL}"
FunctionEnd

Function OpenGithub
  StrCmp "${HAS_GITHUB}" "1" 0 github_done
  ExecShell "open" "${GITHUB_URL}"
github_done:
FunctionEnd

Function OpenFacebook
  StrCmp "${HAS_FACEBOOK}" "1" 0 facebook_done
  ExecShell "open" "${FACEBOOK_URL}"
facebook_done:
FunctionEnd

Function WelcomePageCreate
  !insertmacro MUI_HEADER_TEXT "$(WelcomeHeader)" "$(WelcomeSubheader)"
  nsDialogs::Create 1018
  Pop $WelcomeDialog
  ${If} $WelcomeDialog == error
    Abort
  ${EndIf}

  CreateFont $BrandFont "Segoe UI" 14 700
  ${NSD_CreateLabel} 0 2u 100% 28u "$(WelcomeTitle)"
  Pop $WelcomeTitleControl
  SendMessage $WelcomeTitleControl ${WM_SETFONT} $BrandFont 0

  ${NSD_CreateLabel} 0 34u 100% 14u "$(WelcomeVersion)"
  Pop $0

  ${NSD_CreateLabel} 0 56u 100% 62u "$(WelcomeBody)"
  Pop $0

  ${NSD_CreateLabel} 0 114u 100% 12u "$(LinksLabel)"
  Pop $0

  ${NSD_CreateLink} 0 132u 30% 14u "$(OfficialLink)"
  Pop $WelcomeOfficialLink
  ${NSD_OnClick} $WelcomeOfficialLink OpenOfficial

  ${If} "${HAS_GITHUB}" == "1"
    ${NSD_CreateLink} 34% 132u 28% 14u "$(GithubLink)"
    Pop $WelcomeGithubControl
    ${NSD_OnClick} $WelcomeGithubControl OpenGithub
  ${Else}
    ${NSD_CreateLabel} 34% 132u 28% 14u "$(GithubUnavailable)"
    Pop $WelcomeGithubControl
    EnableWindow $WelcomeGithubControl 0
  ${EndIf}

  ${If} "${HAS_FACEBOOK}" == "1"
    ${NSD_CreateLink} 68% 132u 28% 14u "$(FacebookLink)"
    Pop $WelcomeFacebookControl
    ${NSD_OnClick} $WelcomeFacebookControl OpenFacebook
  ${Else}
    ${NSD_CreateLabel} 68% 132u 28% 14u "$(FacebookUnavailable)"
    Pop $WelcomeFacebookControl
    EnableWindow $WelcomeFacebookControl 0
  ${EndIf}

  nsDialogs::Show
FunctionEnd

Function OptionsPageCreate
  !insertmacro MUI_HEADER_TEXT "$(OptionsHeader)" "$(OptionsSubtext)"
  nsDialogs::Create 1018
  Pop $OptionsDialog
  ${If} $OptionsDialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 5u 100% 50u "$(OptionsPerUser)"
  Pop $0
  ${NSD_CreateLabel} 0 62u 100% 14u "$(OptionsStartMenu)"
  Pop $0
  ${NSD_CreateCheckbox} 0 88u 100% 14u "$(OptionsDesktop)"
  Pop $DesktopCheckbox
  StrCmp $CreateDesktopShortcut "1" 0 +2
    ${NSD_Check} $DesktopCheckbox

  nsDialogs::Show
FunctionEnd

Function OptionsPageLeave
  ${NSD_GetState} $DesktopCheckbox $0
  StrCpy $CreateDesktopShortcut "0"
  ${If} $0 == ${BST_CHECKED}
    StrCpy $CreateDesktopShortcut "1"
  ${EndIf}
FunctionEnd

Function FinishPageCreate
  !insertmacro MUI_HEADER_TEXT "$(FinishTitle)" "$(FinishSubheader)"
  nsDialogs::Create 1018
  Pop $FinishDialog
  ${If} $FinishDialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0 8u 100% 54u "$(FinishText)"
  Pop $0
  ${NSD_CreateCheckbox} 0 76u 100% 14u "$(FinishRun)"
  Pop $LaunchCheckbox
  ${NSD_Check} $LaunchCheckbox
  ${NSD_CreateLink} 0 106u 45% 14u "$(FinishWebsite)"
  Pop $0
  ${NSD_OnClick} $0 OpenOfficial

  GetDlgItem $0 $HWNDPARENT 1
  SendMessage $0 ${WM_SETTEXT} 0 "STR:$(FinishButton)"
  GetDlgItem $0 $HWNDPARENT 3
  EnableWindow $0 0
  GetDlgItem $0 $HWNDPARENT 2
  EnableWindow $0 0

  nsDialogs::Show
FunctionEnd

Function FinishPageLeave
  ${NSD_GetState} $LaunchCheckbox $0
  ${If} $0 == ${BST_CHECKED}
    ExecShell "open" "$INSTDIR\GLO.exe"
  ${EndIf}
FunctionEnd

Function PurgeExistingInstallationFiles
  ; Built-in fallback for damaged/legacy installs whose uninstaller is missing
  ; or returns failure. User data (settings.json and logs) is preserved.
  Delete "$ExistingInstallDir\GLO.exe"
  Delete "$ExistingInstallDir\wintun.dll"
  Delete "$ExistingInstallDir\WINTUN_INFO.json"
  Delete "$ExistingInstallDir\BUILD_INFO.json"
  Delete "$ExistingInstallDir\LICENSE.txt"
  Delete "$ExistingInstallDir\THIRD_PARTY_NOTICES.md"
  Delete "$ExistingInstallDir\WINTUN_LICENSE.txt"
  Delete "$ExistingInstallDir\LIBSODIUM_LICENSE.txt"
  Delete "$ExistingInstallDir\SHA256SUMS.txt"
  Delete "$ExistingInstallDir\uninstall0000.exe"
  Delete "$DESKTOP\GLO.lnk"
  Delete "$SMPROGRAMS\GLO\GLO.lnk"
  Delete "$SMPROGRAMS\GLO\Uninstall GLO.lnk"
  RMDir "$SMPROGRAMS\GLO"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO"
  DeleteRegKey HKCU "Software\Classes\glo"
  RMDir "$ExistingInstallDir"
FunctionEnd

Function RemoveExistingInstallation
  ${If} $ExistingInstallDir == ""
    Return
  ${EndIf}
  IfFileExists "$ExistingInstallDir\GLO.exe" existing_present 0
  ReadRegStr $0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayName"
  StrCmp $0 "" existing_none existing_present
existing_present:
  DetailPrint "$(ProgressStopping)"
  Call CloseGLO
  DetailPrint "$(ProgressUninstalling)"
  ; The legacy uninstaller is best-effort. Always follow it with GLO's own
  ; bounded hard cleanup so an old NSIS self-delete race cannot force users
  ; to run setup twice.
  IfFileExists "$ExistingUninstaller" 0 existing_cleanup
  ExecWait '"$ExistingUninstaller" /S' $0
existing_cleanup:
  StrCpy $3 0
existing_retry:
  Call PurgeExistingInstallationFiles
  IfFileExists "$ExistingInstallDir\GLO.exe" existing_retry_wait 0
  ReadRegStr $1 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayName"
  StrCmp $1 "" existing_clean existing_retry_wait
existing_retry_wait:
  IntOp $3 $3 + 1
  IntCmp $3 20 existing_failed existing_wait existing_failed
existing_wait:
  Sleep 200
  Goto existing_retry
existing_clean:
  StrCpy $ExistingInstallDir ""
  StrCpy $ExistingVersion ""
  StrCpy $ExistingUninstaller ""
  Return
existing_failed:
  MessageBox MB_ICONSTOP|MB_OK "$(ExistingUninstallFailed)"
  Abort
existing_none:
  StrCpy $ExistingInstallDir ""
  StrCpy $ExistingVersion ""
  StrCpy $ExistingUninstaller ""
FunctionEnd

Function CloseGLO
  ; Graceful close for known client generations, then a bounded forced-stop
  ; loop. The repeated taskkill closes the race where a worker or old GUI
  ; survives for a few hundred milliseconds after WM_CLOSE.
  FindWindow $1 "GLOGenericClient"
  IntCmp $1 0 +2
  SendMessage $1 ${WM_CLOSE} 0 0
  FindWindow $1 "GLOGenericClientV0171"
  IntCmp $1 0 +2
  SendMessage $1 ${WM_CLOSE} 0 0
  FindWindow $1 "GLOGenericClientV0170"
  IntCmp $1 0 +2
  SendMessage $1 ${WM_CLOSE} 0 0
  Sleep 250
  StrCpy $0 0
close_force_loop:
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM GLO.exe /T /F'
  Pop $2
  Sleep 200
  IntOp $0 $0 + 1
  IntCmp $0 6 close_force_done close_force_loop close_force_done
close_force_done:
FunctionEnd

Section "GLO Client" SEC_MAIN
  SectionIn RO
  SetDetailsPrint textonly
  Call RemoveExistingInstallation
  Call CloseGLO
  DetailPrint "$(ProgressInstalling)"
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /oname=GLO.exe "${STAGE_DIR}\GLO.exe"
  File /oname=wintun.dll "${STAGE_DIR}\wintun.dll"
  File /oname=WINTUN_INFO.json "${STAGE_DIR}\WINTUN_INFO.json"
  File /oname=BUILD_INFO.json "${STAGE_DIR}\BUILD_INFO.json"
  File /oname=LICENSE.txt "${STAGE_DIR}\LICENSE.txt"
  File /oname=THIRD_PARTY_NOTICES.md "${STAGE_DIR}\THIRD_PARTY_NOTICES.md"
  File /oname=WINTUN_LICENSE.txt "${STAGE_DIR}\WINTUN_LICENSE.txt"
  File /oname=LIBSODIUM_LICENSE.txt "${STAGE_DIR}\LIBSODIUM_LICENSE.txt"
  File /oname=SHA256SUMS.txt "${STAGE_DIR}\SHA256SUMS.txt"
  ; Install an explicit branded icon for Windows shortcuts/shell entries.
  ; Relying on implicit shortcut icon extraction can fall back to a generic gear icon.
  File /oname=GLO.ico "${INSTALLER_ICON}"
  WriteUninstaller "$INSTDIR\uninstall0000.exe"

  CreateDirectory "$SMPROGRAMS\GLO"
  CreateShortcut "$SMPROGRAMS\GLO\GLO.lnk" "$INSTDIR\GLO.exe" "" "$INSTDIR\GLO.ico" 0
  CreateShortcut "$SMPROGRAMS\GLO\Uninstall GLO.lnk" "$INSTDIR\uninstall0000.exe"
  StrCmp $CreateDesktopShortcut "1" 0 +2
    CreateShortcut "$DESKTOP\GLO.lnk" "$INSTDIR\GLO.exe" "" "$INSTDIR\GLO.ico" 0

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayName" "GLO (Game Latency Optimizer)"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayVersion" "${DISPLAY_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "Publisher" "GLO"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "URLInfoAbout" "${OFFICIAL_URL}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "DisplayIcon" "$INSTDIR\GLO.ico"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "UninstallString" '"$INSTDIR\uninstall0000.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO" "NoRepair" 1
  ; writes a fresh protocol handler only after the old installation is gone
  DetailPrint "$(ProgressRegistering)"
  WriteRegStr HKCU "Software\Classes\glo" "" "URL:GLO Protocol"
  WriteRegStr HKCU "Software\Classes\glo" "URL Protocol" ""
  WriteRegStr HKCU "Software\Classes\glo\DefaultIcon" "" "$INSTDIR\GLO.ico,0"
  WriteRegStr HKCU "Software\Classes\glo\shell\open\command" "" '"$INSTDIR\GLO.exe" --uri "%1"'
SectionEnd

Section "Uninstall"
  Call un.CloseGLO
  SetShellVarContext current
  Delete "$DESKTOP\GLO.lnk"
  Delete "$SMPROGRAMS\GLO\GLO.lnk"
  Delete "$SMPROGRAMS\GLO\Uninstall GLO.lnk"
  RMDir "$SMPROGRAMS\GLO"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\GLO"
  DeleteRegValue HKCU "Software\GLO" "InstallerLanguage"
  DeleteRegKey /ifempty HKCU "Software\GLO"

  Delete "$INSTDIR\GLO.exe"
  Delete "$INSTDIR\wintun.dll"
  Delete "$INSTDIR\WINTUN_INFO.json"
  Delete "$INSTDIR\BUILD_INFO.json"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  Delete "$INSTDIR\WINTUN_LICENSE.txt"
  Delete "$INSTDIR\LIBSODIUM_LICENSE.txt"
  Delete "$INSTDIR\SHA256SUMS.txt"
  Delete "$INSTDIR\GLO.ico"
  Delete "$INSTDIR\uninstall0000.exe"
  ; settings.json and logs are user data and intentionally survive uninstall.
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "Software\Classes\glo"
SectionEnd

Function un.CloseGLO
  FindWindow $1 "GLOGenericClient"
  IntCmp $1 0 +2
  SendMessage $1 ${WM_CLOSE} 0 0
  Sleep 250
  StrCpy $0 0
un_close_force_loop:
  nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /IM GLO.exe /T /F'
  Pop $2
  Sleep 200
  IntOp $0 $0 + 1
  IntCmp $0 6 un_close_done un_close_force_loop un_close_done
un_close_done:
FunctionEnd
