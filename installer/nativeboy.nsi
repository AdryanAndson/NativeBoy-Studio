; NativeBoy Studio - instalador (NSIS 3)
Target amd64-unicode
SetCompressor /SOLID lzma
SetCompressorDictSize 64

!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!include "FileFunc.nsh"

!define APPNAME "NativeBoy Studio"
!define VERSION "1.16.0"
!define PUBLISHER "NativeBoy"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NativeBoyStudio"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTDIR}\NativeBoyStudio_Setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\NativeBoyStudio" "InstallDir"
RequestExecutionLevel admin
BrandingText "${APPNAME} ${VERSION}"
VIProductVersion "1.16.0.0"
VIAddVersionKey /LANG=1046 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1046 "FileDescription" "Instalador do ${APPNAME}"
VIAddVersionKey /LANG=1046 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1046 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1046 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=1046 "LegalCopyright" "MIT"

!define MUI_ICON "${STAGE}\assets\icon.ico"
!define MUI_UNICON "${STAGE}\assets\icon.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Bem-vindo ao ${APPNAME}"
!define MUI_WELCOMEPAGE_TEXT "Este assistente vai instalar o ${APPNAME} ${VERSION}.$\r$\n$\r$\nTudo o que é necessário já está incluído: o Studio, o compilador C++ para gerar executáveis Windows x64 e o montador RGBDS. Não é preciso configurar nenhum ambiente de desenvolvimento.$\r$\n$\r$\nClique em Próximo para continuar."
!define MUI_FINISHPAGE_RUN "$INSTDIR\NativeBoyStudio.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir o NativeBoy Studio agora"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\LEIA-ME.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "Ver o guia rápido (LEIA-ME)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\licencas\NativeBoy.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "O ${APPNAME} requer o Windows 10 ou 11 de 64 bits (x64)."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONEXCLAMATION|MB_YESNO "O ${APPNAME} foi feito para o Windows 10 ou 11. Deseja instalar mesmo assim?" IDYES +2
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "NativeBoy Studio (obrigatório)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; remove the previous SDK/toolchain so updates never mix files
  RMDir /r "$INSTDIR\sdk"
  RMDir /r "$INSTDIR\toolchain"
  File /r "${STAGE}\*"
  WriteRegStr HKLM "Software\NativeBoyStudio" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\Desinstalar.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\NativeBoyStudio.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Desinstalar.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\NativeBoyStudio.exe" "" "$INSTDIR\NativeBoyStudio.exe" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Guia rápido.lnk" "$INSTDIR\LEIA-ME.txt"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Desinstalar.lnk" "$INSTDIR\Desinstalar.exe"
SectionEnd

Section "Atalho na Área de Trabalho" SecDesktop
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\NativeBoyStudio.exe" "" "$INSTDIR\NativeBoyStudio.exe" 0
SectionEnd

Section "Abrir arquivos .gb e .gbc com o NativeBoy Studio" SecAssoc
  WriteRegStr HKCR "NativeBoy.ROM" "" "ROM de Game Boy"
  WriteRegStr HKCR "NativeBoy.ROM\DefaultIcon" "" "$INSTDIR\NativeBoyStudio.exe,0"
  WriteRegStr HKCR "NativeBoy.ROM\shell\open\command" "" '"$INSTDIR\NativeBoyStudio.exe" "%1"'
  WriteRegStr HKCR ".gb\OpenWithProgids" "NativeBoy.ROM" ""
  WriteRegStr HKCR ".gbc\OpenWithProgids" "NativeBoy.ROM" ""
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "O Studio, o compilador C++ integrado (GCC/w64devkit), o montador RGBDS e o SDK de runtime."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Cria um atalho na Área de Trabalho."
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} "Permite abrir ROMs com duplo clique (aparece em 'Abrir com')."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  SetRegView 64
  RMDir /r "$INSTDIR\sdk"
  RMDir /r "$INSTDIR\toolchain"
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\licencas"
  Delete "$INSTDIR\NativeBoyStudio.exe"
  Delete "$INSTDIR\SDL2.dll"
  Delete "$INSTDIR\LEIA-ME.txt"
  Delete "$INSTDIR\Desinstalar.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\NativeBoyStudio"
  DeleteRegKey HKCR "NativeBoy.ROM"
  DeleteRegValue HKCR ".gb\OpenWithProgids" "NativeBoy.ROM"
  DeleteRegValue HKCR ".gbc\OpenWithProgids" "NativeBoy.ROM"
  ; seus projetos e jogos em Documentos\NativeBoy Studio NÃO são removidos
SectionEnd
