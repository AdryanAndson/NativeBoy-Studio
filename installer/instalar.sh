#!/bin/sh
# NativeBoy Studio (versão portátil): cria o atalho no menu de aplicativos e,
# se faltar, oferece instalar o compilador g++ e o zenity (janelas de arquivo).
DIR=$(cd "$(dirname "$0")" && pwd)
APPS="$HOME/.local/share/applications"
ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
mkdir -p "$APPS" "$ICONS"
cp "$DIR/assets/icon.png" "$ICONS/nativeboy-studio.png"
cat > "$APPS/nativeboy-studio.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=NativeBoy Studio
GenericName=Conversor de jogos de Game Boy
Comment=Jogue, descompile, modifique e converta jogos de Game Boy em programas nativos
Exec="$DIR/NativeBoyStudio" %f
Path=$DIR
Icon=nativeboy-studio
Terminal=false
Categories=Game;Development;
MimeType=application/x-gameboy-rom;application/x-gameboy-color-rom;
DESK
chmod +x "$APPS/nativeboy-studio.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$APPS" 2>/dev/null
echo "Atalho criado: menu de aplicativos > NativeBoy Studio"
FALTA=""
command -v g++ >/dev/null 2>&1 || FALTA="$FALTA g++"
command -v zenity >/dev/null 2>&1 || FALTA="$FALTA zenity"
if [ -n "$FALTA" ]; then
  echo
  echo "Para converter jogos é preciso instalar:$FALTA"
  printf "Instalar agora com o apt? (será pedida a sua senha) [S/n] "
  read r
  case "$r" in n|N) echo "Depois, rode: sudo apt install$FALTA";; *) sudo apt install -y $FALTA;; esac
fi
echo "Pronto. Abra o NativeBoy Studio pelo menu ou com: $DIR/NativeBoyStudio"
