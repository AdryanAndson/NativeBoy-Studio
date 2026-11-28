#!/bin/sh
# Downloads every third-party dependency at a pinned version into $DEPS (default ./deps).
# Used by developers and by the GitHub Actions workflow.
set -e
DEPS=${DEPS:-$(pwd)/deps}
mkdir -p "$DEPS"; cd "$DEPS"
get() { [ -f "$2" ] || curl -fsSL -o "$2" "$1"; }
clone() { # url dir commit
  [ -d "$2" ] || { git clone -q "$1" "$2" && git -C "$2" checkout -q "$3"; }
}
clone https://github.com/ocornut/imgui.git imgui 144727fad6ded3055d388dadc4072c9c25ee1891   # docking branch (panels that dock)
clone https://github.com/nothings/stb.git stb 4e9b368bfc071e97206ea5c6adba68dd0cbf8897
get https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz SDL2-devel-2.30.9-mingw.tar.gz
[ -d SDL2-2.30.9 ] || tar xzf SDL2-devel-2.30.9-mingw.tar.gz
get https://github.com/gbdev/rgbds/releases/download/v0.8.0/rgbds-0.8.0-linux-x86_64.tar.xz rgbds-linux.tar.xz
[ -d rgbds ] || { mkdir rgbds && tar xJf rgbds-linux.tar.xz -C rgbds; }
get https://github.com/gbdev/rgbds/releases/download/v0.8.0/rgbds-0.8.0-win64.zip rgbds-win64.zip
[ -d rgbds-win ] || { mkdir rgbds-win && (cd rgbds-win && unzip -q ../rgbds-win64.zip); }
get https://github.com/skeeto/w64devkit/releases/download/v2.4.0/w64devkit-x64-2.4.0.7z.exe w64devkit-x64-2.4.0.7z.exe
[ -d w64/w64devkit ] || { mkdir -p w64 && (cd w64 && 7z x -y ../w64devkit-x64-2.4.0.7z.exe >/dev/null); }
get https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage appimagetool
chmod +x appimagetool
mkdir -p appimage-runtime && get https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 appimage-runtime/runtime-x86_64
# (runtime used for v1.2.0: sha256 617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46)
# test ROMs (free / open licences)
clone https://github.com/retrio/gb-test-roms.git gb-test-roms c240dd7d700e5c0b00a7bbba52b53e4ee67b5f15
clone https://github.com/mattcurrie/mealybug-tearoom-tests.git mealybug-tearoom-tests HEAD
mkdir -p testroms
cp gb-test-roms/cpu_instrs/individual/*.gb gb-test-roms/cpu_instrs/cpu_instrs.gb gb-test-roms/instr_timing/instr_timing.gb testroms/
get https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb testroms/dmg-acid2.gb
get https://github.com/mattcurrie/cgb-acid2/releases/download/v1.1/cgb-acid2.gbc testroms/cgb-acid2.gbc
get https://github.com/AntonioND/ucity/releases/download/v1.2/ucity.gbc testroms/ucity.gbc
echo "dependências prontas em $DEPS"
