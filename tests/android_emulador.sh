#!/bin/bash
# Runs the test APK on the Android emulator: starts it standing up (drawn console), lying down (touch
# buttons), holds the A button on the screen and saves screenshots in telas/. Fails if the app closes.
APK=$1; PKG=$2
mkdir -p telas
adb root >/dev/null 2>&1; sleep 2
adb install -r "$APK" || exit 1
adb logcat -c
adb logcat -v time > telas/logcat.txt 2>&1 &
LOGPID=$!
# when it fails: the game's steps, errors, and Android's stack traces of a "not responding" app
falhou() {
  sleep 2
  echo "=== passos do jogo / erros ==="
  grep -E "NativeBoy|SDL|AndroidRuntime|FATAL|DEBUG|libc |ANR in|boy\.jogo|JogoActivity|linker" telas/logcat.txt | grep -v "PREVIA" | tail -120
  echo "=== sistema (atividade e janelas) ==="
  P=$(grep -m1 -oE "I/NativeBoy\( *[0-9]+\)" telas/logcat.txt | grep -oE "[0-9]+")
  grep -E "ActivityTaskManager|ActivityManager|WindowManager|SDL|\( *${P:-0}\)" telas/logcat.txt | grep -vE "PREVIA|chatty" | head -150
  echo "=== rastros (ANR) ==="
  adb shell 'ls /data/anr/; for f in /data/anr/*; do echo "--- $f"; sed -n "1,400p" "$f"; done' 2>&1 | grep -vE "^\s*$" | grep -A60 -E "\"main\"|\"SDLThread\"|----- pid" | head -300
  adb exec-out screencap -p > telas/falha.png
  kill $LOGPID 2>/dev/null
  echo "O APP FECHOU ($1)"
  exit 1
}
set -x
adb shell am start -W -n "$PKG/org.nativeboy.jogo.JogoActivity"
sleep 20
adb shell wm size
adb exec-out screencap -p > telas/1_em_pe.png
adb shell pidof "$PKG" || falhou "ao abrir"
grep -E "NativeBoy" telas/logcat.txt
# lying down (the emulator is turned like a phone)
adb emu rotate
sleep 6
adb exec-out screencap -p > telas/2_deitado.png
# holds A on the screen buttons (landscape 2280x1080 on pixel_4: A is near the right edge, middle height)
read W H < <(adb shell wm size | sed -n 's/.*: \([0-9]*\)x\([0-9]*\).*/\2 \1/p')
M=$(( H < W ? H : W ))
AX=$(( W - M * 85 / 1000 * 3 / 2 - W * 2 / 100 )); AY=$(( H * 52 / 100 ))
adb shell input swipe $AX $AY $AX $AY 4000 &
sleep 2
adb exec-out screencap -p > telas/3_apertando_A.png
wait %2 2>/dev/null; sleep 3
adb exec-out screencap -p > telas/4_soltou.png
adb shell pidof "$PKG" || falhou "deitado"
# back button: the menu
adb shell input keyevent 4
sleep 2
adb exec-out screencap -p > telas/5_menu.png
adb shell pidof "$PKG" || falhou "no menu"
set +x
kill $LOGPID 2>/dev/null
grep -E "NativeBoy|AndroidRuntime|FATAL" telas/logcat.txt | tail -40
ls -la telas
# small previews in the log too (base64), in case the artifact cannot be downloaded
for f in telas/*.png; do
  convert "$f" -resize 480x480 -quality 70 "${f%.png}.jpg" && echo "PREVIA $f $(base64 -w0 "${f%.png}.jpg")"
done
echo "TESTE ANDROID OK"
