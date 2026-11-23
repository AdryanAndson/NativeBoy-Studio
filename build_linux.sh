#!/bin/sh
# Linux x64 (Ubuntu/Lubuntu 24.04+) build of NativeBoy Studio and .deb package.
set -e
cd "$(dirname "$0")"
N=$(pwd)
if [ -n "${DEPS:-}" ]; then D=$DEPS; elif [ -d "$N/deps" ]; then D=$N/deps; else D=$(cd "$N/.." && pwd)/deps; fi
VER=1.16.0
OUT=$N/build/linux
PKG=$N/dist/linux/pkg
APP=$PKG/opt/nativeboy-studio
mkdir -p "$OUT/obj"
g++ -O2 -o build/host_gen_interp tools/gen_interp.cpp core/optable.cpp
build/host_gen_interp core/interp_gen.inc
INC="$(sdl2-config --cflags) -I$D/imgui -I$D/imgui/backends -I$D/stb -I$N/core -I$N/common -DSDL_MAIN_HANDLED"
SRCS="studio/main.cpp studio/project.cpp studio/toolchain.cpp studio/translate.cpp studio/studio_extras.cpp studio/perfis.cpp studio/github_apk.cpp recomp/analysis.cpp recomp/codegen.cpp recomp/disasm.cpp
core/machine.cpp core/apu.cpp core/sgb.cpp core/interp.cpp core/optable.cpp common/platform.cpp common/settings.cpp common/video.cpp
common/ui_common.cpp common/player.cpp common/player_extras.cpp common/console.cpp common/touch.cpp common/music.cpp common/recorder.cpp common/link.cpp $D/imgui/imgui.cpp $D/imgui/imgui_draw.cpp $D/imgui/imgui_tables.cpp
$D/imgui/imgui_widgets.cpp $D/imgui/backends/imgui_impl_sdl2.cpp $D/imgui/backends/imgui_impl_sdlrenderer2.cpp"
pids=""
for f in $SRCS; do
  o="$OUT/obj/$(echo $f | sed 's#.*/##; s#\.cpp$##').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ core/gb.h -nt "$o" ] || [ common/player.h -nt "$o" ] || [ studio/project.h -nt "$o" ] || [ common/video.h -nt "$o" ] || [ common/link.h -nt "$o" ]; then
    g++ -O2 -std=c++17 -Wall -Wno-invalid-offsetof -Wno-misleading-indentation -Wno-unused-function -Wno-format-truncation -Wno-unused-result $INC -c "$f" -o "$o" &
    pids="$pids $!"
  fi
done
fail=0; for p in $pids; do wait $p || fail=1; done
[ $fail = 0 ] || { echo "COMPILE FAILED"; exit 1; }
g++ -o "$OUT/NativeBoyStudio" "$OUT"/obj/*.o $(sdl2-config --libs) -pthread -static-libstdc++ -static-libgcc -Wl,-rpath,'$ORIGIN/lib'
strip "$OUT/NativeBoyStudio"
echo "BUILD OK: $OUT/NativeBoyStudio"

# ---- package tree
rm -rf "$PKG"; mkdir -p "$APP" "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/applications" \
  "$PKG/usr/share/icons/hicolor/256x256/apps" "$PKG/usr/share/icons/hicolor/48x48/apps" "$PKG/usr/share/doc/nativeboy-studio"
cp "$OUT/NativeBoyStudio" "$APP/"
mkdir -p "$APP/assets/fonts" "$APP/sdk/core" "$APP/sdk/common" "$APP/sdk/runtime" "$APP/sdk/imgui/backends" "$APP/sdk/stb" "$APP/toolchain/rgbds" "$APP/licencas"
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$APP/assets/fonts/"
cp "$N/data/jogos_gb.txt" "$APP/assets/" 2>/dev/null || cp data/jogos_gb.txt "$APP/assets/"
ICO=$N/build/win/icon.ico
if [ ! -f "$ICO" ]; then   # (the same icon the Windows build makes)
  mkdir -p "$N/build/win"
  convert -size 256x256 xc:none -fill '#1b2a24' -draw 'roundrectangle 8,8 248,248 40,40' \
    -fill '#9bbc0f' -draw 'rectangle 48,40 208,160' -fill '#0f380f' -draw 'rectangle 64,56 192,144' \
    -fill '#8bac0f' -font DejaVu-Sans-Bold -pointsize 60 -gravity center -annotate +0-24 'NB' \
    -fill '#e05050' -draw 'circle 176,204 176,220' -fill '#e05050' -draw 'circle 128,216 128,232' \
    -fill '#d0d0d0' -draw 'rectangle 56,196 104,208' -draw 'rectangle 74,178 86,226' \
    -define icon:auto-resize=256,64,48,32,16 "$ICO" || { echo "não foi possível gerar o ícone (ImageMagick)"; exit 1; }
fi
convert "$ICO[0]" -resize 256x256 "$PKG/usr/share/icons/hicolor/256x256/apps/nativeboy-studio.png"
convert "$ICO[0]" -resize 48x48 "$PKG/usr/share/icons/hicolor/48x48/apps/nativeboy-studio.png"
cp "$PKG/usr/share/icons/hicolor/256x256/apps/nativeboy-studio.png" "$APP/assets/icon.png"
cp core/gb.h core/cpu_ops.h core/apu.h core/sgb.h core/sgb.cpp core/autoplay.h core/machine.cpp core/apu.cpp core/interp.cpp core/interp_gen.inc "$APP/sdk/core/"
cp common/*.h common/*.cpp "$APP/sdk/common/"
rm -rf "$APP/sdk/android" && cp -r android "$APP/sdk/android"
cp runtime/*.cpp "$APP/sdk/runtime/"
cp $D/imgui/imgui.h $D/imgui/imgui_internal.h $D/imgui/imconfig.h $D/imgui/imstb_*.h $D/imgui/imgui.cpp $D/imgui/imgui_draw.cpp \
   $D/imgui/imgui_tables.cpp $D/imgui/imgui_widgets.cpp "$APP/sdk/imgui/"
cp $D/imgui/backends/imgui_impl_sdl2.* $D/imgui/backends/imgui_impl_sdlrenderer2.* "$APP/sdk/imgui/backends/"
python3 tools/patch_imgui_linux.py "$APP/sdk/imgui/backends/imgui_impl_sdl2.cpp"
cp $D/stb/stb_image.h $D/stb/stb_image_write.h $D/stb/stb_vorbis.c "$APP/sdk/stb/"
# SDL2 for MinGW-w64: lets the Linux Studio also export Windows .exe games
SDLW=$D/SDL2-2.30.9/x86_64-w64-mingw32
mkdir -p "$APP/sdk/SDL2-win/include" "$APP/sdk/SDL2-win/lib" "$APP/sdk/SDL2-win/bin"
cp -r "$SDLW/include/SDL2" "$APP/sdk/SDL2-win/include/"
cp "$SDLW/lib/libSDL2.dll.a" "$APP/sdk/SDL2-win/lib/"
cp "$SDLW/bin/SDL2.dll" "$APP/sdk/SDL2-win/bin/"
echo "$VER-linux-$(cat core/*.cpp core/*.h common/*.cpp common/*.h runtime/*.cpp | md5sum | cut -c1-8)" > "$APP/sdk/VERSION"
cp $D/rgbds/rgbasm $D/rgbds/rgblink $D/rgbds/rgbfix $D/rgbds/rgbgfx "$APP/toolchain/rgbds/"
cp installer/LICENCA.txt "$APP/licencas/NativeBoy.txt"
cp data/LISTA-DE-JOGOS.txt "$APP/licencas/lista_de_jogos.txt"; cp data/CC-BY-SA-4.0.txt "$APP/licencas/"
cp $D/imgui/LICENSE.txt "$APP/licencas/imgui.txt"
cp installer/LEIA-ME-linux.txt "$APP/LEIA-ME.txt"
cp installer/LEIA-ME-linux.txt "$PKG/usr/share/doc/nativeboy-studio/LEIA-ME.txt"
cp installer/LICENCA.txt "$PKG/usr/share/doc/nativeboy-studio/copyright"
ln -s /opt/nativeboy-studio/NativeBoyStudio "$PKG/usr/bin/nativeboy-studio"
cat > "$PKG/usr/share/applications/nativeboy-studio.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=NativeBoy Studio
GenericName=Conversor de jogos de Game Boy
Comment=Jogue, descompile, modifique e converta jogos de Game Boy em programas nativos
Exec=/opt/nativeboy-studio/NativeBoyStudio %f
Icon=nativeboy-studio
Terminal=false
Categories=Game;Development;
MimeType=application/x-gameboy-rom;application/x-gameboy-color-rom;
Keywords=gameboy;gbc;rom;emulador;recompilador;
DESK
SIZE=$(du -sk "$PKG" | cut -f1)
cat > "$PKG/DEBIAN/control" <<CTL
Package: nativeboy-studio
Version: $VER
Section: games
Priority: optional
Architecture: amd64
Depends: libc6 (>= 2.39), libsdl2-2.0-0, libsdl2-dev, g++, zenity, xdg-utils, xdg-user-dirs, curl
Recommends: curl, g++-mingw-w64-x86-64-posix
Installed-Size: $SIZE
Maintainer: Adryan Andson da Silva <AdryanAndson@users.noreply.github.com>
Homepage: https://www.linkedin.com/in/adryan-andson
Description: NativeBoy Studio - converte jogos de Game Boy em programas nativos
 Jogue (emulação), descompile (assembly RGBDS, gráficos, música VGM), modifique
 (tiles, cores, mods, pacotes HD, cheats, ganchos C++) e converta ROMs de Game Boy
 e Game Boy Color em executáveis nativos de Linux x64 por recompilação estática
 SM83 -> C++, com verificação quadro a quadro contra o original.
CTL
cat > "$PKG/DEBIAN/postinst" <<'PI'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
exit 0
PI
cp "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/postrm"
chmod 755 "$PKG/DEBIAN/postinst" "$PKG/DEBIAN/postrm" "$APP/NativeBoyStudio" "$APP/toolchain/rgbds/"*
find "$PKG" -type d -exec chmod 755 {} +
dpkg-deb --build --root-owner-group -Zxz "$PKG" "$N/dist/linux/nativeboy-studio_${VER}_amd64.deb" >/dev/null
ls -la "$N/dist/linux/nativeboy-studio_${VER}_amd64.deb"

# ---- portable tar.gz (no installation; SDL2 bundled, only g++ needed to convert games)
TNAME=NativeBoyStudio-$VER-linux-x64
T=$N/dist/linux/$TNAME
rm -rf "$T"; cp -r "$APP" "$T"
mkdir -p "$T/lib" "$T/sdk/SDL2/include" "$T/sdk/SDL2/arch" "$T/sdk/SDL2/lib"
SDLSO=$(readlink -f /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0)
cp "$SDLSO" "$T/lib/libSDL2-2.0.so.0"
cp "$SDLSO" "$T/sdk/SDL2/lib/libSDL2-2.0.so.0"
cp -r /usr/include/SDL2 "$T/sdk/SDL2/include/"
cp -r /usr/include/x86_64-linux-gnu/SDL2 "$T/sdk/SDL2/arch/"
# SDL_syswm.h includes the X11 headers: bundle them, so converting games does not need libx11-dev installed
mkdir -p "$T/sdk/SDL2/arch/X11/extensions"
for h in Xlib.h X.h Xfuncproto.h Xosdefs.h Xatom.h Xutil.h keysym.h keysymdef.h Xmd.h Xproto.h Xprotostr.h; do
  [ -f /usr/include/X11/$h ] && cp /usr/include/X11/$h "$T/sdk/SDL2/arch/X11/"
done
cp "$PKG/usr/share/icons/hicolor/256x256/apps/nativeboy-studio.png" "$T/assets/icon.png"
cp installer/LEIA-ME-linux-portatil.txt "$T/LEIA-ME.txt"
cp installer/instalar.sh installer/desinstalar.sh "$T/"
chmod 755 "$T/instalar.sh" "$T/desinstalar.sh" "$T/NativeBoyStudio"
( cd "$N/dist/linux" && tar czf "$TNAME.tar.gz" "$TNAME" )
ls -la "$N/dist/linux/$TNAME.tar.gz"

# ---- AppImage (single file, runs on most distributions; g++ still needed to convert games)
AI=$N/dist/linux/AppDir
rm -rf "$AI"; cp -r "$T" "$AI"
rm -f "$AI/instalar.sh" "$AI/desinstalar.sh"
cat > "$AI/AppRun" <<'AR'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export NB_APPIMAGE=1
exec "$HERE/NativeBoyStudio" "$@"
AR
chmod 755 "$AI/AppRun"
cp "$PKG/usr/share/applications/nativeboy-studio.desktop" "$AI/nativeboy-studio.desktop"
sed -i 's#^Exec=.*#Exec=NativeBoyStudio %f#' "$AI/nativeboy-studio.desktop"
cp "$PKG/usr/share/icons/hicolor/256x256/apps/nativeboy-studio.png" "$AI/nativeboy-studio.png"
ln -sf nativeboy-studio.png "$AI/.DirIcon"
AIOUT=$N/dist/linux/NativeBoyStudio-$VER-x86_64.AppImage
rm -f "$AIOUT"
chmod +x "$D/appimagetool"
ARCH=x86_64 "$D/appimagetool" --appimage-extract-and-run --runtime-file "$D/appimage-runtime/runtime-x86_64" -n "$AI" "$AIOUT" > /tmp/appimage.log 2>&1 \
  && chmod 755 "$AIOUT" && ls -la "$AIOUT" || { echo "AVISO: AppImage não gerado"; tail -5 /tmp/appimage.log; }
rm -rf "$AI"
( cd "$N/dist/linux" && sha256sum *.deb *.tar.gz *.AppImage 2>/dev/null > SHA256SUMS )
