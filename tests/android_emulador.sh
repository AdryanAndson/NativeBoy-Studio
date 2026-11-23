#!/bin/bash
# Runs the test APK on the Android emulator: starts it standing up (drawn console), lying down (touch
# buttons), holds the A button on the screen and saves screenshots in telas/. Fails if the app closes.
APK=$1; PKG=$2
set -x
mkdir -p telas
adb install -r "$APK" || exit 1
adb logcat -c
adb shell am start -W -n "$PKG/org.nativeboy.jogo.JogoActivity"
sleep 20
adb shell wm size
adb exec-out screencap -p > telas/1_em_pe.png
adb shell pidof "$PKG" || { adb logcat -d | tail -200; echo "O APP FECHOU"; exit 1; }
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
wait
sleep 1
adb exec-out screencap -p > telas/4_soltou.png
adb shell pidof "$PKG" || { adb logcat -d | tail -200; echo "O APP FECHOU"; exit 1; }
# back button: the menu
adb shell input keyevent 4
sleep 2
adb exec-out screencap -p > telas/5_menu.png
adb logcat -d | grep -iE "SDL|nativeboy|FATAL|AndroidRuntime" | tail -80
ls -la telas
# small previews in the log too (base64), in case the artifact cannot be downloaded
for f in telas/*.png; do
  convert "$f" -resize 480x480 -quality 70 "${f%.png}.jpg" && echo "PREVIA $f $(base64 -w0 "${f%.png}.jpg")"
done
echo "TESTE ANDROID OK"
