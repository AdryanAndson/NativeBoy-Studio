#include "perfis.h"
#include "../common/platform.h"
#include <cstring>
#include <algorithm>

namespace {
struct Kind { const char* id; const char* name; const char* settings; };
// what each kind of game gets by default in the exported game
const Kind KINDS[] = {
    {"plataforma", "plataforma com rolagem lateral",
     "padrao_widescreen=1;padrao_ultrawide=1;padrao_linhas_extras=0;padrao_sem_limite_sprites=1;padrao_escala=nitida;padrao_fundo_laterais=desfocado"},
    {"mapa_livre", "mapa com rolagem para todos os lados",
     "padrao_widescreen=1;padrao_ultrawide=1;padrao_linhas_extras=16;padrao_escala=nitida;padrao_fundo_laterais=desfocado"},
    {"rpg", "RPG visto de cima",
     "padrao_widescreen=1;padrao_ultrawide=0;padrao_linhas_extras=0;padrao_escala=nitida;padrao_fundo_laterais=desfocado"},
    {"tela_a_tela", "aventura que muda de tela em tela",
     "padrao_widescreen=0;padrao_escala=nitida;padrao_fundo_laterais=moldura;padrao_moldura_cor=translucido;padrao_moldura_formato=portatil"},
    {"tela_fixa", "tela fixa (quebra-cabeça, arcade)",
     "padrao_widescreen=0;padrao_escala=nitida;padrao_fundo_laterais=moldura;padrao_moldura_cor=cinza;padrao_moldura_formato=portatil"},
    {"geral", "jogo reconhecido, ajustes gerais", ""},
    {"tiro", "jogo de tiro / ação com muitos sprites",
     "padrao_widescreen=1;padrao_ultrawide=0;padrao_linhas_extras=0;padrao_sem_limite_sprites=1;padrao_escala=nitida;padrao_fundo_laterais=desfocado"},
};
// Built-in list. Only the entries marked 1 were checked with the real game; the others follow what is
// publicly known about how each game scrolls, and can be adjusted in Modificar > Cores e melhorias.
const char* BUILTIN =
    "MICROCITY*|µCity|mapa_livre||0\n"
    "TETRIS*|Tetris|tela_fixa|paleta_padrao=cinza|0\n"
    "DR.MARIO*|Dr. Mario|tela_fixa|paleta_padrao=cor_pastel|0\n"
    "SUPER MARIOLAND|Super Mario Land|plataforma|paleta_padrao=cor_verde_vermelho|0\n"
    "MARIOLAND2*|Super Mario Land 2|plataforma|padrao_linhas_extras=16;paleta_padrao=cor_verde_vermelho|0\n"
    "SUPERMARIOLAND3*|Wario Land|plataforma|padrao_linhas_extras=16;paleta_padrao=cor_amarelo|0\n"
    "WARIOLAND2*|Wario Land II|plataforma|padrao_linhas_extras=16|0\n"
    "WARIOLAND3*|Wario Land 3|plataforma|padrao_linhas_extras=16|0\n"
    "KIRBY DREAM LAND*|Kirby's Dream Land|plataforma|paleta_padrao=cor_pastel|0\n"
    "KIRBY2*|Kirby's Dream Land 2|plataforma|paleta_padrao=cor_pastel|0\n"
    "METROID2*|Metroid II|mapa_livre|paleta_padrao=cor_azul_escuro|0\n"
    "DONKEY KONG|Donkey Kong|tela_fixa|paleta_padrao=cor_marrom|0\n"
    "DONKEYKONGLAND*|Donkey Kong Land|plataforma|paleta_padrao=cor_amarelo|0\n"
    "MEGAMAN*|Mega Man|tela_a_tela|paleta_padrao=cor_azul|0\n"
    "ZELDA*|The Legend of Zelda: Link's Awakening|tela_a_tela|paleta_padrao=cor_verde_vermelho|0\n"
    "POKEMON RED*|Pokémon Red|rpg|paleta_padrao=cor_vermelho|0\n"
    "POKEMON BLUE*|Pokémon Blue|rpg|paleta_padrao=cor_azul|0\n"
    "POKEMON GREEN*|Pokémon Green|rpg|paleta_padrao=cor_verde_vermelho|0\n"
    "POKEMON YELLOW*|Pokémon Yellow|rpg|paleta_padrao=cor_amarelo|0\n"
    "POKEMON_GLD*|Pokémon Gold|rpg||0\n"
    "POKEMON_SLV*|Pokémon Silver|rpg||0\n"
    "PM_CRYSTAL*|Pokémon Crystal|rpg||0\n"
    "GRADIUS*|Gradius / Nemesis|tiro||0\n"
    "NEMESIS*|Nemesis|tiro||0\n"
    "R-TYPE*|R-Type|tiro||0\n"
    "CASTLEVANIA*|Castlevania|plataforma||0\n"
    "DUCK TALES*|DuckTales|plataforma||0\n"
    "BOMBERMAN*|Bomberman|tela_fixa||0\n"
    "PAC-MAN*|Pac-Man|tela_fixa|paleta_padrao=cor_amarelo|0\n"
    "ALLEY WAY*|Alleyway|tela_fixa||0\n";

std::vector<GameProfile> parse(const std::string& text) {
    std::vector<GameProfile> out;
    for (auto& raw : split_lines(text)) {
        std::string l = trim(raw);
        if (l.empty() || l[0] == '#') continue;
        std::vector<std::string> f;
        size_t p = 0;
        while (true) { size_t e = l.find('|', p); f.push_back(trim(l.substr(p, e == std::string::npos ? e : e - p))); if (e == std::string::npos) break; p = e + 1; }
        if (f.size() < 3 || f[0].empty()) continue;
        GameProfile g; g.match = f[0]; g.name = f[1].empty() ? f[0] : f[1]; g.kind = f[2];
        if (f.size() > 3) g.extra = f[3];
        g.tested = f.size() > 4 && f[4] == "1";
        for (auto& c : g.match) c = (char)toupper((unsigned char)c);
        out.push_back(g);
    }
    return out;
}
void set_pairs(Settings& ini, const std::string& pairs) {
    size_t p = 0;
    while (p < pairs.size()) {
        size_t e = pairs.find(';', p); if (e == std::string::npos) e = pairs.size();
        std::string kv = pairs.substr(p, e - p); size_t eq = kv.find('=');
        if (eq != std::string::npos) ini.set(trim(kv.substr(0, eq)), trim(kv.substr(eq + 1)));
        p = e + 1;
    }
}
} // namespace
const GameProfile* find_profile_title(const std::string& header_title, const std::string& app_dir);

const char* profile_kind_name(const std::string& kind) {
    for (auto& k : KINDS) if (kind == k.id) return k.name;
    return "ajustes gerais";
}

namespace {
struct KnownGame { uint32_t crc; std::string name, kind; KnownGameInfo info; };
const std::vector<KnownGame>& known_games(const std::string& app_dir) {
    static std::vector<KnownGame> list;
    static std::string loaded_from = "\x01";
    if (loaded_from == app_dir) return list;
    loaded_from = app_dir; list.clear();
    std::string t;
    if (!plat::read_text(plat::join(app_dir, "assets/jogos_gb.txt"), t)) return list;
    for (auto& l : split_lines(t)) {
        if (l.size() < 10 || l[0] == '#' || l[8] != '|') continue;
        std::vector<std::string> f;     // crc|name|kind|genre|year|publisher|developer|players|series
        for (size_t p = 0;;) { size_t e = l.find('|', p); f.push_back(trim(l.substr(p, e == std::string::npos ? e : e - p))); if (e == std::string::npos) break; p = e + 1; }
        if (f.size() < 2) continue;
        f.resize(9);
        KnownGame g; g.crc = (uint32_t)strtoul(f[0].c_str(), nullptr, 16);
        g.name = f[1]; g.kind = f[2];
        g.info.name = f[1]; g.info.genre = f[3]; g.info.year = f[4]; g.info.publisher = f[5];
        g.info.developer = f[6]; g.info.players = f[7]; g.info.series = f[8];
        list.push_back(g);
    }
    std::sort(list.begin(), list.end(), [](const KnownGame& a, const KnownGame& b) { return a.crc < b.crc; });
    return list;
}
uint32_t crc32_of(const std::vector<uint8_t>& d) {
    static uint32_t t[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : d) c = t[(c ^ b) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
const KnownGame* find_known(const std::vector<uint8_t>& rom, const std::string& app_dir) {
    auto& l = known_games(app_dir);
    uint32_t c = crc32_of(rom);
    auto it = std::lower_bound(l.begin(), l.end(), c, [](const KnownGame& g, uint32_t v) { return g.crc < v; });
    return it != l.end() && it->crc == c ? &*it : nullptr;
}
} // namespace

std::string known_game_name(const std::vector<uint8_t>& rom, const std::string& app_dir) {
    const KnownGame* k = find_known(rom, app_dir);
    return k ? k->name : "";
}
size_t known_game_count(const std::string& app_dir) { return known_games(app_dir).size(); }
bool known_game_info(const std::vector<uint8_t>& rom, const std::string& app_dir, KnownGameInfo& out) {
    const KnownGame* k = find_known(rom, app_dir);
    if (!k) return false;
    out = k->info;
    return true;
}
void store_game_info(const KnownGameInfo& g, Settings& ini) {
    const std::pair<const char*, const std::string*> f[] = {{"nome_oficial", &g.name}, {"genero", &g.genre}, {"ano", &g.year},
        {"editora", &g.publisher}, {"desenvolvedora", &g.developer}, {"jogadores", &g.players}, {"serie", &g.series}};
    for (auto& x : f) if (!x.second->empty()) ini.set(x.first, *x.second);
}

namespace { std::vector<GameProfile> g_from_list; }

const GameProfile* find_profile(const std::string& header_title, const std::string& app_dir, const std::vector<uint8_t>* rom) {
    const GameProfile* byt = find_profile_title(header_title, app_dir);
    if (byt || !rom) return byt;
    const KnownGame* k = find_known(*rom, app_dir);
    if (!k) return nullptr;
    g_from_list.assign(1, GameProfile());
    GameProfile& g = g_from_list[0];
    g.match = header_title; g.name = k->name; g.kind = k->kind.empty() ? "geral" : k->kind; g.tested = false;
    return &g;
}

const GameProfile* find_profile_title(const std::string& header_title, const std::string& app_dir) {
    static std::vector<GameProfile> all;
    static std::string loaded_from = "\x01";
    if (loaded_from != app_dir) {
        loaded_from = app_dir;
        all.clear();
        std::string user;
        if (!app_dir.empty() && plat::read_text(plat::join(app_dir, "perfis_jogos.txt"), user)) all = parse(user);   // the user's entries win
        auto b = parse(BUILTIN);
        all.insert(all.end(), b.begin(), b.end());
    }
    std::string t = trim(header_title);
    for (auto& c : t) c = (char)toupper((unsigned char)c);
    if (t.empty()) return nullptr;
    for (auto& g : all) {
        if (!g.match.empty() && g.match.back() == '*') { if (t.compare(0, g.match.size() - 1, g.match, 0, g.match.size() - 1) == 0) return &g; }
        else if (t == g.match) return &g;
    }
    return nullptr;
}

std::string apply_profile(const GameProfile& p, Settings& ini) {
    for (auto& k : KINDS) if (p.kind == k.id) set_pairs(ini, k.settings);
    set_pairs(ini, p.extra);
    ini.set("perfil", p.name);
    ini.set("perfil_tipo", p.kind);
    ini.set("perfil_testado", p.tested ? "1" : "0");
    return p.name + " (" + profile_kind_name(p.kind) + ")";
}
