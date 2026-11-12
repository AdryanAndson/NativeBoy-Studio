#!/usr/bin/env python3
"""Builds data/jogos_gb.txt (CRC32 -> game name, kind, genre, year, publisher, developer, players, series)
from libretro-database. Source: https://github.com/libretro/libretro-database, CC BY-SA 4.0.
Needed .dat files (metadat/<folder>/Nintendo - Game Boy[ Color].dat), saved as <folder>-<system>.dat:
  no-intro, genre, developer, publisher, releaseyear, maxusers, franchise
usage: gerar_jogos_gb.py <folder with the .dat files> <output>"""
import re, sys, os
GENRE_KIND = {
    "Platform": "plataforma", "Action": "plataforma", "Beat'em Up": "plataforma",
    "Role-playing (RPG)": "rpg",
    "Adventure": "tela_a_tela",
    "Shooter": "tiro", "Shoot'em Up": "tiro",
    "Puzzle": "tela_fixa", "Board": "tela_fixa", "Card": "tela_fixa", "Gambling": "tela_fixa", "Quiz": "tela_fixa",
    "Educational": "tela_fixa", "Casual Game": "tela_fixa", "Thinking": "tela_fixa", "Pinball": "tela_fixa", "Fighting": "tela_fixa",
    "Strategy": "mapa_livre", "Simulation": "mapa_livre",
}
GENRE_PT = {
    "Platform": "Plataforma", "Role-playing (RPG)": "RPG", "Action": "Ação", "Sports": "Esporte", "Puzzle": "Quebra-cabeça",
    "Racing": "Corrida", "Strategy": "Estratégia", "Adventure": "Aventura", "Board": "Tabuleiro", "Simulation": "Simulação",
    "Shooter": "Tiro", "Compilation": "Coletânea", "Shoot'em Up": "Tiro (nave)", "Gambling": "Cassino", "Fighting": "Luta",
    "Beat'em Up": "Briga de rua", "Card": "Cartas", "Hunting and Fishing": "Caça e pesca", "Educational": "Educativo",
    "Pinball": "Pinball", "Sports with Animals": "Esporte com animais", "Quiz": "Perguntas", "Music / Dancing": "Música e dança",
    "Various": "Variados", "Casual Game": "Casual", "Demo": "Demonstração", "Thinking": "Raciocínio",
}
FIELDS = {"genre": r'genre "([^"]*)"', "developer": r'developer "([^"]*)"', "publisher": r'publisher "([^"]*)"',
          "releaseyear": r'releaseyear "([^"]*)"', "maxusers": r'users (\d+)', "franchise": r'franchise "([^"]*)"'}
src, out = sys.argv[1], sys.argv[2]
meta = {k: {} for k in FIELDS}
names = {}
for f in sorted(os.listdir(src)):
    t = open(os.path.join(src, f), encoding="utf-8", errors="replace").read()
    folder = f.split("-", 1)[0]
    if folder == "no":     # no-intro
        for name, crc in re.findall(r'game \(\s*name "([^"]*)".*?crc ([0-9A-Fa-f]{8})', t, re.S):
            names.setdefault(crc.upper(), name)
    elif folder in FIELDS:
        for block in re.findall(r'game \((.*?)\n\)', t, re.S):
            v = re.search(FIELDS[folder], block); c = re.search(r'crc ([0-9A-Fa-f]{8})', block)
            if v and c: meta[folder].setdefault(c.group(1).upper(), v.group(1).strip())
clean = lambda s: re.sub(r"\s*\?{2,}", "", s.replace("|", "/")).strip()
# a field missing in one version (region, revision) is taken from another version of the same game
base = lambda n: n.split(" (", 1)[0].strip().lower()
by_base = {}
for crc, name in names.items(): by_base.setdefault(base(name), []).append(crc)
for k in FIELDS:
    for crc, name in names.items():
        if not meta[k].get(crc):
            for other in by_base[base(name)]:
                if meta[k].get(other): meta[k][crc] = meta[k][other]; break
rows = []
for crc, name in names.items():
    g = meta["genre"].get(crc, "")
    rows.append((name.lower(), crc, clean(name), GENRE_KIND.get(g, ""), GENRE_PT.get(g, clean(g)),
                 meta["releaseyear"].get(crc, ""), clean(meta["publisher"].get(crc, "")), clean(meta["developer"].get(crc, "")),
                 meta["maxusers"].get(crc, ""), clean(meta["franchise"].get(crc, ""))))
rows.sort()
with open(out, "w", encoding="utf-8") as o:
    o.write("# Jogos de Game Boy e Game Boy Color: CRC32 da ROM | nome | tipo | gênero | ano | editora | desenvolvedora | jogadores | série\n")
    o.write("# Dados derivados do libretro-database (https://github.com/libretro/libretro-database),\n")
    o.write("# listas No-Intro, gêneros, desenvolvedoras, editoras, anos, jogadores e séries.\n")
    o.write("# Licença: Creative Commons Atribuição-CompartilhaIgual 4.0 (CC BY-SA 4.0). Este arquivo também fica sob CC BY-SA 4.0.\n")
    o.write("# O tipo vem do gênero e é só um ponto de partida. Campos vazios: informação não encontrada na fonte.\n")
    for r in rows:
        o.write("|".join(r[1:]).rstrip("|") + "\n")
n = len(rows)
cnt = lambda i: sum(1 for r in rows if r[i])
print(f"{n} jogos; tipo {cnt(3)}, gênero {cnt(4)}, ano {cnt(5)}, editora {cnt(6)}, desenvolvedora {cnt(7)}, jogadores {cnt(8)}, série {cnt(9)}")
