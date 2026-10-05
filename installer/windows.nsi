# -*- coding: utf-8 -*-
Unicode true
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!define PRODUCT "Mirrorfly Office"
!define REGROOT "Software\Mirrorfly\MirrorflyOffice"
!define UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\MirrorflyOffice"
Name "${PRODUCT} ${DISPLAY_VERSION} 测试版"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\Mirrorfly Office"
InstallDirRegKey HKCU "${UNINSTALL}" "InstallLocation"
BrandingText "舒宇 镜蝶科技 研发部"
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "${PRODUCT}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${DISPLAY_VERSION}"
VIAddVersionKey /LANG=2052 "FileVersion" "${VERSION}.0"
VIAddVersionKey /LANG=2052 "FileDescription" "Mirrorfly Office 测试版安装程序"
VIAddVersionKey /LANG=2052 "LegalCopyright" "镜蝶科技"
!define MUI_ICON "${PROJECT}\assets\mirrorfly.ico"
!define MUI_UNICON "${PROJECT}\assets\mirrorfly.ico"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
Var PreviousDir

!macro CheckExecutable PREFIX
Function ${PREFIX}CheckExecutable
    IfFileExists "$INSTDIR\MirrorflyOffice.exe" 0 available
    System::Call 'kernel32::CreateFileW(w "$INSTDIR\MirrorflyOffice.exe", i 0x80000000, i 0, p 0, i 3, i 0, p 0) p .r0'
    ${If} $0 == -1
        MessageBox MB_OK|MB_ICONEXCLAMATION "请先关闭 Mirrorfly Office，再安装或卸载。" /SD IDOK
        Abort
    ${EndIf}
    System::Call 'kernel32::CloseHandle(p r0)'
    available:
FunctionEnd
!macroend
!insertmacro CheckExecutable ""
!insertmacro CheckExecutable "un."

Function .onInit
    SetShellVarContext current
    SetRegView 64
    ${IfNot} ${RunningX64}
        MessageBox MB_OK "此安装包需要 64 位 Windows 10 或更新系统。" /SD IDOK
        Abort
    ${EndIf}
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK "此安装包需要 Windows 10 或更新系统。" /SD IDOK
        Abort
    ${EndIf}
    ReadRegStr $PreviousDir HKCU "${UNINSTALL}" "InstallLocation"
FunctionEnd

Function ValidateDirectory
    ${If} $PreviousDir != ""
    ${AndIf} $PreviousDir != $INSTDIR
        MessageBox MB_OK "已安装的版本位于 $PreviousDir。请使用原目录升级，或先卸载旧版本。" /SD IDOK
        Abort
    ${EndIf}
    IfFileExists "$INSTDIR\*.*" 0 directory_ok
    ReadINIStr $0 "$INSTDIR\mirrorfly-install.ini" "Installation" "Product"
    ${If} $0 != "MirrorflyOffice"
        MessageBox MB_OK "请选择空文件夹，以保留该目录已有的文件。" /SD IDOK
        Abort
    ${EndIf}
    directory_ok:
FunctionEnd

!macro RegisterType EXT LABEL
    WriteRegStr HKCU "Software\Classes\MirrorflyOffice.${EXT}" "" "${LABEL}"
    WriteRegStr HKCU "Software\Classes\MirrorflyOffice.${EXT}\DefaultIcon" "" '$\"$INSTDIR\MirrorflyOffice.exe$\",0'
    WriteRegStr HKCU "Software\Classes\MirrorflyOffice.${EXT}\shell\open\command" "" '$\"$INSTDIR\MirrorflyOffice.exe$\" --open $\"%1$\"'
    WriteRegStr HKCU "Software\Classes\.${EXT}\OpenWithProgids" "MirrorflyOffice.${EXT}" ""
    WriteRegStr HKCU "Software\Classes\Applications\MirrorflyOffice.exe\SupportedTypes" ".${EXT}" ""
    WriteRegStr HKCU "${REGROOT}\Capabilities\FileAssociations" ".${EXT}" "MirrorflyOffice.${EXT}"
!macroend

Section "Mirrorfly Office"
    Call ValidateDirectory
    Call CheckExecutable
    SetOverwrite on
    !include "${PAYLOAD_INSTALL}"
    SetOutPath "$INSTDIR"
    WriteINIStr "$INSTDIR\mirrorfly-install.ini" "Installation" "Product" "MirrorflyOffice"
    WriteINIStr "$INSTDIR\mirrorfly-install.ini" "Installation" "Version" "${DISPLAY_VERSION}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\Mirrorfly Office"
    CreateShortcut "$SMPROGRAMS\Mirrorfly Office\Mirrorfly Office.lnk" "$INSTDIR\MirrorflyOffice.exe"
    CreateShortcut "$SMPROGRAMS\Mirrorfly Office\卸载 Mirrorfly Office.lnk" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${UNINSTALL}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKCU "${UNINSTALL}" "DisplayVersion" "${DISPLAY_VERSION}"
    WriteRegStr HKCU "${UNINSTALL}" "Publisher" "镜蝶科技"
    WriteRegStr HKCU "${UNINSTALL}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL}" "DisplayIcon" '$\"$INSTDIR\MirrorflyOffice.exe$\",0'
    WriteRegStr HKCU "${UNINSTALL}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
    WriteRegStr HKCU "${UNINSTALL}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
    WriteRegDWORD HKCU "${UNINSTALL}" "EstimatedSize" ${SIZE_KB}
    WriteRegDWORD HKCU "${UNINSTALL}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL}" "NoRepair" 1
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\MirrorflyOffice.exe" "" "$INSTDIR\MirrorflyOffice.exe"
    WriteRegStr HKCU "Software\Classes\Applications\MirrorflyOffice.exe" "FriendlyAppName" "${PRODUCT}"
    WriteRegStr HKCU "Software\Classes\Applications\MirrorflyOffice.exe\shell\open\command" "" '$\"$INSTDIR\MirrorflyOffice.exe$\" --open $\"%1$\"'
    WriteRegStr HKCU "${REGROOT}\Capabilities" "ApplicationName" "${PRODUCT}"
    WriteRegStr HKCU "${REGROOT}\Capabilities" "ApplicationDescription" "轻量文档、表格、演示、PDF 与思维导图"
    WriteRegStr HKCU "${REGROOT}\Capabilities" "ApplicationIcon" '$\"$INSTDIR\MirrorflyOffice.exe$\",0'
    WriteRegStr HKCU "Software\RegisteredApplications" "Mirrorfly Office" "${REGROOT}\Capabilities"
    !insertmacro RegisterType "txt" "文本文档"
    !insertmacro RegisterType "text" "文本文档"
    !insertmacro RegisterType "md" "Markdown 文档"
    !insertmacro RegisterType "markdown" "Markdown 文档"
    !insertmacro RegisterType "docx" "Word 文档"
    !insertmacro RegisterType "xlsx" "Excel 工作簿"
    !insertmacro RegisterType "pptx" "PowerPoint 演示文稿"
    !insertmacro RegisterType "pdf" "PDF 文档"
    !insertmacro RegisterType "mfg" "Mirrorfly 自由导图"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Function un.onInit
    SetShellVarContext current
    SetRegView 64
    ReadINIStr $0 "$INSTDIR\mirrorfly-install.ini" "Installation" "Product"
    ReadRegStr $1 HKCU "${UNINSTALL}" "InstallLocation"
    ${If} $0 != "MirrorflyOffice"
    ${OrIf} $1 != $INSTDIR
        MessageBox MB_OK "安装目录校验失败，请从系统应用列表卸载。" /SD IDOK
        Abort
    ${EndIf}
    Call un.CheckExecutable
FunctionEnd

!macro UnregisterType EXT
    DeleteRegValue HKCU "Software\Classes\.${EXT}\OpenWithProgids" "MirrorflyOffice.${EXT}"
    DeleteRegKey /ifempty HKCU "Software\Classes\.${EXT}\OpenWithProgids"
    DeleteRegKey /ifempty HKCU "Software\Classes\.${EXT}"
    DeleteRegKey HKCU "Software\Classes\MirrorflyOffice.${EXT}"
!macroend

Section "Uninstall"
    !insertmacro UnregisterType "txt"
    !insertmacro UnregisterType "text"
    !insertmacro UnregisterType "md"
    !insertmacro UnregisterType "markdown"
    !insertmacro UnregisterType "docx"
    !insertmacro UnregisterType "xlsx"
    !insertmacro UnregisterType "pptx"
    !insertmacro UnregisterType "pdf"
    !insertmacro UnregisterType "mfg"
    DeleteRegKey HKCU "Software\Classes\Applications\MirrorflyOffice.exe"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\App Paths\MirrorflyOffice.exe"
    DeleteRegValue HKCU "Software\RegisteredApplications" "Mirrorfly Office"
    DeleteRegKey HKCU "${REGROOT}"
    DeleteRegKey /ifempty HKCU "Software\Mirrorfly"
    DeleteRegKey HKCU "${UNINSTALL}"
    Delete "$SMPROGRAMS\Mirrorfly Office\Mirrorfly Office.lnk"
    Delete "$SMPROGRAMS\Mirrorfly Office\卸载 Mirrorfly Office.lnk"
    RMDir "$SMPROGRAMS\Mirrorfly Office"
    !include "${PAYLOAD_REMOVE}"
    Delete "$INSTDIR\mirrorfly-install.ini"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd
