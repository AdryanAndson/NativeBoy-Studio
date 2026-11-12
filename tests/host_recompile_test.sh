#!/bin/sh
# Host verification: discovery + static recompilation + native/interpreter lockstep.
# usage: tests/host_recompile_test.sh rom frames_discovery frames_verify
set -e
N=$(cd "$(dirname "$0")/.." && pwd)
ROM="$1"; FD=${2:-3600}; FV=${3:-3600}; ENT="$4"
OUT=$(mktemp -d)
"$N/build/host/nbcli" convert "$ROM" "$OUT" "$FD" $ENT
cd "$OUT/src"
for f in *.cpp; do g++ -O1 -std=c++17 -w -I"$N/core" -c "$f" -o "${f%.cpp}.o" & done
gcc -c rom_data.S -o rom_data.o
wait
g++ -O2 -std=c++17 -w -o game_test *.o "$N/runtime/selftest.cpp" "$N/tools/selftest_main.cpp" "$N/build/host/libnbcore.a" -Wl,-z,noexecstack
./game_test "$FV" st.json || true
python3 -c "import json,sys;d=json.load(open('st.json'));m=d.pop('missing');print(json.dumps(d,indent=1));print('missing entries:',len(m));open('missing.txt','w').write(chr(10).join(m))"
echo "OUT=$OUT"
