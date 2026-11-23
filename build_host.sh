#!/bin/sh
# Host (Linux) build of the core + developer tools, used by the automated tests.
set -e
cd "$(dirname "$0")"
CX="g++ -O2 -std=c++17 -Wall -Wno-invalid-offsetof -Wno-misleading-indentation"
mkdir -p build/host/core
$CX -o build/host/gen_interp tools/gen_interp.cpp core/optable.cpp
build/host/gen_interp core/interp_gen.inc
for s in machine apu interp optable sgb; do $CX -c core/$s.cpp -o build/host/core/$s.o & done; wait
rm -f build/host/libnbcore.a; ar rcs build/host/libnbcore.a build/host/core/*.o
$CX -o build/host/nbcli tools/nbcli.cpp recomp/*.cpp build/host/libnbcore.a
$CX -o build/host/nbtest tools/nbtest.cpp build/host/libnbcore.a
$CX -pthread -o build/host/linktest tools/linktest.cpp common/link.cpp build/host/libnbcore.a
$CX -I${DEPS:-../deps}/stb $(sdl2-config --cflags) -o build/host/musictest tools/musictest.cpp common/music.cpp common/platform.cpp common/settings.cpp build/host/libnbcore.a $(sdl2-config --libs)
$CX -o build/host/translatetest tools/translatetest.cpp studio/translate.cpp
$CX -o build/host/dbgtest tools/dbgtest.cpp build/host/libnbcore.a
$CX -o build/host/rewindtest tools/rewindtest.cpp build/host/libnbcore.a
$CX -I${DEPS:-../deps}/stb $(sdl2-config --cflags) -o build/host/fxtest tools/fxtest.cpp common/video.cpp common/platform.cpp common/settings.cpp build/host/libnbcore.a $(sdl2-config --libs)
$CX -I${DEPS:-../deps}/stb $(sdl2-config --cflags) -o build/host/rectest tools/rectest.cpp common/recorder.cpp common/video.cpp common/platform.cpp common/settings.cpp build/host/libnbcore.a $(sdl2-config --libs)
$CX -o build/host/widetest tools/widetest.cpp build/host/libnbcore.a
$CX -o build/host/smarttest tools/smarttest.cpp build/host/libnbcore.a
$CX -o build/host/reloctest tools/reloctest.cpp build/host/libnbcore.a
$CX -o build/host/exttest tools/exttest.cpp build/host/libnbcore.a
$CX -o build/host/memtest tools/memtest.cpp build/host/libnbcore.a
$CX -o build/host/lagtest tools/lagtest.cpp build/host/libnbcore.a
$CX -o build/host/diagtest tools/diagtest.cpp build/host/libnbcore.a
$CX -pthread -o build/host/nettest tools/nettest.cpp common/link.cpp build/host/libnbcore.a
$CX -I${DEPS:-../deps}/stb $(sdl2-config --cflags) -o build/host/hdtest tools/hdtest.cpp common/video.cpp common/platform.cpp common/settings.cpp build/host/libnbcore.a $(sdl2-config --libs)
$CX -I${DEPS:-../deps}/stb $(sdl2-config --cflags) -o build/host/apktest tools/apktest.cpp studio/github_apk.cpp common/platform.cpp common/settings.cpp $(sdl2-config --libs)
