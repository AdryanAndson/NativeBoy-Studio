#!/bin/sh
# Windows x64 build of NativeBoy Studio + SDK + installer (cross-compiled with MinGW-w64).
set -e
cd "$(dirname "$0")"
N=$(pwd)
if [ -n "${DEPS:-}" ]; then D=$DEPS; elif [ -d "$N/deps" ]; then D=$N/deps; else D=$(cd "$N/.." && pwd)/deps; fi
SDL=$D/SDL2-2.30.9/x86_64-w64-mingw32
OUT=$N/build/win
STAGE=$N/dist/NativeBoyStudio
CXX="x86_64-w64-mingw32-g++-posix"
INC="-I$SDL/include/SDL2 -I$D/imgui -I$D/imgui/backends -I$D/stb -I$N/core -I$N/common -DSDL_MAIN_HANDLED"
mkdir -p "$OUT/obj"

# 1. interpreter generated from the shared opcode table
g++ -O2 -o build/host_gen_interp tools/gen_interp.cpp core/optable.cpp
build/host_gen_interp core/interp_gen.inc

# 2. icon + version resource
if [ ! -f "$OUT/icon.ico" ]; then
  convert -size 256x256 xc:none -fill '#1b2a24' -draw 'roundrectangle 8,8 248,248 40,40' \
    -fill '#9bbc0f' -draw 'rectangle 48,40 208,160' -fill '#0f380f' -draw 'rectangle 64,56 192,144' \
    -fill '#8bac0f' -font DejaVu-Sans-Bold -pointsize 60 -gravity center -annotate +0-24 'NB' \
    -fill '#e05050' -draw 'circle 176,204 176,220' -fill '#e05050' -draw 'circle 128,216 128,232' \
    -fill '#d0d0d0' -draw 'rectangle 56,196 104,208' -draw 'rectangle 74,178 86,226' \
    -define icon:auto-resize=256,64,48,32,16 "$OUT/icon.ico"
fi
cat > "$OUT/studio.rc" <<RC
1 ICON "icon.ico"
1 VERSIONINFO
FILEVERSION 1,17,0,0
PRODUCTVERSION 1,17,0,0
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "041604B0"
    BEGIN
      VALUE "CompanyName", "NativeBoy"
      VALUE "FileDescription", "NativeBoy Studio"
      VALUE "FileVersion", "1.17.0"
      VALUE "ProductName", "NativeBoy Studio"
      VALUE "ProductVersion", "1.17.0"
      VALUE "OriginalFilename", "NativeBoyStudio.exe"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x416, 1200
  END
END
RC
(cd "$OUT" && x86_64-w64-mingw32-windres studio.rc -O coff -o obj/studio_res.o)

# 3. compile Studio
SRCS="studio/main.cpp studio/project.cpp studio/toolchain.cpp studio/translate.cpp studio/studio_extras.cpp studio/perfis.cpp studio/github_apk.cpp recomp/analysis.cpp recomp/codegen.cpp recomp/disasm.cpp
core/machine.cpp core/apu.cpp core/sgb.cpp core/interp.cpp core/optable.cpp common/platform.cpp common/settings.cpp common/video.cpp
common/ui_common.cpp common/player.cpp common/player_extras.cpp common/console.cpp common/touch.cpp common/music.cpp common/recorder.cpp common/link.cpp $D/imgui/imgui.cpp $D/imgui/imgui_draw.cpp $D/imgui/imgui_tables.cpp
$D/imgui/imgui_widgets.cpp $D/imgui/backends/imgui_impl_sdl2.cpp $D/imgui/backends/imgui_impl_sdlrenderer2.cpp"
pids=""
for f in $SRCS; do
  o="$OUT/obj/$(echo $f | sed 's#.*/##; s#\.cpp$##').o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ core/gb.h -nt "$o" ] || [ common/player.h -nt "$o" ] || [ studio/project.h -nt "$o" ]; then
    $CXX -O2 -std=c++17 -Wall -Wno-invalid-offsetof -Wno-misleading-indentation -Wno-unused-function -Wno-format-truncation $INC -c "$f" -o "$o" &
    pids="$pids $!"
  fi
done
fail=0; for p in $pids; do wait $p || fail=1; done
[ $fail = 0 ] || { echo "COMPILE FAILED"; exit 1; }
$CXX -o "$OUT/NativeBoyStudio.exe" "$OUT"/obj/*.o "$SDL/lib/libSDL2.dll.a" -static -static-libgcc -static-libstdc++ \
  -mwindows -lcomdlg32 -lole32 -lshell32 -lwininet -luuid -lws2_32
x86_64-w64-mingw32-strip "$OUT/NativeBoyStudio.exe"
echo "BUILD OK: $OUT/NativeBoyStudio.exe"

# 4. stage the distribution folder
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp "$OUT/NativeBoyStudio.exe" "$SDL/bin/SDL2.dll" "$STAGE/"
mkdir -p "$STAGE/assets/fonts" "$STAGE/sdk"
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf "$STAGE/assets/fonts/"
cp data/jogos_gb.txt "$STAGE/assets/"
cp "$OUT/icon.ico" "$STAGE/assets/"
mkdir -p "$STAGE/sdk/core" "$STAGE/sdk/common" "$STAGE/sdk/runtime" "$STAGE/sdk/imgui/backends" "$STAGE/sdk/stb" \
         "$STAGE/sdk/SDL2/include" "$STAGE/sdk/SDL2/lib" "$STAGE/sdk/SDL2/bin"
cp core/gb.h core/cpu_ops.h core/apu.h core/sgb.h core/sgb.cpp core/autoplay.h core/machine.cpp core/apu.cpp core/interp.cpp core/interp_gen.inc "$STAGE/sdk/core/"
cp common/*.h common/*.cpp "$STAGE/sdk/common/"
rm -rf "$STAGE/sdk/android" && cp -r android "$STAGE/sdk/android"
cp runtime/*.cpp "$STAGE/sdk/runtime/"
cp $D/imgui/imgui.h $D/imgui/imgui_internal.h $D/imgui/imconfig.h $D/imgui/imstb_*.h $D/imgui/imgui.cpp $D/imgui/imgui_draw.cpp \
   $D/imgui/imgui_tables.cpp $D/imgui/imgui_widgets.cpp $D/imgui/LICENSE.txt "$STAGE/sdk/imgui/"
cp $D/imgui/backends/imgui_impl_sdl2.* $D/imgui/backends/imgui_impl_sdlrenderer2.* "$STAGE/sdk/imgui/backends/"
cp $D/stb/stb_image.h $D/stb/stb_image_write.h $D/stb/stb_vorbis.c "$STAGE/sdk/stb/"
cp -r $SDL/include/SDL2 "$STAGE/sdk/SDL2/include/"
cp $SDL/lib/libSDL2.dll.a "$STAGE/sdk/SDL2/lib/"
cp $SDL/bin/SDL2.dll "$STAGE/sdk/SDL2/bin/"
echo "1.17.0-$(cat core/*.cpp core/*.h common/*.cpp common/*.h runtime/*.cpp | md5sum | cut -c1-8)" > "$STAGE/sdk/VERSION"
# toolchain: w64devkit (GCC) without the debugger, Fortran and editor
mkdir -p "$STAGE/toolchain"
cp -r $D/w64/w64devkit "$STAGE/toolchain/w64devkit"
( cd "$STAGE/toolchain/w64devkit" && rm -rf bin/gdb.exe bin/gdbserver.exe bin/*gfortran* bin/*.bat share/vim \
  libexec/gcc/x86_64-w64-mingw32/*/f951.exe lib/gcc/x86_64-w64-mingw32/*/libgfortran* src w64devkit.exe )
mkdir -p "$STAGE/toolchain/rgbds"
cp $D/rgbds-win/* "$STAGE/toolchain/rgbds/"
# licenses
mkdir -p "$STAGE/licencas"
cp $D/imgui/LICENSE.txt "$STAGE/licencas/imgui.txt"
cp $D/SDL2-2.30.9/LICENSE.txt "$STAGE/licencas/SDL2.txt" 2>/dev/null || true
cp "$STAGE/toolchain/w64devkit/COPYING.MinGW-w64-runtime.txt" "$STAGE/licencas/" 2>/dev/null || true
cp $D/w64/w64devkit/README.md "$STAGE/licencas/w64devkit_README.md" 2>/dev/null || true
cp installer/LICENCA.txt "$STAGE/licencas/NativeBoy.txt"
cp data/LISTA-DE-JOGOS.txt "$STAGE/licencas/lista_de_jogos.txt"; cp data/CC-BY-SA-4.0.txt "$STAGE/licencas/"
cp installer/LEIA-ME.txt "$STAGE/LEIA-ME.txt"
du -sh "$STAGE"
echo "STAGE OK: $STAGE"

# 5. installer
if [ "${1:-}" != "--no-installer" ]; then
  makensis -V2 -DSTAGE="$STAGE" -DOUTDIR="$N/dist" installer/nativeboy.nsi
  ls -la "$N/dist/NativeBoyStudio_Setup.exe"
  ( cd "$N/dist" && sha256sum NativeBoyStudio_Setup.exe > SHA256SUMS-windows.txt )
fi
