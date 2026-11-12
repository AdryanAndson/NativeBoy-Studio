#!/bin/sh
# Automated GUI smoke test (Linux, X11): starts the Studio with a ROM as if it had been dropped
# on the window, checks that the window appears, that the project is created, that the screen
# is actually drawn, and that closing the window ends the program cleanly.
# usage (needs an X server, e.g. xvfb-run):  tests/gui_test.sh path/to/NativeBoyStudio rom.gb
set -u
BIN=$1; ROM=$2
OUT=${OUT:-$(mktemp -d)}; mkdir -p "$OUT"
H=$(mktemp -d)
fail() { echo "GUI FALHOU: $1"; kill "$PID" 2>/dev/null; exit 1; }
HOME=$H XDG_CONFIG_HOME=$H/.config "$BIN" "$ROM" > "$OUT/studio.log" 2>&1 &
PID=$!
WID=$(timeout 20 xdotool search --sync --name "NativeBoy Studio" | head -1)
[ -n "$WID" ] || fail "a janela não apareceu"
sleep 3
kill -0 $PID 2>/dev/null || fail "o programa fechou sozinho"
import -window root "$OUT/tela_inicial.png" 2>/dev/null
P=$(find "$H" -name projeto.ini | head -1)
[ -n "$P" ] || fail "o projeto da ROM não foi criado"
python3 - "$OUT/tela_inicial.png" <<'PY' || fail "a tela está vazia"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("L").resize((160, 100))
px = list(im.tobytes())
sys.exit(0 if max(px) - min(px) > 40 and len(set(px)) > 20 else 1)
PY
# keyboard reaches the app: Escape closes the "what to do" dialog
xdotool windowactivate --sync "$WID" 2>/dev/null; xdotool key --window "$WID" Escape; sleep 1
kill -0 $PID 2>/dev/null || fail "o programa fechou após uma tecla"
import -window root "$OUT/tela_depois.png" 2>/dev/null
# a close request (SDL turns SIGTERM into the same "quit" event as closing the window) ends the program
kill -TERM $PID
for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 $PID 2>/dev/null || break; sleep 1; done
kill -0 $PID 2>/dev/null && fail "o programa não fechou ao fechar a janela"
wait $PID; RC=$?
[ $RC -eq 0 ] || fail "código de saída $RC"
echo "GUI OK: janela, projeto "$(basename "$(dirname "$P")")", desenho da tela e fechamento verificados. Capturas em $OUT"
