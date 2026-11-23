#!/bin/sh
# Verifiable test suite for NativeBoy (host build). Produces tests/RESULTADOS.md
#   1. Reference interpreter: Blargg cpu_instrs (11 tests + combined) and instr_timing
#   2. Static recompiler: the same ROMs compiled to native code, lockstep-compared
#      with the interpreter every frame (registers, RAM, VRAM, OAM, framebuffer)
#   3. Real game (uCity, GPLv3): discovery -> native -> automatic fix iteration
#   4. Disassembly round trip: RGBDS re-assembles every ROM byte-for-byte
# usage: tests/run_all.sh <testroms dir> [rgbds dir]
set -u
N=$(cd "$(dirname "$0")/.." && pwd)
R=${1:-$N/../testroms}
RGBDS=${2:-$N/../deps/rgbds}
OUT=$N/tests/RESULTADOS.md
"$N/build_host.sh" > /tmp/nb_build_host.log 2>&1 || { echo "build failed"; tail -40 /tmp/nb_build_host.log; exit 1; }
pass=0; fail=0
ok() { echo "| $1 | OK | $2 |" >> "$OUT"; pass=$((pass+1)); }
ko() { echo "| $1 | FALHOU | $2 |" >> "$OUT"; fail=$((fail+1)); }
{
echo "# Resultados dos testes - NativeBoy"
echo
echo "Gerado em $(date '+%Y-%m-%d %H:%M') por tests/run_all.sh"
echo
echo "| Teste | Resultado | Detalhes |"
echo "|---|---|---|"
} > "$OUT"

# 1. interpreter
for f in "$R"/0*.gb "$R"/1*.gb "$R/cpu_instrs.gb" "$R/instr_timing.gb" "$R/mem_timing.gb" "$R/mem_timing2.gb" "$R/halt_bug.gb" "$R/interrupt_time.gb"; do
  [ -f "$f" ] || continue
  n=$(basename "$f")
  o=$("$N/build/host/nbtest" "$f" 4000 2>&1)
  if echo "$o" | grep -q "Passed" && ! echo "$o" | grep -q "RAMRESULT=[1-9]"; then ok "Interpretador: $n" "saída serial: Passed"; else ko "Interpretador: $n" "$(echo "$o" | tr '\n' ' ' | cut -c1-80)"; fi
done
# Super Game Boy (own test ROM): detection by the game, packets, palettes and border
S="$N/tests/roms/sgb_test.gb"
if [ -f "$S" ]; then
  o1=$("$N/build/host/nbtest" "$S" 120 --sgb 2>&1); o2=$("$N/build/host/nbtest" "$S" 120 2>&1)
  if echo "$o1" | head -1 | grep -qx "S" && echo "$o2" | head -1 | grep -qx "N"; then ok "Interpretador: Super Game Boy (sgb_test.gb)" "jogo detecta SGB só quando ligado; paletas e moldura aplicadas"
  else ko "Interpretador: Super Game Boy (sgb_test.gb)" "$(echo "$o1 / $o2" | tr '\n' ' ' | cut -c1-80)"; fi
fi
# Cartridge hardware and link cable (own test ROMs)
M7="$N/tests/roms/mbc7_test.gb"
if [ -f "$M7" ]; then
  o=$("$N/build/host/nbtest" "$M7" 20 2>&1 | head -1)
  if [ "$o" = "AOKEOK" ]; then ok "Interpretador: MBC7 (acelerômetro + EEPROM 93LC56)" "leitura do sensor e gravação/leitura da EEPROM corretas"
  else ko "Interpretador: MBC7 (acelerômetro + EEPROM 93LC56)" "saída: $o"; fi
fi
LT="$N/tests/roms/link_test.gb"
if [ -f "$LT" ] && [ -x "$N/build/host/linktest" ]; then
  o=$("$N/build/host/linktest" "$LT" 2>&1)
  if echo "$o" | grep -q "LINK OK"; then ok "Cabo link pela rede (TCP local, 2 Game Boys)" "$(echo "$o" | head -1)"
  else ko "Cabo link pela rede (TCP local, 2 Game Boys)" "$(echo "$o" | tr '\n' ' ' | cut -c1-80)"; fi
fi
# Link cable over the internet: invite code, players on the same network, game check, delay, router port (fake router)
if [ -x "$N/build/host/nettest" ]; then
  o=$("$N/build/host/nettest" 2>&1)
  if echo "$o" | grep -q "NET OK"; then ok "Cabo link: código de convite, rede local, jogo diferente e atraso" "$(echo "$o" | head -1 | cut -d' ' -f1)"
  else ko "Cabo link: código de convite, rede local, jogo diferente e atraso" "$(echo "$o" | tr '\n' ' ' | cut -c1-160)"; fi
  RP=28080; RL=$(mktemp)
  python3 "$N/tests/fake_router.py" $RP "$RL" & RPID=$!
  sleep 1
  o=$(NB_UPNP_DESC=http://127.0.0.1:$RP/desc.xml "$N/build/host/nettest" upnp 2>&1)
  kill $RPID 2>/dev/null; wait $RPID 2>/dev/null
  if echo "$o" | grep -q "upnp_estado=2 ip_externo=203.0.113.7" && grep -q "^AddPortMapping NewExternalPort=5738" "$RL" && grep -q "^DeletePortMapping" "$RL"; then
    ok "Cabo link: abrir a porta no roteador (UPnP, roteador simulado)" "porta aberta, endereço externo lido e porta fechada ao sair"
  else ko "Cabo link: abrir a porta no roteador (UPnP)" "$(echo "$o" | tr '\n' ' ') $(cat "$RL" | tr '\n' ' ')"; fi
  rm -f "$RL"
fi
# HD pack: pieces collected while playing, one sheet to redraw, imported back into the pack
if [ -x "$N/build/host/hdtest" ] && [ -f "$R/ucity.gbc" ]; then
  T=$(mktemp -d)
  o=$("$N/build/host/hdtest" "$R/ucity.gbc" "$T/hd" 2>&1)
  if echo "$o" | grep -q "HD OK"; then ok "Pacote HD: folha única, modelos suavizados e importação (µCity)" "$(echo "$o" | head -1 | cut -d' ' -f1-4) $(echo "$o" | head -1 | grep -o 'importados=[0-9]*')"
  else ko "Pacote HD: folha única, modelos suavizados e importação" "$(echo "$o" | tr '\n' ' ')"; fi
  rm -rf "$T"
fi
MT="$N/tests/roms/music_test.gb"
if [ -f "$MT" ] && [ -x "$N/build/host/musictest" ]; then
  T=$(mktemp -d)
  o=$("$N/build/host/musictest" "$MT" "$N/tests/roms/musica_test" "$T/a.wav" 2>&1)
  if echo "$o" | grep -q "MUSIC OK"; then ok "Trilha remasterizada (OGG no lugar da música) + gravação WAV" "troca na hora certa, mistura e WAV válidos"
  else ko "Trilha remasterizada (OGG no lugar da música) + gravação WAV" "$(echo "$o" | head -1 | cut -c1-80)"; fi
  rm -rf "$T"
fi
TX="$N/tests/roms/text_test.gb"
if [ -f "$TX" ] && [ -x "$N/build/host/translatetest" ]; then
  T=$(mktemp -d)
  "$N/build/host/translatetest" "$TX" "$T/tr.gb" "$T/tr2.gb" > "$T/log" 2>&1; rc=$?
  o=$("$N/build/host/nbtest" "$T/tr.gb" 5 2>&1 | head -1); o2=$("$N/build/host/nbtest" "$T/tr2.gb" 5 2>&1 | head -1)
  if [ $rc -eq 0 ] && [ "$o" = "OLA MUNDO/PRESS START TO PLAY" ]; then ok "Tradução: busca relativa, tabela, extração e reinserção (text_test.gb)" "texto trocado no jogo; texto grande demais recusado sem a opção de mover"
  else ko "Tradução: busca relativa, tabela, extração e reinserção (text_test.gb)" "saída: $o"; fi
  if [ $rc -eq 0 ] && grep -q "automatica: ok" "$T/log" && [ "$o2" = "OLA MUNDO/APERTE START PARA JOGAR AGORA" ]; then ok "Editor de textos: tabela automática e texto maior que o original (text_test.gb)" "tabela achada sem digitar palavra; texto maior movido e ponteiro atualizado: \"$o2\""
  else ko "Editor de textos: tabela automática e texto maior que o original" "saída: $o2 / $(grep -E 'automatica|nota|erro2' "$T/log" | tr '\n' ' ')"; fi
  rm -rf "$T"
fi
if [ -x "$N/build/host/dbgtest" ] && [ -f "$N/tests/roms/jt_test.sym" ]; then
  A=$(grep StateE "$N/tests/roms/jt_test.sym" | cut -d: -f2 | cut -d' ' -f1)
  o=$("$N/build/host/dbgtest" "$N/tests/roms/jt_test.gb" "$A" 2>&1)
  if echo "$o" | grep -q "DEBUG OK"; then ok "Depurador: ponto de parada, passo a passo e continuar" "para antes da instrução, avança 1 instrução por passo"
  else ko "Depurador: ponto de parada, passo a passo e continuar" "$(echo "$o" | head -1 | cut -c1-80)"; fi
fi
if [ -x "$N/build/host/rewindtest" ] && [ -f "$R/ucity.gbc" ]; then
  o=$("$N/build/host/rewindtest" "$R/ucity.gbc" 2>&1 | grep -E "state=|REWIND")
  if echo "$o" | grep -q "REWIND OK"; then ok "Voltar no tempo: estados restaurados exatos e jogo segue igual" "$(echo "$o" | head -1)"
  else ko "Voltar no tempo: estados restaurados exatos e jogo segue igual" "$(echo "$o" | head -1)"; fi
fi
if [ -x "$N/build/host/fxtest" ] && [ -f "$R/ucity.gbc" ]; then
  T=$(mktemp -d)
  "$N/build/host/fxtest" "$R/ucity.gbc" 600 xbr4 "$T/x.png" 1 50 1 >/dev/null 2>&1
  r=$(python3 -c "
from PIL import Image
im=Image.open('$T/x.png'); print('OK' if im.size==(640,576) and len(set(im.convert('RGB').tobytes()[i:i+3] for i in range(0,640*576*3,3)))>64 else 'FAIL', im.size)" 2>&1)
  case "$r" in OK*) ok "Gráficos modernos: xBR 4x + sombra + cores vivas + vinheta" "imagem 640x576 suavizada";; *) ko "Gráficos modernos: xBR 4x + sombra + cores vivas + vinheta" "$r";; esac
  rm -rf "$T"
fi
if [ -f "$R/ucity.gbc" ]; then
  T=$(mktemp -d)
  o=$(NB_EXPLORE=7200 "$N/build/host/nbcli" convert "$R/ucity.gbc" "$T" 14400 2>&1 | grep coverage)
  lin=$(echo "$o" | sed 's/.*coverage_linear=\([0-9]*\).*/\1/'); tot=$(echo "$o" | sed 's/.*coverage_total=\([0-9]*\).*/\1/')
  if [ -n "$lin" ] && [ "$tot" -gt "$lin" ]; then ok "Exploração automática acha código além do jogo automático (µCity)" "$lin -> $tot bytes de código executados"
  else ko "Exploração automática acha código além do jogo automático (µCity)" "$o"; fi
  rm -rf "$T"
fi
if [ -x "$N/build/host/rectest" ] && [ -f "$R/ucity.gbc" ]; then
  T=$(mktemp -d)
  "$N/build/host/rectest" "$R/ucity.gbc" "$T/a.gif" "$T/a.avi" >/dev/null 2>&1
  r=$(python3 - "$T" <<'PY'
import sys, struct, io
from PIL import Image
t = sys.argv[1]
im = Image.open(t + '/a.gif'); n = 1
try:
    while True: im.seek(im.tell() + 1); n += 1
except EOFError: pass
d = open(t + '/a.avi', 'rb').read()
p = d.find(b'00dc'); sz = struct.unpack('<I', d[p+4:p+8])[0]
jpg = Image.open(io.BytesIO(d[p+8:p+8+sz]))
ok = n == 150 and d[:4] == b'RIFF' and struct.unpack('<I', d[4:8])[0] == len(d) - 8 and jpg.size == (480, 432) and b'01wb' in d and b'idx1' in d
print(('OK' if ok else 'FAIL') + ' gif=%d quadros, avi=%d KB' % (n, len(d) // 1024))
PY
)
  case "$r" in OK*) ok "Gravação: GIF animado e vídeo AVI (MJPEG + som)" "${r#OK }";; *) ko "Gravação: GIF animado e vídeo AVI (MJPEG + som)" "$r";; esac
  rm -rf "$T"
fi
if [ -x "$N/build/host/widetest" ] && [ -f "$N/tests/roms/scroll_test.gb" ]; then
  o0=$("$N/build/host/widetest" "$N/tests/roms/scroll_test.gb" 0); o1=$("$N/build/host/widetest" "$N/tests/roms/scroll_test.gb" 1)
  if echo "$o1" | grep -q "(100.0%)"; then ok "Widescreen: memória do cenário (scroll_test.gb)" "lateral igual ao que passou pela tela: ${o1#*matching=} (sem memória: ${o0#*matching=})"
  else ko "Widescreen: memória do cenário (scroll_test.gb)" "$o1"; fi
fi
# Widescreen: diagonal scrolling where the game writes whole map rows (title screens flying over a map):
# each tile at the sides is shown only if the game wrote it for that very place
if [ -x "$N/build/host/diagtest" ] && [ -f "$N/tests/roms/diag_test.gb" ]; then
  d0=$("$N/build/host/diagtest" "$N/tests/roms/diag_test.gb" 0); d1=$("$N/build/host/diagtest" "$N/tests/roms/diag_test.gb" 1)
  d2=$("$N/build/host/diagtest" "$N/tests/roms/diag_test.gb" 1 rewind)
  if echo "$d1" | grep -q "(100.00%)" && echo "$d2" | grep -q "(100.00%)"; then ok "Widescreen: rolagem diagonal sem listras (diag_test.gb)" "laterais corretas: $(echo "$d1" | sed 's/.*(\(.*\)).*/\1/') (regra antiga por coluna: $(echo "$d0" | sed 's/.*(\(.*\)).*/\1/')); voltando no tempo: $(echo "$d2" | sed 's/.*(\(.*\)).*/\1/')"
  else ko "Widescreen: rolagem diagonal sem listras (diag_test.gb)" "$d1 / $d2"; fi
fi
# Extended view: ultrawide (beyond 256 columns) and taller picture (rows above/below) show what was really there
if [ -x "$N/build/host/exttest" ] && [ -f "$N/tests/roms/vscroll_test.gb" ]; then
  eh=$("$N/build/host/exttest" "$N/tests/roms/scroll_test.gb" 300 "1:0"); ev=$("$N/build/host/exttest" "$N/tests/roms/vscroll_test.gb" 300 "1:0")
  if echo "$eh" | grep -q "cols_match=6144/6144" && echo "$ev" | grep -q "rows_match=5120/5120" && echo "$ev" | grep -q "top_ok=1"; then
    ok "Ultrawide e expansão vertical (scroll_test.gb, vscroll_test.gb)" "além de 256 colunas: ${eh#*cols_match=}; linhas extras: ${ev%% cols*}"
  else ko "Ultrawide e expansão vertical" "h: $eh / v: $ev"; fi
fi
# Run-ahead: one frame less input lag, and the game itself (state, sound, scenery memory) exactly the same
if [ -x "$N/build/host/lagtest" ] && [ -f "$N/tests/roms/lag_test.gb" ]; then
  l1=$("$N/build/host/lagtest" "$N/tests/roms/lag_test.gb" "$N/tests/roms/scroll_test.gb")
  l2=""; [ -f "$R/ucity13.gbc" ] && l2=$("$N/build/host/lagtest" "$N/tests/roms/lag_test.gb" "$R/ucity13.gbc")
  if echo "$l1" | grep -q "sem=1 run_ahead_1=0 run_ahead_2=0" && echo "$l1" | grep -q "jogo_identico=1" && { [ -z "$l2" ] || echo "$l2" | grep -q "jogo_identico=1"; }; then
    ok "Menos atraso nos controles (run-ahead)" "atraso: 1 quadro sem, 0 com run-ahead; jogo, som e memória do cenário idênticos (scroll_test$([ -n "$l2" ] && echo ", µCity"))"
  else ko "Menos atraso nos controles (run-ahead)" "$(echo "$l1 $l2" | tr '\n' ' ')"; fi
fi
# Scenery memory, 2nd generation: objects outside the screen, look-ahead, memory kept between sessions.
# Everything shown beyond the original screen is checked against the test ROM's real world.
if [ -x "$N/build/host/memtest" ] && [ -f "$N/tests/roms/sprite_test.gb" ]; then
  o=$("$N/build/host/memtest" sprites "$N/tests/roms/sprite_test.gb")
  if [ $? -eq 0 ]; then ok "Personagens fora da tela original (sprite_test.gb)" "$o"; else ko "Personagens fora da tela original (sprite_test.gb)" "$o"; fi
  v=$("$N/build/host/memtest" verdade "$N/tests/roms/scroll_test.gb")
  s1=$(echo "$v" | awk '/^simples/{gsub(/[(%)]/,"",$4); print $4}'); s2=$(echo "$v" | awk '/^olhar/{gsub(/[(%)]/,"",$4); print $4}'); s3=$(echo "$v" | awk '/^memoria/{gsub(/[(%)]/,"",$4); print $4}')
  n1=$(echo "$v" | awk -F'[= ]' '/^simples/{print $3}'); n2=$(echo "$v" | awk -F'[= ]' '/^olhar/{print $3}')
  if [ "$s1" = "100.00" ] && [ "$s3" = "100.00" ] && awk "BEGIN{exit !($s2 >= 99.0 && $n2 > $n1 * 1.5)}"; then
    ok "Memória do cenário: olhar adiante e memória salva (scroll_test.gb)" "corretos fora da tela: simples ${s1}%, olhar adiante ${s2}% (mostra $((n2 * 100 / n1 - 100))% a mais), memória da sessão anterior ${s3}%"
  else ko "Memória do cenário: olhar adiante e memória salva" "$(echo "$v" | tr '\n' ' ')"; fi
fi
# Widescreen: visited places come back after leaving a scene (house) and returning
if [ -x "$N/build/host/reloctest" ] && [ -f "$N/tests/roms/reloc_test.gb" ]; then
  r0=$("$N/build/host/reloctest" "$N/tests/roms/reloc_test.gb" 0); r1=$("$N/build/host/reloctest" "$N/tests/roms/reloc_test.gb" 1)
  if echo "$r1" | grep -q "recognised=1" && echo "$r1" | grep -q "(100.0%)"; then ok "Widescreen: lembrar lugares já visitados (reloc_test.gb)" "ao sair da casa a lateral mostra a rua de novo: ${r1#*matching=} (sem: ${r0#*matching=})"
  else ko "Widescreen: lembrar lugares já visitados (reloc_test.gb)" "$r1"; fi
fi
# Smart widescreen: narrows on a "menu" with leftovers at the sides, widens again afterwards; never narrows while scrolling
if [ -x "$N/build/host/smarttest" ] && [ -f "$N/tests/roms/smart_test.gb" ]; then
  o=$("$N/build/host/smarttest" "$N/tests/roms/smart_test.gb" 560 1); os=$("$N/build/host/smarttest" "$N/tests/roms/scroll_test.gb" 300 1)
  a=$(echo "$o" | awk '$1==170{print $2}'); b=$(echo "$o" | awk '$1==300{print $2}'); c=$(echo "$o" | awk '$1==540{print $2}')
  n=$(echo "$os" | awk '$3==1' | wc -l)
  if [ "$a" = "1.00" ] && [ "$b" = "0.00" ] && [ "$c" = "1.00" ] && [ "$n" = "0" ]; then ok "Widescreen inteligente: estreita no menu e volta no jogo (smart_test.gb)" "largura: jogo 100% -> menu 0% -> jogo 100%; rolagem (scroll_test) nunca estreitou"
  else ko "Widescreen inteligente (smart_test.gb)" "jogo=$a menu=$b volta=$c estreitou_na_rolagem=$n"; fi
fi
# Widescreen: map columns the game left stale are hidden, fresh ones are shown
W="$N/tests/roms/wide_test.gb"
if [ -f "$W" ]; then
  T=$(mktemp -d)
  for e in 0 1; do for f in 120 300; do "$N/build/host/nbtest" "$W" $f --gray --wide $e --ppm "$T/w_${e}_$f.ppm" >/dev/null 2>&1; done; done
  r=$(python3 - "$T" <<'PY'
import sys
def px(p, x, y):
    d = open(p, 'rb').read(); hdr = d.split(b'\n', 1); w = int(hdr[0].split()[1]); b = hdr[1]
    return b[(y * w + x) * 3]
t = sys.argv[1]
ok = px(t+'/w_0_300.ppm', 10, 72) == 0 and px(t+'/w_1_300.ppm', 10, 72) == 255 and px(t+'/w_1_120.ppm', 10, 72) == 0 and px(t+'/w_1_300.ppm', 128, 72) in (0, 255)
print("OK" if ok else "FAIL")
PY
)
  if [ "$r" = OK ]; then ok "Widescreen: esconder colunas antigas do mapa (wide_test.gb)" "restos de outra tela escondidos; mapa atualizado continua visível"
  else ko "Widescreen: esconder colunas antigas do mapa (wide_test.gb)" "pixels de borda incorretos"; fi
  rm -rf "$T"
fi

# 2+3. recompiler lockstep
native_test() {  # rom discovery_frames verify_frames label expect_serial
  rom=$1; fd=$2; fv=$3; label=$4; exp=$5
  T=$(mktemp -d)
  "$N/build/host/nbcli" convert "$rom" "$T" "$fd" > "$T/conv.log" 2>&1 || { ko "$label" "conversão falhou"; return; }
  ( cd "$T/src" && for f in *.cpp; do g++ -O1 -std=c++17 -w -I"$N/core" -c "$f" -o "${f%.cpp}.o" & done; gcc -c rom_data.S -o rom_data.o; wait
    g++ -O2 -std=c++17 -w -o game *.o "$N/runtime/selftest.cpp" "$N/tools/selftest_main.cpp" "$N/build/host/libnbcore.a" -Wl,-z,noexecstack ) > "$T/build.log" 2>&1
  [ -x "$T/src/game" ] || { ko "$label" "compilação falhou"; return; }
  "$T/src/game" "$fv" "$T/st.json" > /dev/null 2>&1
  iter=0
  while grep -q '"missing": \["' "$T/st.json" && [ $iter -lt 3 ] && [ -z "${NT_NOFIX:-}" ]; do
    iter=$((iter+1))
    python3 -c "import json;print('\n'.join(json.load(open('$T/st.json'))['missing']))" > "$T/entries.txt"
    cat "$T/entries.txt" >> "$T/all_entries.txt"
    "$N/build/host/nbcli" convert "$rom" "$T" "$fd" "$T/all_entries.txt" > "$T/conv.log" 2>&1
    ( cd "$T/src" && rm -f *.o game && for f in *.cpp; do g++ -O1 -std=c++17 -w -I"$N/core" -c "$f" -o "${f%.cpp}.o" & done; gcc -c rom_data.S -o rom_data.o; wait
      g++ -O2 -std=c++17 -w -o game *.o "$N/runtime/selftest.cpp" "$N/tools/selftest_main.cpp" "$N/build/host/libnbcore.a" -Wl,-z,noexecstack ) > "$T/build.log" 2>&1
    "$T/src/game" "$fv" "$T/st.json" > /dev/null 2>&1
  done
  res=$(python3 - "$T/st.json" "$exp" <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); exp=sys.argv[2]
ok = d["identical"] and d["fallback_rom_instr"]==0 and d["fallback_ram_instr"]==0 and (exp=="" or exp in d["serial"])
print(("OK" if ok else "FAIL")+"|idêntico ao interpretador em %d quadros; fallback ROM=%d RAM=%d; instr. nativas=%d+%d RAM%s" % (
  d["frames"], d["fallback_rom_instr"], d["fallback_ram_instr"], d["native_instr"], d["native_ram_instr"], ("; serial contém '%s'"%exp) if exp else ""))
PY
)
  st=${res%%|*}; det=${res#*|}
  [ $iter -gt 0 ] && det="$det; $iter rodada(s) de correção automática"
  if [ "$st" = OK ]; then ok "$label" "$det"; else ko "$label" "$det"; fi
  rm -rf "$T"
}
for f in "$R"/0*.gb "$R"/1*.gb; do
  [ -f "$f" ] || continue
  native_test "$f" 2500 2500 "Nativo: $(basename "$f")" "Passed"
done
[ -f "$R/cpu_instrs.gb" ] && native_test "$R/cpu_instrs.gb" 4000 4000 "Nativo: cpu_instrs.gb (11 testes)" "Passed all tests"
[ -f "$R/instr_timing.gb" ] && native_test "$R/instr_timing.gb" 600 600 "Nativo: instr_timing.gb" "Passed"
[ -f "$N/tests/roms/sgb_test.gb" ] && { export NB_SGB=1; native_test "$N/tests/roms/sgb_test.gb" 120 300 "Nativo: Super Game Boy (sgb_test.gb, pacotes SGB)" "S"; unset NB_SGB; }
[ -f "$N/tests/roms/mbc7_test.gb" ] && native_test "$N/tests/roms/mbc7_test.gb" 20 60 "Nativo: MBC7 (mbc7_test.gb)" "AOKEOK"
[ -f "$N/tests/roms/jt2_test.gb" ] && NT_NOFIX=1 native_test "$N/tests/roms/jt2_test.gb" 0 120 "Nativo: tabela lida por HL (jp hl) sem descoberta (jt2_test.gb)" "ABCDEOK"
[ -f "$N/tests/roms/jt_test.gb" ] && NT_NOFIX=1 native_test "$N/tests/roms/jt_test.gb" 0 120 "Nativo: tabela de saltos sem descoberta (jt_test.gb, só análise estática)" "ABCDEOK"
[ -f "$R/mem_timing.gb" ] && native_test "$R/mem_timing.gb" 600 600 "Nativo: mem_timing.gb (temporização de memória por ciclo)" "Passed"
[ -f "$R/halt_bug.gb" ] && native_test "$R/halt_bug.gb" 600 600 "Nativo: halt_bug.gb" ""
[ -f "$R/dmg-acid2.gb" ] && native_test "$R/dmg-acid2.gb" 300 600 "Nativo: dmg-acid2.gb" ""
[ -f "$R/cgb-acid2.gbc" ] && native_test "$R/cgb-acid2.gbc" 300 600 "Nativo: cgb-acid2.gbc" ""
[ -f "$R/ucity.gbc" ] && native_test "$R/ucity.gbc" 1800 7200 "Nativo: uCity (jogo real, GBC, MBC5) - descoberta 30 s, verificação 2 min" ""

# 4. RGBDS round trip
if [ -x "$RGBDS/rgbasm" ]; then
  for f in "$R"/*.gb "$R"/*.gbc; do
    T=$(mktemp -d)
    "$N/build/host/nbcli" convert "$f" "$T" 1200 > /dev/null 2>&1
    ( cd "$T/disasm" && "$RGBDS/rgbasm" -o main.o main.asm && "$RGBDS/rgblink" -o out.gb main.o ) > "$T/rgb.log" 2>&1
    if cmp -s "$T/disasm/out.gb" "$f"; then ok "Desmontagem RGBDS: $(basename "$f")" "remontada idêntica byte a byte ($(stat -c %s "$f") bytes)"
    else ko "Desmontagem RGBDS: $(basename "$f")" "$(head -c 120 "$T/rgb.log" | tr '\n' ' ')"; fi
    rm -rf "$T"
  done
fi
# 4b. decompilation: symbol file names, jump table as "dw", pseudo-C, still byte-identical
if [ -x "$RGBDS/rgbasm" ] && [ -f "$N/tests/roms/jt_test.gb" ]; then
  T=$(mktemp -d)
  o=$("$N/build/host/nbcli" convert "$N/tests/roms/jt_test.gb" "$T" 0 2>&1)
  ( cd "$T/disasm" && "$RGBDS/rgbasm" -o main.o main.asm && "$RGBDS/rgblink" -o out.gb main.o ) > "$T/rgb.log" 2>&1
  if cmp -s "$T/disasm/out.gb" "$N/tests/roms/jt_test.gb" && echo "$o" | grep -q "symbols=14" && echo "$o" | grep -q "jump_tables=1" \
     && grep -q "dw StateE" "$T/disasm/bank_00.asm" && grep -q "ld \[wCounter\], a" "$T/disasm/bank_00.asm" && grep -q "case 4: goto StateE;" "$T/disasm/pseudocodigo.c"
  then ok "Descompilação: símbolos .sym + tabela de saltos + pseudocódigo C" "14 nomes importados, tabela de 6 destinos como dw, remontagem idêntica"
  else ko "Descompilação: símbolos .sym + tabela de saltos + pseudocódigo C" "$(head -c 120 "$T/rgb.log" | tr '\n' ' ')"; fi
  rm -rf "$T"
fi
# 4c. GUI smoke test (only when an X server can be started and the Linux Studio is built)
GB="$N/build/linux/NativeBoyStudio"
if command -v xvfb-run >/dev/null 2>&1 && command -v xdotool >/dev/null 2>&1 && [ -x "$GB" ] && [ -f "$R/dmg-acid2.gb" ]; then
  o=$(xvfb-run -a -s "-screen 0 1600x1000x24" "$N/tests/gui_test.sh" "$GB" "$R/dmg-acid2.gb" 2>&1 | grep "^GUI")
  case "$o" in "GUI OK"*) ok "Interface (Xvfb): abrir ROM, desenhar, teclado, fechar" "janela e projeto verificados";; *) ko "Interface (Xvfb): abrir ROM, desenhar, teclado, fechar" "$o";; esac
fi
# 4d. single-file executable: export with the Studio, run the one file (self-test, no window), portable mode
SDKR="$N/dist/linux/pkg/opt/nativeboy-studio"
if [ -x "$GB" ] && [ -d "$SDKR/sdk" ] && [ -f "$N/tests/roms/smart_test.gb" ]; then
  T=$(mktemp -d); mkdir -p "$T/home"
  HOME="$T/home" XDG_DATA_HOME= "$GB" --convert "$N/tests/roms/smart_test.gb" --frames 300 --verify 300 --export --projects "$T/proj" --games "$T/jogos" --sdk-root "$SDKR" > "$T/conv.log" 2>&1
  X="$T/jogos/Executável único/SmartTest.x86_64"
  [ -f "$X" ] || X=$(ls "$T/jogos/Executável único/"*.x86_64 2>/dev/null | head -1)
  mkdir -p "$T/pen"; cp "$X" "$T/pen/jogo.x86_64" 2>/dev/null
  HOME="$T/home" XDG_DATA_HOME= "$T/pen/jogo.x86_64" --selftest 300 "$T/st1.json" > /dev/null 2>&1
  mkdir -p "$T/pen/jogo - dados"
  HOME="$T/home" XDG_DATA_HOME= "$T/pen/jogo.x86_64" --selftest 300 "$T/st2.json" > /dev/null 2>&1
  ndir=$(ls "$T/home/.local/share/nativeboy/jogos" 2>/dev/null | wc -l)
  if grep -q '"identical": true' "$T/st1.json" 2>/dev/null && grep -q '"identical": true' "$T/st2.json" 2>/dev/null && [ "$ndir" = 1 ] && ls "$T/pen/jogo - dados" | grep -q config; then
    ok "Executável único: um arquivo só se instala e roda (normal e portátil)" "$(du -k "$X" | cut -f1) KB; dados na pasta do usuário e no modo pendrive; autoteste idêntico"
  else ko "Executável único" "$(grep -iE "único|erro" "$T/conv.log" | head -2 | tr '\n' ' ')"; fi
  # game profile: the ROM is recognised by its header title
  if [ -f "$R/ucity.gbc" ]; then
    PF=$(HOME="$T/home" XDG_DATA_HOME= "$GB" --convert "$R/ucity.gbc" --frames 1 --projects "$T/proj2" --games "$T/jogos2" --sdk-root "$SDKR" 2>&1 | grep -m1 "PERFIL:")
    if echo "$PF" | grep -q "µCity"; then ok "Perfil automático por jogo" "$PF"; else ko "Perfil automático por jogo" "perfil não detectado"; fi
  fi
  # every known Game Boy / Color game is recognised by the ROM's checksum (list shipped with the program)
  PP=$(ls -d "$N"/dist/linux/NativeBoyStudio-*-linux-x64 2>/dev/null | tail -1)
  if [ -f "$R/ucity13.gbc" ] && [ -x "$PP/NativeBoyStudio" ]; then
    RC=$(HOME="$T/home" XDG_DATA_HOME= "$PP/NativeBoyStudio" --convert "$R/ucity13.gbc" --frames 1 --projects "$T/proj3" --games "$T/jogos3" 2>&1 | grep -m1 "RECONHECIDO:")
    NJ=$(grep -c '^[0-9A-F]\{8\}|' "$PP/assets/jogos_gb.txt")
    if echo "$RC" | grep -q "uCity (World) (v1.3)"; then ok "Reconhecer jogos pelo checksum ($NJ jogos de GB/GBC na lista)" "${RC#RECONHECIDO: }"; else ko "Reconhecer jogos pelo checksum" "não reconheceu ucity13.gbc"; fi
  fi
  # the .deb made by the same export: valid package, installs to /opt, game runs from it
  DEB=$(ls "$T/jogos/Instaladores/"*.deb 2>/dev/null | head -1)
  if [ -n "$DEB" ] && command -v dpkg-deb >/dev/null; then
    mkdir -p "$T/raiz"; dpkg-deb -x "$DEB" "$T/raiz" 2>/dev/null
    G=$(ls -d "$T/raiz/opt/nativeboy-jogos/"* 2>/dev/null | head -1); EXE="$G/$(basename "$G")"
    "$EXE" --selftest 300 "$T/st3.json" > /dev/null 2>&1
    if dpkg-deb --info "$DEB" 2>/dev/null | grep -q "Package: nativeboy-" && [ -f "$G/config/instalado.ini" ] && ls "$T/raiz/usr/share/applications/"*.desktop >/dev/null 2>&1 && grep -q '"identical": true' "$T/st3.json" 2>/dev/null; then
      ok "Instalador .deb gerado pelo Studio" "$(du -k "$DEB" | cut -f1) KB; pacote válido (dpkg-deb), jogo em /opt + atalho no menu; autoteste idêntico"
    else ko "Instalador .deb" "$(dpkg-deb --info "$DEB" 2>&1 | head -2 | tr '\n' ' ')"; fi
  fi
  rm -rf "$T"
fi
# 5. PPU accuracy against hardware reference images (informative, not pass/fail)
MB=${MEALYBUG:-$N/../deps/mealybug}; MBE=${MEALYBUG_EXPECTED:-$N/../deps/mealybug-tearoom-tests/expected}
if [ -d "$MB" ] && [ -d "$MBE" ]; then
  echo "" >> "$OUT"; echo "Precisão de vídeo (Mealybug, imagens do hardware real, mudanças de registradores no meio da linha): $("$N/tests/mealybug.sh" "$MB" "$MBE" 2>/dev/null | tail -1 | sed 's/TOTAL: //')" >> "$OUT"
fi
{
echo
echo "**Total: $pass aprovados, $fail reprovados.**"
echo
echo "ROMs de teste: Blargg gb-test-roms (distribuição livre), dmg-acid2/cgb-acid2 (MIT, Matt Currie), uCity (GPLv3, Antonio Niño Díaz)."
} >> "$OUT"
cat "$OUT"
[ $fail -eq 0 ]
