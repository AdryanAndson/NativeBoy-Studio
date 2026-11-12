#include "project.h"
#include "perfis.h"
#include "../common/video.h"
#include "stb_image_write.h"
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);   // stb_image_write.cpp (video.cpp)
#include "../common/platform.h"
#include "../core/apu.h"
#include <memory>
#include "../recomp/codegen.h"
#include "../recomp/disasm.h"
#include <algorithm>
#include <ctime>
#include <cstring>
#include <set>

using namespace gb;

std::string fmt_num(unsigned long long v) {
    std::string s = std::to_string(v), r;
    int c = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) { r.insert(r.begin(), s[i]); if (++c % 3 == 0 && i > 0) r.insert(r.begin(), '.'); }
    return r;
}

std::string Project::sanitize(const std::string& s, bool for_exe) {
    std::string r;
    bool up = true;
    for (unsigned char c : s) {
        if (isalnum(c)) { r += for_exe && up ? (char)toupper(c) : (for_exe ? (char)tolower(c) : (char)c); up = false; }
        else if (c == ' ' || c == '_' || c == '-') { if (for_exe) up = true; else r += c == ' ' ? ' ' : (char)c; }
    }
    while (!r.empty() && r.back() == ' ') r.pop_back();
    if (r.empty()) r = for_exe ? "Jogo" : "Jogo";
    return r;
}

std::string Project::path(const std::string& rel) const { return plat::join(dir, rel); }
#ifdef _WIN32
static const char* EXE_SUFFIX = ".exe";
static const char* SDL_LIB_NAME = "SDL2.dll";
#else
static const char* EXE_SUFFIX = "";
static const char* SDL_LIB_NAME = "libSDL2-2.0.so.0";
#endif
std::string Project::exe_path() const { return path("build/" + exe_name + EXE_SUFFIX); }

bool Project::create_from_rom(const std::string& rom_path, const std::string& root, Project& p, std::string& err) {
    std::vector<u8> rom;
    if (!plat::read_file(rom_path, rom)) { err = "Não foi possível ler o arquivo:\n" + rom_path; return false; }
    RomInfo info;
    if (!parse_header(rom, info)) { err = "Esta ROM não pode ser usada: " + info.problem; return false; }
    if (!info.logo_ok) err = "Aviso: o cabeçalho da ROM não tem o logotipo padrão; ela pode estar corrompida ou ser um homebrew incomum.";
    std::string title = info.title.empty() ? plat::stem(rom_path) : info.title;
    // Pretty name: "MICROCITY" -> "Microcity"
    std::string pretty;
    bool first = true;
    for (char c : title) { pretty += first ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c); first = (c == ' '); }
    std::string folder = sanitize(pretty, false);
    std::string d = plat::join(root, folder);
    int n = 2;
    while (plat::dir_exists(d)) {
        // same ROM already imported? reuse that project
        std::vector<u8> old;
        Settings s; s.load(plat::join(d, "projeto.ini"));
        if (plat::read_file(plat::join(d, "rom/" + s.get("rom_original", "original.gb")), old) && old == rom)
            return p.open(d, err);
        d = plat::join(root, folder + " (" + std::to_string(n++) + ")");
    }
    for (auto sub : {"rom", "analise", "desmontagem", "graficos", "musica", "src", "build", "mods", "backups", "hd", "exportar"})
        if (!plat::make_dirs(plat::join(d, sub))) { err = "Não foi possível criar a pasta do projeto em\n" + d + "\nVerifique as permissões."; return false; }
    std::string ext = (info.cgb_supported ? ".gbc" : ".gb");
    plat::write_file(plat::join(d, "rom/original" + ext), rom.data(), rom.size());
    plat::write_file(plat::join(d, "rom/trabalho" + ext), rom.data(), rom.size());
    Settings ini;
    ini.path = plat::join(d, "projeto.ini");
    ini.set("nome", pretty);
    ini.set("executavel", sanitize(pretty, true));
    ini.set("rom_original", "original" + ext);
    ini.set("rom_trabalho", "trabalho" + ext);
    ini.set("origem", rom_path);
    ini.set("criado", plat::now_stamp());
    {   // a symbol file next to the ROM (same name, .sym) is imported automatically
        std::string sp = rom_path; size_t dot = sp.rfind('.');
        if (dot != std::string::npos) sp = sp.substr(0, dot);
        if (plat::file_exists(sp + ".sym")) plat::copy_file(sp + ".sym", plat::join(d, "analise/simbolos.sym"));
    }
    if (const GameProfile* gp = find_profile(info.title, plat::exe_dir(), &rom)) apply_profile(*gp, ini);
    { KnownGameInfo kg; if (known_game_info(rom, plat::exe_dir(), kg)) store_game_info(kg, ini); }   // official name, genre, year...
    ini.seti("etapa", ST_IMPORTED);
    ini.seti("quadros_descoberta", 3600);
    ini.seti("quadros_verificacao", 3600);
    ini.save();
    plat::write_text(plat::join(d, "LEIA-ME.txt"),
                     "Projeto do NativeBoy Studio\r\n\r\n"
                     "rom\\          ROM original (nunca é alterada) e ROM de trabalho (com suas modificações)\r\n"
                     "analise\\      relatório de compatibilidade, lista de funções, dados da descoberta de código\r\n"
                     "desmontagem\\  código-fonte assembly (RGBDS) que remonta a ROM idêntica\r\n"
                     "graficos\\     folhas de tiles recuperadas\r\n"
                     "musica\\       capturas de música em formato VGM\r\n"
                     "src\\          código C++ gerado pelo recompilador estático\r\n"
                     "build\\        executável compilado e registros de compilação\r\n"
                     "mods\\         seus ganchos C++ (hooks.cpp), cheats e configurações\r\n"
                     "hd\\           pacotes de gráficos em alta definição\r\n"
                     "backups\\      cópias automáticas para recuperação\r\n");
    return p.open(d, err);
}

bool Project::open(const std::string& d, std::string& err) {
    dir = d;
    ini = Settings();
    if (!ini.load(plat::join(d, "projeto.ini"))) { err = "Esta pasta não contém um projeto do NativeBoy Studio (projeto.ini não encontrado)."; return false; }
    name = ini.get("nome", plat::file_name(d));
    exe_name = ini.get("executavel", sanitize(name, true));
    std::string rp = path("rom/" + ini.get("rom_trabalho", "trabalho.gb"));
    if (!plat::read_file(rp, rom)) {
        // recovery: fall back to the original ROM
        if (!plat::read_file(path("rom/" + ini.get("rom_original", "original.gb")), rom)) { err = "A ROM do projeto está faltando em " + rp; return false; }
        plat::write_file(rp, rom.data(), rom.size());
    }
    if (!parse_header(rom, info)) { err = "A ROM do projeto é inválida: " + info.problem; return false; }
    an_valid = false;
    load_trace();
    load_verify();
    if (ini.get("genero_ok", "") != "1") {     // projects made before the game list had genre, year...: look it up once
        std::vector<u8> o; KnownGameInfo kg;
        if (plat::read_file(path("rom/" + ini.get("rom_original", "original.gb")), o) && known_game_info(o, plat::exe_dir(), kg)) store_game_info(kg, ini);
        ini.set("genero_ok", "1"); ini.save();
    }
    return true;
}

bool Project::save() { ini.set("modificado", plat::now_stamp()); return ini.save(); }

bool Project::rom_modified() const {
    std::vector<u8> o;
    plat::read_file(path("rom/" + ini.get("rom_original", "original.gb")), o);
    return o != rom;
}

void Project::backup(const std::string& reason) {
    std::string b = path("backups/" + plat::now_stamp());
    plat::make_dirs(b);
    plat::copy_file(path("projeto.ini"), plat::join(b, "projeto.ini"));
    plat::copy_file(path("rom/" + ini.get("rom_trabalho", "trabalho.gb")), plat::join(b, "rom_trabalho.bin"));
    if (plat::file_exists(path("analise/entradas.txt"))) plat::copy_file(path("analise/entradas.txt"), plat::join(b, "entradas.txt"));
    if (plat::file_exists(path("mods/hooks.cpp"))) plat::copy_file(path("mods/hooks.cpp"), plat::join(b, "hooks.cpp"));
    plat::write_text(plat::join(b, "motivo.txt"), reason);
    // keep the 20 most recent backups
    auto all = list_backups();
    for (size_t i = 20; i < all.size(); i++) plat::remove_tree(path("backups/" + all[i]));
}

std::vector<std::string> Project::list_backups() const {
    auto v = plat::list_dir(path("backups"), true);
    std::sort(v.rbegin(), v.rend());
    return v;
}

bool Project::restore_backup(const std::string& nm, std::string& err) {
    std::string b = path("backups/" + nm);
    if (!plat::dir_exists(b)) { err = "Backup não encontrado."; return false; }
    backup("antes de restaurar " + nm);
    std::vector<u8> r;
    if (plat::read_file(plat::join(b, "rom_trabalho.bin"), r)) { rom = r; plat::write_file(path("rom/" + ini.get("rom_trabalho", "trabalho.gb")), r.data(), r.size()); }
    if (plat::file_exists(plat::join(b, "entradas.txt"))) plat::copy_file(plat::join(b, "entradas.txt"), path("analise/entradas.txt"));
    if (plat::file_exists(plat::join(b, "hooks.cpp"))) plat::copy_file(plat::join(b, "hooks.cpp"), path("mods/hooks.cpp"));
    parse_header(rom, info);
    an_valid = false;
    ini.seti("etapa", std::min(step(), (int)ST_DISCOVERED));
    save();
    return true;
}

bool Project::write_working_rom(const std::vector<u8>& r, const std::string& reason) {
    backup(reason);
    rom = r;
    parse_header(rom, info);
    bool ok = plat::write_file(path("rom/" + ini.get("rom_trabalho", "trabalho.gb")), r.data(), r.size());
    if (step() > ST_DISCOVERED) ini.seti("etapa", ST_DISCOVERED);
    an_valid = false;
    save();
    return ok;
}

// ---------------------------------------------------------------- trace storage
bool Project::load_trace() {
    trace.clear(); ram.clear();
    std::vector<u8> t;
    if (plat::read_file(path("analise/descoberta.bin"), t)) trace = t;
    std::vector<u8> r;
    if (plat::read_file(path("analise/codigo_ram.bin"), r)) {
        size_t n = r.size() / 5;
        for (size_t i = 0; i < n; i++) {
            Machine::RamExec e;
            e.pc = (u16)(r[i * 5] | (r[i * 5 + 1] << 8));
            memcpy(e.bytes, &r[i * 5 + 2], 3);
            ram.push_back(e);
        }
    }
    return !trace.empty();
}
bool Project::save_trace() {
    std::vector<u8> r;
    for (auto& e : ram) { r.push_back(e.pc & 0xFF); r.push_back(e.pc >> 8); r.insert(r.end(), e.bytes, e.bytes + 3); }
    return plat::write_file(path("analise/descoberta.bin"), trace.data(), trace.size()) &&
           plat::write_file(path("analise/codigo_ram.bin"), r.data(), r.size());
}
void Project::merge_trace(const std::vector<u8>& t, const std::vector<Machine::RamExec>& r) {
    if (trace.size() != t.size()) { if (trace.empty()) trace.assign(t.size(), 0); }
    for (size_t i = 0; i < t.size() && i < trace.size(); i++) trace[i] |= t[i];
    std::set<unsigned long long> seen;
    auto key = [](const Machine::RamExec& e) { return ((unsigned long long)e.pc << 24) | ((unsigned long long)e.bytes[0] << 16) | (e.bytes[1] << 8) | e.bytes[2]; };
    for (auto& e : ram) seen.insert(key(e));
    for (auto& e : r) if (seen.insert(key(e)).second) ram.push_back(e);
    save_trace();
}

int Project::add_entries(const std::vector<std::string>& lines) {
    std::string cur;
    plat::read_text(path("analise/entradas.txt"), cur);
    std::set<std::string> have;
    for (auto& l : split_lines(cur)) if (!trim(l).empty()) have.insert(trim(l));
    int added = 0;
    for (auto& l : lines) {
        unsigned bk, ad;
        if (sscanf(l.c_str(), " %x:%x", &bk, &ad) != 2 || ad >= 0x8000) continue;
        char b[16]; snprintf(b, sizeof b, "%02X:%04X", bk, ad);
        if (have.insert(b).second) { cur += std::string(b) + "\n"; added++; }
    }
    if (added) { plat::write_text(path("analise/entradas.txt"), cur); an_valid = false; }
    return added;
}

int Project::import_missing(const std::string& file, std::string& err) {
    std::string t;
    if (!plat::read_text(file, t)) { err = "Não foi possível ler " + file; return -1; }
    std::vector<std::string> lines;
    for (auto& l : split_lines(t)) if (!l.empty() && l[0] != '#') lines.push_back(l);
    return add_entries(lines);
}

// ---------------------------------------------------------------- steps
static void tiles_sheet(const u8* data, int ntiles, const u32 pal[4], std::vector<u32>& img, int& w, int& h) {
    int cols = 16, rows = (ntiles + cols - 1) / cols;
    w = cols * 8; h = rows * 8;
    img.assign((size_t)w * h, 0xFF000000);
    for (int t = 0; t < ntiles; t++) {
        int tx = (t % cols) * 8, ty = (t / cols) * 8;
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int ci = (((data[t * 16 + y * 2 + 1] >> (7 - x)) & 1) << 1) | ((data[t * 16 + y * 2] >> (7 - x)) & 1);
                img[(size_t)(ty + y) * w + tx + x] = pal[ci];
            }
    }
}

bool Project::step_discover(int frames, BuildLog& log, std::string& err) {
    if (log.line) log.line("Executando o jogo em modo de análise por " + std::to_string(frames) + " quadros (" + std::to_string(frames / 60) + " s de jogo) com entrada automática...");
    nb::DiscoveryResult d;
    // automatic exploration: as many extra frames as the linear session, played from saved moments
    int explore = ini.getb("explorar", true) ? frames : 0;
    int total = frames + explore;
    if (explore && log.line) log.line("Exploração automática ligada: mais " + std::to_string(explore / 60) + " s de jogo a partir de momentos salvos, com jeitos diferentes de jogar.");
    bool ok = nb::run_discovery(rom, frames, d, err, [&](int f) {
        if (log.progress) log.progress((float)f / total);
        return !(log.cancel && *log.cancel);
    }, ini.getb("sgb", true), explore);
    if (!ok) return false;
    merge_trace(d.trace, d.ram);
    int n = 0;
    for (u8 v : d.trace) n += v;
    if (log.line) log.line("Instruções executadas encontradas: " + fmt_num(n) + "; instruções executadas a partir da RAM: " + fmt_num(d.ram.size()));
    if (explore && log.line && d.code_bytes_total > d.code_bytes_linear)
        log.line("A exploração encontrou " + fmt_num((int)(d.code_bytes_total - d.code_bytes_linear)) + " trechos de código a mais (" +
                 std::to_string(d.explore_useful) + " de " + std::to_string(d.explore_runs) + " tentativas acharam algo novo).");
    // graphics snapshots from VRAM
    static const u32 shades[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};
    int si = 0;
    for (auto& s : d.vram_snapshots) {
        for (int bank = 0; bank < (s[0x4082] ? 2 : 1); bank++) {
            std::vector<u32> img; int w, h;
            tiles_sheet(&s[bank * 0x2000], 384, shades, img, w, h);
            char nm[64]; snprintf(nm, sizeof nm, "graficos/vram_%02d_banco%d.png", si, bank);
            save_png(path(nm), img.data(), w, h);
        }
        si++;
    }
    if (log.line) log.line("Folhas de gráficos capturadas da VRAM: " + std::to_string(si));
    if (!d.vgm.empty()) {
        plat::write_file(path("musica/captura_descoberta.vgm"), d.vgm.data(), d.vgm.size());
        if (log.line) log.line("Música capturada: musica/captura_descoberta.vgm (abre em players VGM, ex.: foobar2000 + plugin VGM)");
    }
    ini.seti("quadros_descobertos", ini.geti("quadros_descobertos", 0) + frames);
    if (step() < ST_DISCOVERED) ini.seti("etapa", ST_DISCOVERED);
    save();
    return true;
}

bool Project::step_analyze(BuildLog& log, std::string& err) {
    an = nb::Analysis();
    an.rom = rom;
    an.rom.resize(((rom.size() + 0x3FFF) / 0x4000) * 0x4000, 0xFF);
    std::string ent;
    if (plat::read_text(path("analise/entradas.txt"), ent))
        for (auto& l : split_lines(ent)) { unsigned bk, ad; if (sscanf(l.c_str(), " %x:%x", &bk, &ad) == 2) an.extra_entries.insert(nb::Analysis::off_of(bk, ad)); }
    std::string symtxt;
    if (plat::read_text(path("analise/simbolos.sym"), symtxt)) {
        int ns = nb::load_sym_file(symtxt, an.rom.size(), an.sym);
        if (log.line) log.line("Arquivo de símbolos: " + std::to_string(ns) + " nomes importados (" + std::to_string(an.sym.rom.size()) + " rótulos de código, " + std::to_string(an.sym.ram.size()) + " variáveis).");
    }
    std::vector<u8> tr = trace;
    if (tr.size() != an.rom.size()) tr.resize(an.rom.size(), 0);
    if (log.line) log.line("Analisando o código (descida recursiva a partir dos vetores de entrada + rastros executados)...");
    nb::analyze(an, &tr, &ram);
    an_valid = true;
    if (log.progress) log.progress(0.4f);
    if (log.line) log.line("Instruções identificadas: " + fmt_num(an.n_instr) + " | funções: " + fmt_num(an.funcs.size()) +
                           " | saltos indiretos (jp hl): " + std::to_string(an.indirect.size()) + " | trechos de código em RAM: " + std::to_string(an.ram_code.size()));
    if (log.line && !an.jump_tables.empty()) {
        size_t ne = 0; for (auto& j : an.jump_tables) ne += j.targets.size();
        log.line("Tabelas de saltos encontradas: " + std::to_string(an.jump_tables.size()) + " (" + std::to_string(ne) + " destinos de código a mais).");
    }
    if (!nb::write_disassembly(an, path("desmontagem"), err)) return false;
    if (log.line) log.line("Desmontagem RGBDS gravada em desmontagem\\ (remonta a ROM idêntica).");
    if (!nb::write_pseudo_c(an, path("desmontagem/pseudocodigo.c"), err)) return false;
    if (log.line) log.line("Pseudocódigo C legível gravado em desmontagem\\pseudocodigo.c.");
    if (log.progress) log.progress(0.7f);
    // function list
    std::string csv = "nome;banco;endereco;instrucoes;executada_na_descoberta;rotina_de_som\n";
    for (auto& f : an.funcs) {
        char b[160];
        snprintf(b, sizeof b, "%s;%02X;%04X;%d;%s;%s\n", f.name.c_str(), f.bank, f.addr, f.n_instr, f.traced ? "sim" : "nao", f.sound ? "sim" : "nao");
        csv += b;
    }
    plat::write_text(path("analise/funcoes.csv"), csv);
    // ROM data sheets (2bpp view of each bank)
    static const u32 shades[4] = {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
    int banks = (int)(an.rom.size() / 0x4000);
    for (int b = 0; b < banks && b < 512; b++) {
        std::vector<u32> img; int w, h;
        tiles_sheet(&an.rom[(size_t)b * 0x4000], 1024, shades, img, w, h);
        // mark code tiles (mostly instructions) with a red tint so graphics stand out
        for (int t = 0; t < 1024; t++) {
            int code = 0;
            for (int i = 0; i < 16; i++) if (an.fl[(size_t)b * 0x4000 + t * 16 + i] & nb::AF_BODY) code++;
            if (code < 8) continue;
            int tx = (t % 16) * 8, ty = (t / 16) * 8;
            for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { u32& c = img[(size_t)(ty + y) * w + tx + x]; c = (c & 0xFFFF0000) | ((c & 0xFF00) >> 2 & 0xFF00) | ((c & 0xFF) >> 2); }
        }
        char nm[64]; snprintf(nm, sizeof nm, "graficos/rom_banco_%02X.png", b);
        save_png(path(nm), img.data(), w, h);
    }
    if (log.line) log.line("Folhas de tiles da ROM gravadas em graficos\\ (áreas de código aparecem avermelhadas).");
    Report r = make_report();
    std::string txt = "RELATÓRIO DE COMPATIBILIDADE - " + name + "\r\n" + r.title + "\r\n" + r.summary + "\r\n\r\n";
    for (auto& it : r.items) txt += std::string(it.level == 0 ? "[OK] " : it.level == 1 ? "[AVISO] " : it.level == 2 ? "[PROBLEMA] " : "[INFO] ") + it.text + "\r\n";
    plat::write_text(path("analise/relatorio.txt"), txt);
    if (step() < ST_ANALYZED) ini.seti("etapa", ST_ANALYZED);
    save();
    if (log.progress) log.progress(1.0f);
    return true;
}

bool Project::step_generate(BuildLog& log, std::string& err) {
    if (!an_valid && !step_analyze(log, err)) return false;
    nb::CodegenOptions opt;
    opt.game_name = name;
    opt.sgb = ini.getb("sgb", true) && info.sgb_flag == 0x03 && !info.cgb_supported;
    // user hooks
    std::string hooks;
    if (plat::read_text(path("mods/ganchos.txt"), hooks)) {
        for (auto& l : split_lines(hooks)) {
            unsigned bk, ad; char fn[128];
            if (sscanf(l.c_str(), " %x:%x %127s", &bk, &ad, fn) == 3) opt.hooks.push_back({nb::Analysis::off_of(bk, ad), fn});
        }
    }
    opt.hooks_file = plat::file_exists(path("mods/hooks.cpp"));
    // clean previous generated sources
    for (auto& f : plat::list_dir(path("src"), false)) plat::remove_file(path("src/" + f));
    nb::CodegenResult res;
    if (log.line) log.line("Gerando C++ a partir do código SM83...");
    if (!nb::generate_cpp(an, path("src"), opt, res, err)) return false;
    if (log.line) log.line("Gerados " + std::to_string(res.files.size()) + " arquivos: " + fmt_num(res.n_instr) + " instruções da ROM em " +
                           std::to_string(res.n_pages) + " blocos nativos, " + fmt_num(res.n_ram_instr) + " instruções de RAM protegidas por verificação.");
    ini.seti("instrucoes_nativas", res.n_instr);
    if (step() < ST_GENERATED) ini.seti("etapa", ST_GENERATED);
    save();
    return true;
}

static uint64_t file_hash(const std::string& p) {
    std::vector<u8> d;
    plat::read_file(p, d);
    uint64_t h = 1469598103934665603ULL;
    for (u8 c : d) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

bool Project::step_build(const Toolchain& tc, BuildLog& log, std::string& err) {
    last_build_error.clear();
    if (!tc.ok) { err = tc.problem; return false; }
    // the Linux Studio can also build a Windows version (MinGW-w64) into build-win/
    const std::string B = tc.cross_win ? "build-win" : "build";
    const std::string SUF = tc.exe_suffix();
    const std::string SDLN = tc.win_target() ? "SDL2.dll" : SDL_LIB_NAME;
    const std::string exe_out = path(B + "/" + exe_name + SUF);
    plat::make_dirs(path(B));
    plat::write_text(path(B + "/.compilando"), plat::now_stamp());
    std::vector<std::string> rt;
    std::string errors;
    if (!ensure_runtime_objects(tc, rt, log, errors)) { err = errors; last_build_error = errors; return false; }
    std::string obj = path(B + "/obj");
    plat::make_dirs(obj);
    std::vector<std::pair<std::string, std::string>> jobs;
    std::vector<std::string> objs;
    for (auto& f : plat::list_dir(path("src"), false)) {
        if (f.size() < 4 || f.substr(f.size() - 4) != ".cpp") continue;
        std::string src = path("src/" + f), o = plat::join(obj, f.substr(0, f.size() - 4) + ".o");
        objs.push_back(o);
        // (the SDK version is part of it: a newer Studio may change the machine's layout, so everything is rebuilt)
        char hs[96]; snprintf(hs, sizeof hs, "%016llx-%s", (unsigned long long)file_hash(src), plat::file_name(tc.cache_dir).c_str());
        std::string old;
        plat::read_text(o + ".hash", old);
        if (plat::file_exists(o) && old == hs) continue;     // unchanged since last build
        plat::write_text(o + ".hash", "");
        jobs.push_back({src, o});
    }
    bool hooks = plat::file_exists(path("mods/hooks.cpp"));
    if (hooks) { jobs.push_back({path("mods/hooks.cpp"), plat::join(obj, "hooks_usuario.o")}); objs.push_back(plat::join(obj, "hooks_usuario.o")); }
    if (log.line) log.line("Compilando código traduzido: " + std::to_string(jobs.size()) + " arquivo(s) alterado(s) de " + std::to_string(objs.size()) + "...");
    std::string flags = "-O1 -std=c++17 -w " + tc.include_flags() + " -I.";
    if (!compile_many(tc, jobs, flags, path("src"), log, errors)) {
        err = errors; last_build_error = errors;
        plat::write_text(path(B + "/compilacao.log"), errors);
        return false;
    }
    for (auto& j : jobs) {
        if (j.first.find("hooks.cpp") != std::string::npos) continue;
        char hs[96]; snprintf(hs, sizeof hs, "%016llx-%s", (unsigned long long)file_hash(j.first), plat::file_name(tc.cache_dir).c_str());
        plat::write_text(j.second + ".hash", hs);
    }
    // embedded ROM data
    std::string romo = plat::join(obj, "rom_data.o");
    plat::ProcResult r = plat::run(plat::quote(tc.gxx) + " -c rom_data.S -o ../" + B + "/obj/rom_data.o", path("src"), tc.env());
    if (r.code != 0) { err = "Falha ao incorporar os dados do jogo:\n" + r.output; last_build_error = err; return false; }
    objs.push_back(romo);
    // game icon (PNG for the window/menu, .ico resource inside the Windows .exe)
    {
        std::string ierr;
        if (!make_icon(ierr)) { if (log.line) log.line("Aviso: ícone não gerado (" + ierr + ")."); }
        else if (tc.win_target() && !tc.windres().empty()) {
            plat::copy_file(path("build/icone.ico"), path(B + "/obj/icone.ico"));
            // icon + version information (shown in the file's Properties and in Task Manager)
            std::string nm; for (char c : name) nm += (c == '"' || c == '\\') ? '\'' : c;
            std::string rc = "1 ICON \"icone.ico\"\n"
                             "1 VERSIONINFO\nFILEVERSION 1,0,0,0\nPRODUCTVERSION 1,0,0,0\nFILEOS 0x40004\nFILETYPE 0x1\nBEGIN\n"
                             " BLOCK \"StringFileInfo\"\n BEGIN\n  BLOCK \"041604B0\"\n  BEGIN\n"
                             "   VALUE \"FileDescription\", \"" + nm + "\"\n"
                             "   VALUE \"ProductName\", \"" + nm + "\"\n"
                             "   VALUE \"FileVersion\", \"1.0.0.0\"\n   VALUE \"ProductVersion\", \"1.0.0.0\"\n"
                             "   VALUE \"OriginalFilename\", \"" + exe_name + ".exe\"\n"
                             "   VALUE \"Comments\", \"Jogo nativo gerado pelo NativeBoy Studio\"\n"
                             "  END\n END\n BLOCK \"VarFileInfo\"\n BEGIN\n  VALUE \"Translation\", 0x416, 1200\n END\nEND\n";
            plat::write_file(path(B + "/obj/icone.rc"), rc.data(), rc.size());
            plat::ProcResult ir = plat::run(plat::quote(tc.windres()) + " icone.rc -O coff -o icone_res.o", path(B + "/obj"), tc.env());
            if (ir.code == 0) objs.push_back(plat::join(obj, "icone_res.o"));
            else if (log.line) log.line("Aviso: o ícone não pôde ser embutido no .exe (" + ir.output.substr(0, 200) + ").");
        }
    }
    // the runtime objects are copied next to the project objects so that the linker only
    // ever sees short relative paths (works with any user name / folder name)
    std::string rtdir = path(B + "/runtime");
    plat::make_dirs(rtdir);
    std::vector<std::string> rt_local;
    for (auto& o : rt) {
        std::string dst = plat::join(rtdir, plat::file_name(o));
        if (!plat::file_exists(dst) || plat::file_mtime(dst) < plat::file_mtime(o)) plat::copy_file(o, dst);
        rt_local.push_back(dst);
    }
    // link
    if (log.line) log.line("Gerando o executável " + exe_name + SUF + " (" + tc.platform_name() + ")...");
    std::string build_dir = path(B);
    auto rel = [&](const std::string& p) { return p.compare(0, build_dir.size(), build_dir) == 0 ? p.substr(build_dir.size() + 1) : plat::tool_path(p); };
    std::string rsp;
    for (auto& o : objs) rsp += plat::quote(rel(o)) + "\n";
    for (auto& o : rt_local) rsp += plat::quote(rel(o)) + "\n";
    std::string rsp_path = path(B + "/link.rsp");
    std::string rs2;   // gcc response files use forward slashes / escaped backslashes
    for (char c : rsp) rs2 += c == '\\' ? '/' : c;
    plat::write_text(rsp_path, rs2);
    std::string out = exe_out + ".part";
    r = plat::run(plat::quote(tc.gxx) + " -o " + plat::quote(exe_name + SUF + ".part") + " @link.rsp " + tc.link_libs(), build_dir, tc.env());
    if (r.code != 0) { err = "Falha ao gerar o executável (link):\n" + r.output; last_build_error = err; plat::write_text(path(B + "/compilacao.log"), err); return false; }
    if (!plat::move_file(out, exe_out)) { err = "Não foi possível gravar " + exe_out + ". O jogo está aberto? Feche-o e tente novamente."; return false; }
    if (!tc.sdl_dll().empty()) plat::copy_file(tc.sdl_dll(), path(B + "/" + SDLN));
    plat::set_executable(exe_out);
    plat::make_dirs(path(B + "/assets/fonts"));
    plat::copy_file(tc.font_file(), path(B + "/assets/fonts/DejaVuSans.ttf"));
    plat::remove_file(path(B + "/.compilando"));
    plat::write_text(path(B + "/compilacao.log"), "OK " + plat::now_stamp() + "\n");
    if (log.line) log.line("Compilação concluída: " + plat::join(B, exe_name + SUF));
    if (tc.cross_win) return true;     // the Windows build does not change the project stage
    if (step() < ST_BUILT) ini.seti("etapa", ST_BUILT);
    verify = VerifyResult();
    plat::remove_file(path("build/verificacao.json"));
    save();
    return true;
}

static std::string json_raw(const std::string& j, const std::string& key) {
    size_t p = j.find("\"" + key + "\"");
    if (p == std::string::npos) return "";
    p = j.find(':', p);
    if (p == std::string::npos) return "";
    p++;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\n')) p++;
    size_t e = p;
    if (j[p] == '"') { e = j.find('"', p + 1); return j.substr(p + 1, e - p - 1); }
    if (j[p] == '[') { e = j.find(']', p); return j.substr(p, e - p + 1); }
    while (e < j.size() && j[e] != ',' && j[e] != '\n' && j[e] != '}') e++;
    return j.substr(p, e - p);
}

bool Project::load_verify() {
    verify = VerifyResult();
    std::string j;
    if (!plat::read_text(path("build/verificacao.json"), j)) return false;
    verify.ran = true;
    verify.ok = json_raw(j, "ok") == "true";
    verify.identical = json_raw(j, "identical") == "true";
    verify.frames = atoi(json_raw(j, "frames").c_str());
    verify.diverged_at = atoi(json_raw(j, "diverged_at").c_str());
    verify.fb_rom = strtoull(json_raw(j, "fallback_rom_instr").c_str(), nullptr, 10);
    verify.fb_ram = strtoull(json_raw(j, "fallback_ram_instr").c_str(), nullptr, 10);
    verify.native_entries = strtoull(json_raw(j, "native_entries").c_str(), nullptr, 10);
    verify.native_instr = atoi(json_raw(j, "native_instr").c_str());
    verify.native_ram_instr = atoi(json_raw(j, "native_ram_instr").c_str());
    verify.serial = json_raw(j, "serial");
    std::string arr = json_raw(j, "missing");
    size_t p = 0;
    while ((p = arr.find('"', p)) != std::string::npos) {
        size_t e = arr.find('"', p + 1);
        if (e == std::string::npos) break;
        verify.missing.push_back(arr.substr(p + 1, e - p - 1));
        p = e + 1;
    }
    return true;
}

bool Project::step_verify(int frames, BuildLog& log, std::string& err) {
    if (!plat::file_exists(exe_path())) { err = "O executável ainda não foi compilado."; return false; }
    if (log.line) log.line("Verificando: executando " + std::to_string(frames) + " quadros em código nativo e no interpretador de referência, comparando o estado completo a cada quadro...");
    if (log.progress) log.progress(0.1f);
    std::string out = path("build/verificacao.json");
    plat::remove_file(out);
    plat::ProcResult r = plat::run(plat::quote(exe_path()) + " --selftest " + std::to_string(frames) + " verificacao.json", path("build"));
    if (!r.started) { err = "Não foi possível executar o jogo compilado para verificação."; return false; }
    if (!load_verify()) { err = "A verificação não produziu resultado (código de saída " + std::to_string(r.code) + ").\n" + r.output; return false; }
    if (log.progress) log.progress(1.0f);
    if (verify.identical) log.line("Resultado idêntico ao interpretador em todos os " + std::to_string(verify.frames) + " quadros.");
    else log.line("DIVERGÊNCIA no quadro " + std::to_string(verify.diverged_at) + ": o código nativo não se comportou como o original.");
    log.line("Execuções em código nativo: " + fmt_num(verify.native_entries) + " | instruções que precisaram do interpretador: ROM " +
             fmt_num(verify.fb_rom) + ", RAM " + fmt_num(verify.fb_ram));
    if (!verify.missing.empty()) log.line(std::to_string(verify.missing.size()) + " ponto(s) de código não traduzido(s) foram registrados.");
    ini.seti("etapa", std::max(step(), (int)ST_VERIFIED));
    save();
    return true;
}

bool Project::step_export(const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_dir) {
    const bool win = tc.win_target();
    const std::string SUF = tc.exe_suffix();
    const std::string exe_src = tc.cross_win ? path("build-win/" + exe_name + ".exe") : exe_path();
    if (!plat::file_exists(exe_src)) { err = "Compile o projeto antes de exportar."; return false; }
    out_dir = plat::join(games_root, tc.cross_win ? name + " (Windows)" : name);
    for (auto sub : {"", "assets", "assets/fonts", "assets/mods", "assets/hd", "config", "saves"})
        if (!plat::make_dirs(plat::join(out_dir, sub))) { err = "Não foi possível criar " + plat::join(out_dir, sub); return false; }
    std::string game_exe = plat::join(out_dir, exe_name + SUF);
    if (!plat::copy_file(exe_src, game_exe)) { err = "Não foi possível copiar o executável. Se o jogo exportado estiver aberto, feche-o."; return false; }
    plat::set_executable(game_exe);
    if (!tc.sdl_dll().empty()) plat::copy_file(tc.sdl_dll(), plat::join(out_dir, win ? std::string("SDL2.dll") : std::string(SDL_LIB_NAME)));
#ifndef _WIN32
    if (!win) {
    // Linux: launcher icon in the game folder and in the applications menu
    std::string desk = "[Desktop Entry]\nType=Application\nName=" + name + "\nComment=Jogo nativo gerado pelo NativeBoy Studio\n"
                       "Exec=\"" + game_exe + "\"\nPath=" + out_dir + "\nIcon=" +
                       (plat::file_exists(path("build/icone.png")) ? plat::join(out_dir, "assets/icone.png") : std::string("nativeboy-studio")) +
                       "\nTerminal=false\nCategories=Game;\nStartupWMClass=" + exe_name + "\n";
    std::string dfile = plat::join(out_dir, exe_name + ".desktop");
    plat::write_text(dfile, desk);
    plat::set_executable(dfile);
    if (ini.getb("menu_aplicativos", true)) {
        const char* home = getenv("HOME");
        if (home) {
            std::string apps = plat::join(home, ".local/share/applications");
            plat::make_dirs(apps);
            plat::write_text(plat::join(apps, "nativeboy-" + exe_name + ".desktop"), desk);
        }
    }
    }
#endif
    plat::copy_file(tc.font_file(), plat::join(out_dir, "assets/fonts/DejaVuSans.ttf"));
    for (auto& pk : plat::list_dir(path("hd"), true)) {     // the tiles and pack.ini (not the sheet nor the original models)
        const std::string src = path("hd/" + pk), dst = plat::join(out_dir, "assets/hd/" + pk);
        plat::make_dirs(dst);
        for (auto& f : plat::list_dir(src, false))
            if (f != "folha_hd.png" && f != "folha_hd.txt") plat::copy_file(plat::join(src, f), plat::join(dst, f));
    }
    for (auto& md : plat::list_dir(path("mods/pacotes"), true)) plat::copy_tree(path("mods/pacotes/" + md), plat::join(out_dir, "assets/mods/" + md));
    std::string cfgp = plat::join(out_dir, "config/settings.ini");
    {
        // creator's defaults; the player's own choices (if the game was already exported and played) are kept
        Settings s; s.load(cfgp); s.path = cfgp;
        bool fresh = s.kv.empty();
        auto def = [&](const char* k, const std::string& v) { if (fresh || ini.getb("forcar_padroes", false) || !s.kv.count(k)) s.set(k, v); };
        def("fidelity", "0"); def("vsync", "1"); def("fullscreen", "0"); def("escala", ini.get("padrao_escala", "inteira")); def("volume", "80");
        def("correcao_cor", "1"); def("janela", "4");
        // visual settings chosen in Modificar > Cores e melhorias always follow the project
        s.set("paleta", ini.get("paleta_padrao", "classico"));
        s.set("cores_personalizadas", ini.get("cores_personalizadas", ""));
        s.set("filtro", ini.get("padrao_filtro", "nitido"));
        s.set("widescreen", ini.get("padrao_widescreen", "0"));
        s.set("bordas_wide", ini.get("padrao_bordas_wide", "1"));
        s.set("memoria_cenario", ini.get("padrao_memoria_cenario", "1"));
        s.set("hud_largo", ini.get("padrao_hud_largo", "1"));
        s.set("wide_inteligente", ini.get("padrao_wide_inteligente", "1"));
        s.set("velocidade_jogo", ini.get("padrao_velocidade", "100"));
        s.set("ultrawide", ini.get("padrao_ultrawide", "0"));
        s.set("linhas_extras", ini.get("padrao_linhas_extras", "0"));
        s.set("moldura_formato", ini.get("padrao_moldura_formato", "portatil"));
        s.set("visual_console", ini.get("padrao_visual_console", "desligado"));
        if (!ini.get("padrao_console_cor", "").empty()) s.set("console_cor", ini.get("padrao_console_cor", ""));
        s.set("animacao_ligar", ini.get("padrao_animacao_ligar", "1"));
        s.set("fundo_laterais", ini.get("padrao_fundo_laterais", "desfocado"));
        s.set("moldura_cor", ini.get("padrao_moldura_cor", "cinza"));
        s.set("sem_limite_sprites", ini.get("padrao_sem_limite_sprites", "0"));
        s.set("rastro_lcd", ini.get("padrao_rastro_lcd", "0"));
        s.set("sombra_sprites", ini.get("padrao_sombra_sprites", "0"));
        s.set("vinheta", ini.get("padrao_vinheta", "0"));
        s.set("cores_vivas", ini.get("padrao_cores_vivas", "0"));
        s.set("brilho", ini.get("padrao_brilho", "0"));
        s.set("tela_inicial", ini.get("padrao_tela_inicial", "1"));
        s.set("sgb", ini.getb("sgb", true) && info.sgb_flag == 0x03 && !info.cgb_supported ? "1" : "0");
        s.set("sgb_borda", ini.get("padrao_sgb_borda", "1"));
        auto packs = plat::list_dir(path("hd"), true);
        if (!packs.empty() && ini.getb("hd_padrao", false)) s.set("pacote_hd", packs[0]);
        s.save();
    }
    if (plat::file_exists(path("mods/cheats.txt"))) plat::copy_file(path("mods/cheats.txt"), plat::join(out_dir, "config/cheats.txt"));
    if (plat::file_exists(path("mods/fundo.png"))) plat::copy_file(path("mods/fundo.png"), plat::join(out_dir, "assets/fundo.png"));
    if (plat::file_exists(path("build/icone.png"))) plat::copy_file(path("build/icone.png"), plat::join(out_dir, "assets/icone.png"));
    if (plat::file_exists(path("mods/conquistas.txt"))) plat::copy_file(path("mods/conquistas.txt"), plat::join(out_dir, "assets/conquistas.txt"));
    else plat::remove_file(plat::join(out_dir, "assets/conquistas.txt"));
    if (plat::file_exists(path("mods/abertura.png"))) plat::copy_file(path("mods/abertura.png"), plat::join(out_dir, "assets/abertura.png"));
    else plat::remove_file(plat::join(out_dir, "assets/abertura.png"));
    plat::remove_tree(plat::join(out_dir, "assets/musica"));
    if (plat::file_exists(path("mods/musica/musica.ini"))) plat::copy_tree(path("mods/musica"), plat::join(out_dir, "assets/musica"));
    else plat::remove_file(plat::join(out_dir, "assets/fundo.png"));
    std::string st = verify.ran ? (verify.identical && verify.fb_rom == 0 && verify.fb_ram == 0 ? "100% nativo na verificação" : "nativo com trechos via interpretador de compatibilidade") : "não verificado";
    plat::write_text(plat::join(out_dir, "LEIA-ME.txt"),
                     name + "\r\n\r\nExecutável nativo para " + tc.platform_name() + " gerado pelo NativeBoy Studio (" + st + ").\r\n"
                     + (win ? "Para jogar: dê dois cliques em " + exe_name + ".exe (Windows 10/11 64 bits). Não é necessário ter a ROM.\r\n\r\n"
                            : "Para jogar: dê dois cliques em " + exe_name + " (ou procure \"" + name + "\" no menu de aplicativos, seção Jogos).\r\n"
                              "Não é necessário ter a ROM.\r\n\r\n") +
                     "Esc ou F1: menu (vídeo, áudio, controles, mods, estados salvos)\r\n"
                     "F5/F8: salvar/carregar estado | F11: tela cheia | F12: captura de tela | Tab: acelerar | P: pausa\r\n\r\n"
                     "Pastas: assets\\ (fontes, mods, pacotes HD)  config\\ (configurações)  saves\\ (progresso)  capturas\\\r\n"
                     "Se aparecer config\\missing_code.txt, importe-o no NativeBoy Studio para tornar o jogo 100% nativo.\r\n\r\n"
                     "Distribuição: este executável contém o código e os dados do jogo original. Só o distribua se você tiver os direitos sobre o jogo.\r\n");
    {   // information for the Studio's game library
        Settings gi; gi.path = plat::join(out_dir, "config/jogo.ini");
        gi.set("nome", name); gi.set("executavel", exe_name + SUF); gi.set("plataforma", win ? "windows" : "linux");
        gi.set("titulo_rom", info.title); gi.set("gbc", info.cgb_supported ? "1" : "0"); gi.set("exportado", plat::now_stamp());
        {   // what the game list knows (looked up again, so projects made by older versions get it too)
            KnownGameInfo kg; Settings tmp;
            std::vector<u8> orig_rom; plat::read_file(path("rom/" + ini.get("rom_original", "original.gb")), orig_rom);
            if (known_game_info(orig_rom, plat::exe_dir(), kg)) { store_game_info(kg, tmp); store_game_info(kg, ini); }
            for (const char* k : {"nome_oficial", "genero", "ano", "editora", "desenvolvedora", "jogadores", "serie"}) {
                std::string v = ini.get(k, "");
                if (!v.empty()) gi.set(k, v);
            }
        }
        gi.set("projeto", dir);
        gi.save();
    }
    if (ini.getb("instalador", true)) {
        std::string inst, ierr;
        bool ok = tc.win_target() ? make_installer_windows(out_dir, games_root, tc, log, ierr, inst) : make_deb(out_dir, games_root, log, ierr, inst);
        if (ok) { ini.set(tc.win_target() ? "instalador_windows" : "instalador_linux", inst); if (log.line) log.line("Instalador: " + inst); }
        else if (log.line) log.line("Aviso: o instalador não foi gerado (" + ierr + ").");
    }
    if (ini.getb("exe_unico", true)) {
        std::string single, serr;
        if (make_single_exe(out_dir, games_root, tc, log, serr, single)) {
            ini.set(tc.cross_win ? "exe_unico_windows" : "exe_unico_arquivo", single);
            if (log.line) log.line("Executável único: " + single);
        } else if (log.line) log.line("Aviso: o executável único não foi gerado (" + serr + "). A pasta do jogo foi exportada normalmente.");
    }
    if (tc.cross_win) { ini.set("pasta_exportada_windows", out_dir); save(); if (log.line) log.line("Versão Windows exportada para " + out_dir); return true; }
    ini.set("pasta_exportada", out_dir);
    ini.seti("etapa", ST_EXPORTED);
    save();
    if (log.line) log.line("Exportado para " + out_dir);
    return true;
}

Report Project::make_report() const {
    Report r;
    const RomInfo& ri = info;
    r.items.push_back({ri.supported ? 0 : 2, "Cartucho: " + ri.mbc_name + (ri.supported ? " (suportado)" : " - " + ri.problem)});
    r.items.push_back({3, std::string("Sistema: ") + (ri.cgb_only ? "somente Game Boy Color" : ri.cgb_supported ? "Game Boy Color (compatível com GB)" : "Game Boy") +
                          ", " + std::to_string(ri.rom_banks) + " bancos de ROM (" + std::to_string(rom.size() / 1024) + " KB)"});
    if (ri.header_checksum != ri.header_checksum_calc) r.items.push_back({1, "Checksum do cabeçalho não confere: a ROM pode ter sido modificada."});
    int traced = 0;
    for (u8 v : trace) traced += v ? 1 : 0;
    if (trace.empty()) r.items.push_back({1, "A descoberta dinâmica de código ainda não foi executada."});
    else r.items.push_back({3, "Descoberta dinâmica: " + fmt_num(ini.geti("quadros_descobertos", 0)) + " quadros analisados, " + fmt_num(traced) + " instruções executadas."});
    if (an_valid) {
        r.items.push_back({0, "Código identificado: " + fmt_num(an.n_instr) + " instruções em " + fmt_num(an.funcs.size()) + " funções."});
        if (!an.indirect.empty())
            r.items.push_back({3, std::to_string(an.indirect.size()) + " salto(s) indireto(s) (jp hl): os destinos são resolvidos pelo despachante nativo; destinos nunca executados na descoberta usam o interpretador e são registrados."});
        if (!an.unresolved_far.empty())
            r.items.push_back({1, std::to_string(an.unresolved_far.size()) + " chamada(s) do banco 0 para bancos desconhecidos: cobertas apenas se executadas na descoberta."});
        if (!an.ram_code.empty())
            r.items.push_back({0, std::to_string(an.ram_code.size()) + " trecho(s) de código copiados para a RAM foram traduzidos com verificação de bytes."});
        if (!an.illegal.empty()) r.items.push_back({1, std::to_string(an.illegal.size()) + " opcode(s) ilegais encontrados (provavelmente dados interpretados como código)."});
        if (an.n_overlap) r.items.push_back({3, std::to_string(an.n_overlap) + " instrução(ões) sobrepostas (truques de código) - tratadas separadamente."});
    }
    if (!last_build_error.empty()) r.items.push_back({2, "A última compilação falhou. Veja os detalhes na aba Registro."});
    if (!verify.ran) {
        r.status = 3;
        r.title = "Ainda não verificado";
        r.summary = "Compile e verifique o projeto para saber se a tradução está completa.";
        if (!plat::file_exists(exe_path())) return r;
        return r;
    }
    if (!verify.identical) {
        r.status = 2;
        r.title = "FALHA: divergência detectada";
        r.summary = "O executável nativo deixou de reproduzir o comportamento original no quadro " + std::to_string(verify.diverged_at) +
                    ". Não use este executável. Envie o relatório para os desenvolvedores do NativeBoy.";
        r.items.push_back({2, "Divergência entre código nativo e interpretador de referência no quadro " + std::to_string(verify.diverged_at) + "."});
        return r;
    }
    r.items.push_back({0, "Verificação: estado idêntico ao interpretador de referência em " + fmt_num(verify.frames) + " quadros (CPU, RAM, VRAM, OAM e imagem)."});
    if (verify.fb_rom == 0 && verify.fb_ram == 0) {
        r.status = 0;
        r.title = "NATIVO COMPLETO (verificado)";
        r.summary = "Todo o código executado na verificação rodou como código x64 compilado, sem interpretar instruções SM83. "
                    "Partes do jogo não alcançadas nos testes continuam protegidas: se forem executadas e não estiverem traduzidas, "
                    "o jogo usa o interpretador de compatibilidade e registra o ponto para você corrigir.";
        r.items.push_back({0, "Nenhuma instrução precisou do interpretador durante a verificação."});
    } else {
        r.status = 1;
        r.title = "NATIVO PARCIAL - correção necessária";
        r.summary = "O jogo funciona, mas parte do código executado ainda passou pelo interpretador de compatibilidade (" +
                    fmt_num(verify.fb_rom + verify.fb_ram) + " instruções). Use \"Corrigir automaticamente\" para traduzir esses trechos.";
        if (verify.fb_rom) r.items.push_back({1, fmt_num(verify.fb_rom) + " instruções da ROM via interpretador (" + std::to_string(verify.missing.size()) + " pontos de entrada registrados)."});
        if (verify.fb_ram) r.items.push_back({1, fmt_num(verify.fb_ram) + " instruções na RAM via interpretador (código gerado em tempo de execução que mudou após a descoberta)."});
    }
    return r;
}

// ------------------------------------------------------------------ diagnostics package
static uint32_t crc32_of(const std::string& d) {
    static uint32_t t[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = t[(c ^ b) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool write_zip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    auto u16le = [](std::string& s, unsigned v) { s += (char)(v & 255); s += (char)((v >> 8) & 255); };
    auto u32le = [](std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (i * 8)) & 255); };
    for (auto& f : files) {
        uint32_t crc = crc32_of(f.second), off = (uint32_t)out.size(), sz = (uint32_t)f.second.size();
        u32le(out, 0x04034b50); u16le(out, 20); u16le(out, 0x0800 /* UTF-8 names */); u16le(out, 0);
        u16le(out, 0); u16le(out, 0x21); u32le(out, crc); u32le(out, sz); u32le(out, sz);
        u16le(out, (unsigned)f.first.size()); u16le(out, 0);
        out += f.first; out += f.second;
        u32le(central, 0x02014b50); u16le(central, 20); u16le(central, 20); u16le(central, 0x0800); u16le(central, 0);
        u16le(central, 0); u16le(central, 0x21); u32le(central, crc); u32le(central, sz); u32le(central, sz);
        u16le(central, (unsigned)f.first.size()); u16le(central, 0); u16le(central, 0); u16le(central, 0); u16le(central, 0);
        u32le(central, 0); u32le(central, off);
        central += f.first;
    }
    uint32_t cd_off = (uint32_t)out.size();
    out += central;
    u32le(out, 0x06054b50); u16le(out, 0); u16le(out, 0); u16le(out, (unsigned)files.size()); u16le(out, (unsigned)files.size());
    u32le(out, (uint32_t)central.size()); u32le(out, cd_off); u16le(out, 0);
    return plat::write_file(path, out.data(), out.size());
}

bool Project::write_diagnostics_zip(const std::string& out_zip, const std::string& system_info, std::string& err) const {
    std::vector<std::pair<std::string, std::string>> files;
    files.push_back({"sistema.txt", system_info});
    auto add = [&](const std::string& rel, const std::string& inside) {
        std::string t;
        if (plat::read_text(path(rel), t)) files.push_back({inside, t.size() > 4 * 1024 * 1024 ? t.substr(0, 4 * 1024 * 1024) + "\n[...cortado]" : t});
    };
    // project settings without the original path of the ROM on disk
    {
        std::string ini_txt;
        for (auto& kv : ini.kv) if (kv.first != "origem") ini_txt += kv.first + "=" + kv.second + "\n";
        files.push_back({"projeto.ini", ini_txt});
    }
    char hdr[512];
    snprintf(hdr, sizeof hdr, "titulo=%s\ncartucho=%s (0x%02X)\ngbc=%s\nsgb=%s\nbancos=%d\ntamanho=%zu\nchecksum_global=%04X\n",
             info.title.c_str(), info.mbc_name.c_str(), info.cart_type, info.cgb_supported ? "sim" : "nao", info.sgb_flag == 3 ? "sim" : "nao",
             info.rom_banks, rom.size(), info.global_checksum);
    files.push_back({"cartucho.txt", hdr});
    add("analise/relatorio.txt", "relatorio.txt");
    add("analise/funcoes.csv", "funcoes.csv");
    add("analise/entradas.txt", "entradas.txt");
    add("build/compilacao.log", "compilacao.log");
    add("build/verificacao.json", "verificacao.json");
    add("build-win/compilacao.log", "compilacao_windows.log");
    std::string exp = ini.get("pasta_exportada", "");
    if (!exp.empty()) {
        std::string t;
        if (plat::read_text(plat::join(exp, "config/missing_code.txt"), t)) files.push_back({"jogo_exportado/missing_code.txt", t});
        if (plat::read_text(plat::join(exp, "config/estatisticas_nativas.txt"), t)) files.push_back({"jogo_exportado/estatisticas_nativas.txt", t});
        if (plat::read_text(plat::join(exp, "config/settings.ini"), t)) files.push_back({"jogo_exportado/settings.ini", t});
    }
    files.push_back({"LEIA-ME.txt", "Pacote de diagnóstico do NativeBoy Studio.\r\n"
                                     "Contém relatórios, registros de compilação/verificação e informações do sistema.\r\n"
                                     "NÃO contém a ROM, o código do jogo, gráficos nem saves.\r\n"});
    if (!write_zip(out_zip, files)) { err = "Não foi possível gravar " + out_zip; return false; }
    return true;
}

// ------------------------------------------------------------------ game icon
static std::vector<uint32_t> box_resize(const std::vector<uint32_t>& src, int sw, int sh, int dw, int dh) {
    std::vector<uint32_t> out((size_t)dw * dh);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            int x0 = x * sw / dw, x1 = std::max(x0 + 1, (x + 1) * sw / dw), y0 = y * sh / dh, y1 = std::max(y0 + 1, (y + 1) * sh / dh);
            unsigned a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t c = src[(size_t)yy * sw + xx];
                    a += c >> 24; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
                }
            out[(size_t)y * dw + x] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    return out;
}

static void png_to_mem(void* ctx, void* data, int size) { auto* v = (std::vector<u8>*)ctx; v->insert(v->end(), (u8*)data, (u8*)data + size); }

static std::vector<u8> encode_png(const std::vector<uint32_t>& px, int w, int h) {
    std::vector<u8> rgba((size_t)w * h * 4), out;
    for (size_t i = 0; i < px.size(); i++) {
        rgba[i * 4] = (px[i] >> 16) & 255; rgba[i * 4 + 1] = (px[i] >> 8) & 255; rgba[i * 4 + 2] = px[i] & 255; rgba[i * 4 + 3] = px[i] >> 24;
    }
    stbi_write_png_to_func(png_to_mem, &out, w, h, 4, rgba.data(), w * 4);
    return out;
}

// .ico with PNG-compressed images (supported since Windows Vista)
static bool write_ico(const std::string& path, const std::vector<uint32_t>& px256) {
    const int sizes[4] = {256, 48, 32, 16};
    std::vector<std::vector<u8>> imgs;
    for (int s : sizes) imgs.push_back(encode_png(s == 256 ? px256 : box_resize(px256, 256, 256, s, s), s, s));
    std::vector<u8> o = {0, 0, 1, 0, 4, 0};
    uint32_t off = 6 + 16 * 4;
    for (int i = 0; i < 4; i++) {
        u8 d = sizes[i] == 256 ? 0 : (u8)sizes[i];
        uint32_t len = (uint32_t)imgs[i].size();
        u8 e[16] = {d, d, 0, 0, 1, 0, 32, 0, (u8)len, (u8)(len >> 8), (u8)(len >> 16), (u8)(len >> 24), (u8)off, (u8)(off >> 8), (u8)(off >> 16), (u8)(off >> 24)};
        o.insert(o.end(), e, e + 16);
        off += len;
    }
    for (auto& im : imgs) o.insert(o.end(), im.begin(), im.end());
    return plat::write_file(path, o.data(), o.size());
}

bool Project::make_icon(std::string& err) {
    std::vector<uint32_t> px; int w = 0, h = 0;
    std::string chosen = path("mods/icone.png");
    if (plat::file_exists(chosen) && load_png(chosen, px, w, h)) {
        // square crop from the centre, then resize to 256
        int s = std::min(w, h);
        std::vector<uint32_t> sq((size_t)s * s);
        for (int y = 0; y < s; y++) for (int x = 0; x < s; x++) sq[(size_t)y * s + x] = px[(size_t)(y + (h - s) / 2) * w + x + (w - s) / 2];
        px = s == 256 ? sq : box_resize(sq, s, s, 256, 256);
    } else {
        // automatic: the most colourful of the first ~20 s of the game (usually the title screen)
        auto mp = std::make_unique<gb::Machine>();   // large: keep it off the worker thread's stack
        gb::Machine& m = *mp;
        if (!m.load_rom(rom, err)) return false;
        std::vector<uint32_t> best; int best_n = 0;
        for (int f = 1; f <= 1200; f++) {
            m.run_frame(nullptr);
            m.apu->out.clear();
            if (f % 30 || f < 90) continue;
            std::vector<uint32_t> cols(m.fb, m.fb + 160 * 144);
            std::sort(cols.begin(), cols.end());
            int n = (int)(std::unique(cols.begin(), cols.end()) - cols.begin());
            if (n >= best_n) { best_n = n; best.assign(m.fb, m.fb + 160 * 144); }
        }
        if (best.empty()) { err = "o jogo não desenhou nenhuma tela"; return false; }
        px.assign(256 * 256, 0);
        for (int y = 0; y < 256; y++) for (int x = 0; x < 256; x++) px[(size_t)y * 256 + x] = best[(size_t)(8 + y / 2) * 160 + 16 + x / 2] | 0xFF000000u;
    }
    plat::make_dirs(path("build"));
    if (!save_png(path("build/icone.png"), px.data(), 256, 256)) { err = "não foi possível gravar o ícone"; return false; }
    if (!write_ico(path("build/icone.ico"), px)) { err = "não foi possível gravar icone.ico"; return false; }
    return true;
}

// ------------------------------------------------------------------ single-file executable and installers
static void list_files_rec(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
    std::string d = rel.empty() ? root : plat::join(root, rel);
    for (auto& f : plat::list_dir(d, false)) out.push_back(rel.empty() ? f : rel + "/" + f);
    for (auto& s : plat::list_dir(d, true)) list_files_rec(root, rel.empty() ? s : rel + "/" + s, out);
}

// the files of an exported game that are shipped (not the player's progress, captures or launchers)
static std::vector<std::string> game_files(const std::string& game_dir) {
    std::vector<std::string> files, out;
    list_files_rec(game_dir, "", files);
    std::sort(files.begin(), files.end());
    for (auto& rel : files) {
        if (rel.compare(0, 6, "saves/") == 0 || rel.compare(0, 9, "capturas/") == 0) continue;
        if (rel == "config/missing_code.txt" || rel == "LEIA-ME.txt" || rel == "desinstalar.ini") continue;
        if (rel.size() > 8 && rel.compare(rel.size() - 8, 8, ".desktop") == 0) continue;
        out.push_back(rel);
    }
    return out;
}

namespace {
struct PackWriter {
    std::string body, entries;
    uint32_t count = 0;
    uint64_t hash = 1469598103934665603ULL;
    uint64_t base = 0;                   // file offset where the body starts (= size of the program in front)
    static void put_str(std::string& o, const std::string& s) { uint16_t n = (uint16_t)s.size(); o.append((const char*)&n, 2); o += s; }
    template <class T> static void put(std::string& o, T v) { o.append((const char*)&v, sizeof v); }
    bool add(const std::string& rel, const std::vector<u8>& d, uint8_t flags) {
        int clen = 0;
        unsigned char* c = stbi_zlib_compress(d.empty() ? (unsigned char*)"" : (unsigned char*)d.data(), (int)d.size(), &clen, 6);
        if (!c) return false;
        put_str(entries, rel); put<uint64_t>(entries, base + body.size()); put<uint64_t>(entries, (uint64_t)clen); put<uint64_t>(entries, d.size()); put<uint8_t>(entries, flags);
        body.append((const char*)c, clen);
        free(c);
        for (char ch : rel) { hash ^= (u8)ch; hash *= 1099511628211ULL; }
        for (u8 b : d) { hash ^= b; hash *= 1099511628211ULL; }
        count++;
        return true;
    }
    // program + body + index (header strings, optional stub size, entries) + trailer
    void finish(std::vector<u8>& out, const char magic[8], const std::vector<std::string>& head, bool with_stub) {
        std::string idx;
        put<uint32_t>(idx, count);
        for (auto& h : head) put_str(idx, h);
        if (with_stub) put<uint64_t>(idx, base);
        idx += entries;
        uint64_t ioff = base + body.size(), isize = idx.size();
        out.insert(out.end(), body.begin(), body.end());
        out.insert(out.end(), idx.begin(), idx.end());
        out.insert(out.end(), magic, magic + 8);
        for (uint64_t v : {ioff, isize, hash}) { const u8* b = (const u8*)&v; out.insert(out.end(), b, b + 8); }
    }
};
} // namespace

// compiles runtime/<src> into build/obj/<out> once per project (Windows: with the game's icon)
static bool build_stub(const Project& p, const Toolchain& tc, const std::string& src_name, const std::string& out_name,
                       const std::string& libs, BuildLog& log, std::string& err, std::string& out_path) {
    const bool win = tc.win_target();
    std::string src = plat::join(plat::join(tc.sdk, "runtime"), src_name);
    if (!plat::file_exists(src)) { err = src_name + " não encontrado no SDK. Reinstale o NativeBoy Studio."; return false; }
    std::string obj = p.path(std::string(tc.cross_win ? "build-win" : "build") + "/obj");
    plat::make_dirs(obj);
    out_path = plat::join(obj, out_name);
    bool has_res = win && plat::file_exists(plat::join(obj, "icone_res.o"));
    if (plat::file_exists(out_path) && plat::file_mtime(out_path) >= plat::file_mtime(src) &&
        !(has_res && plat::file_mtime(out_path) < plat::file_mtime(plat::join(obj, "icone_res.o")))) return true;
    if (log.line) log.line("Compilando " + src_name + "...");
    plat::copy_file(src, plat::join(obj, src_name));
    std::string cmd = plat::quote(tc.gxx) + " -O2 -std=c++17 -w -I" + plat::quote(plat::join(plat::tool_path(tc.sdk), "stb")) + " " + src_name +
                      (has_res ? " icone_res.o" : "") + " -o " + out_name + " " + libs;
    plat::ProcResult r = plat::run(cmd, obj, tc.env());
    if (r.code != 0 || !plat::file_exists(out_path)) { err = "falha ao compilar " + src_name + ": " + r.output.substr(0, 400); return false; }
    return true;
}

static std::string ascii_id(const std::string& s) {
    std::string id;
    for (char c : s) id += (isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_';
    return id.empty() ? "jogo" : id;
}

bool Project::make_single_exe(const std::string& game_dir, const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_file) {
    const bool win = tc.win_target();
    std::string stub;
    if (!build_stub(*this, tc, "launcher.cpp", std::string("iniciador") + tc.exe_suffix(),
                    win ? "-static -static-libgcc -static-libstdc++ -mwindows -lole32 -lshell32 -luuid" : "-static-libstdc++ -static-libgcc", log, err, stub)) return false;
    std::vector<u8> outb;
    if (!plat::read_file(stub, outb)) { err = "iniciador ilegível"; return false; }
    PackWriter pw; pw.base = outb.size();
    for (auto& rel : game_files(game_dir)) {
        std::vector<u8> d;
        if (!plat::read_file(plat::join(game_dir, rel), d)) continue;
        if (!pw.add(rel, d, rel == "config/settings.ini" ? 1 : 0)) { err = "falha ao compactar " + rel; return false; }
    }
    const char magic[8] = {'N', 'B', 'P', 'A', 'C', 'K', '0', '1'};
    pw.finish(outb, magic, {ascii_id(exe_name), exe_name + tc.exe_suffix()}, false);
    std::string od = plat::join(games_root, "Executável único");
    plat::make_dirs(od);
    out_file = plat::join(od, exe_name + (win ? std::string(".exe") : std::string(".x86_64")));
    if (!plat::write_file(out_file, outb.data(), outb.size())) { err = "não foi possível gravar " + out_file + " (o jogo está aberto?)"; return false; }
    plat::set_executable(out_file);
    if (log.line) log.line("Executável único: " + std::to_string(pw.count) + " arquivos, " + std::to_string(outb.size() / 1024) + " KB.");
    return true;
}

// ---- Windows installer
bool Project::make_installer_windows(const std::string& game_dir, const std::string& games_root, const Toolchain& tc, BuildLog& log, std::string& err, std::string& out_file) {
    std::string stub;
    if (!build_stub(*this, tc, "instalador.cpp", "instalador.exe",
                    "-static -static-libgcc -static-libstdc++ -mwindows -municode -lcomctl32 -lole32 -lshell32 -luuid -ladvapi32", log, err, stub)) return false;
    std::vector<u8> outb;
    if (!plat::read_file(stub, outb)) { err = "instalador ilegível"; return false; }
    Settings gi; gi.load(plat::join(game_dir, "config/jogo.ini"));
    std::string name = gi.get("nome", exe_name), stamp = gi.get("exportado", plat::now_stamp());
    std::string version;                                       // 2026-11-10 09:51 -> 2026.11.10.951
    for (char c : stamp) if (isdigit((unsigned char)c)) version += c;
    if (version.size() >= 8) version = version.substr(0, 4) + "." + std::to_string(atoi(version.substr(4, 2).c_str())) + "." +
                                       std::to_string(atoi(version.substr(6, 2).c_str())) + (version.size() >= 12 ? "." + std::to_string(atoi(version.substr(8, 4).c_str())) : "");
    PackWriter pw; pw.base = outb.size();
    for (auto& rel : game_files(game_dir)) {
        std::vector<u8> d;
        if (!plat::read_file(plat::join(game_dir, rel), d)) continue;
        if (!pw.add(rel, d, rel == "config/settings.ini" ? 1 : 0)) { err = "falha ao compactar " + rel; return false; }
    }
    const char magic[8] = {'N', 'B', 'I', 'N', 'S', 'T', '0', '1'};
    pw.finish(outb, magic, {ascii_id(exe_name), exe_name + ".exe", name, version}, true);
    std::string od = plat::join(games_root, "Instaladores");
    plat::make_dirs(od);
    std::string fn = "Instalar " + name; for (auto& c : fn) if (strchr("\\/:*?\"<>|", c)) c = '_';
    out_file = plat::join(od, fn + ".exe");
    if (!plat::write_file(out_file, outb.data(), outb.size())) { err = "não foi possível gravar " + out_file; return false; }
    if (log.line) log.line("Instalador para Windows: " + std::to_string(outb.size() / 1024) + " KB.");
    return true;
}

// ---- Linux .deb (written here: ar + tar + gzip, no external tools)
namespace {
uint32_t crc32_of(const std::string& d) {
    static uint32_t t[256]; static bool init = false;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = t[(c ^ b) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
bool gzip(const std::string& in, std::string& out) {
    int clen = 0;
    unsigned char* z = stbi_zlib_compress((unsigned char*)in.data(), (int)in.size(), &clen, 8);
    if (!z || clen < 6) { free(z); return false; }
    const unsigned char hdr[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
    out.assign((const char*)hdr, 10);
    out.append((const char*)z + 2, clen - 6);                 // raw deflate: without the zlib header and adler32
    free(z);
    uint32_t crc = crc32_of(in), sz = (uint32_t)in.size();
    out.append((const char*)&crc, 4); out.append((const char*)&sz, 4);
    return true;
}
struct Tar {
    std::string data;
    long long mtime = (long long)time(nullptr);
    void entry(const std::string& name, const std::string& content, int mode, char type) {
        char h[512]; memset(h, 0, 512);
        std::string n = "./" + name;
        std::string prefix;
        if (n.size() > 99) { size_t cut = n.rfind('/', 154); if (cut != std::string::npos) { prefix = n.substr(0, cut); n = n.substr(cut + 1); } }
        memcpy(h, n.c_str(), std::min<size_t>(n.size(), 99));
        snprintf(h + 100, 8, "%07o", mode); snprintf(h + 108, 8, "%07o", 0); snprintf(h + 116, 8, "%07o", 0);
        snprintf(h + 124, 12, "%011llo", (unsigned long long)(type == '0' ? content.size() : 0));
        snprintf(h + 136, 12, "%011llo", mtime);
        memset(h + 148, ' ', 8);
        h[156] = type;
        memcpy(h + 257, "ustar", 6); memcpy(h + 263, "00", 2);
        memcpy(h + 265, "root", 4); memcpy(h + 297, "root", 4);
        if (!prefix.empty()) memcpy(h + 345, prefix.c_str(), std::min<size_t>(prefix.size(), 154));
        unsigned sum = 0; for (int i = 0; i < 512; i++) sum += (unsigned char)h[i];
        snprintf(h + 148, 8, "%06o", sum); h[154] = 0; h[155] = ' ';
        data.append(h, 512);
        if (type == '0') { data += content; data.append((512 - content.size() % 512) % 512, '\0'); }
    }
    std::vector<std::string> dirs_done;
    void dir(const std::string& d) {                            // with all its parents
        std::string acc;
        size_t p = 0;
        while (p <= d.size()) {
            size_t e = d.find('/', p); if (e == std::string::npos) e = d.size();
            acc = d.substr(0, e);
            if (std::find(dirs_done.begin(), dirs_done.end(), acc) == dirs_done.end()) { dirs_done.push_back(acc); entry(acc + "/", "", 0755, '5'); }
            p = e + 1;
        }
    }
    void file(const std::string& path, const std::string& content, int mode) {
        size_t sl = path.rfind('/');
        if (sl != std::string::npos) dir(path.substr(0, sl));
        entry(path, content, mode, '0');
    }
    std::string finish() { return data + std::string(1024, '\0'); }
};
void ar_member(std::string& out, const std::string& name, const std::string& d) {
    char h[61];
    snprintf(h, sizeof h, "%-16s%-12lld%-6d%-6d%-8s%-10zu`\n", name.c_str(), (long long)time(nullptr), 0, 0, "100644", d.size());
    out.append(h, 60);
    out += d;
    if (d.size() & 1) out += '\n';
}
} // namespace

bool Project::make_deb(const std::string& game_dir, const std::string& games_root, BuildLog& log, std::string& err, std::string& out_file) {
    Settings gi; gi.load(plat::join(game_dir, "config/jogo.ini"));
    std::string name = gi.get("nome", exe_name), stamp = gi.get("exportado", plat::now_stamp());
    std::string id = ascii_id(exe_name), pkg = "nativeboy-";
    for (char c : id) pkg += (isalnum((unsigned char)c)) ? (char)tolower((unsigned char)c) : '-';
    std::string digits; for (char c : stamp) if (isdigit((unsigned char)c)) digits += c;
    std::string version = "1.0." + (digits.empty() ? std::string("0") : digits);
    const std::string root = "opt/nativeboy-jogos/" + id;
    Tar data;
    size_t installed = 0;
    for (auto& rel : game_files(game_dir)) {
        std::string c;
        if (!plat::read_text(plat::join(game_dir, rel), c)) continue;
        bool exec = rel == exe_name || rel.find(".so") != std::string::npos;
        data.file(root + "/" + rel, c, exec ? 0755 : 0644);
        installed += c.size();
    }
    data.file(root + "/config/instalado.ini", "id=" + id + "\n", 0644);     // saves go to ~/.local/share/nativeboy/jogos/<id>
    data.file("usr/games/" + pkg, "#!/bin/sh\nexec \"/" + root + "/" + exe_name + "\" \"$@\"\n", 0755);
    bool icon = plat::file_exists(plat::join(game_dir, "assets/icone.png"));
    if (icon) { std::string c; plat::read_text(plat::join(game_dir, "assets/icone.png"), c); data.file("usr/share/icons/hicolor/256x256/apps/" + pkg + ".png", c, 0644); }
    data.file("usr/share/applications/" + pkg + ".desktop",
              "[Desktop Entry]\nType=Application\nName=" + name + "\nComment=Jogo nativo gerado pelo NativeBoy Studio\nExec=/usr/games/" + pkg +
              "\nPath=/" + root + "\n" + (icon ? "Icon=" + pkg + "\n" : "") + "Terminal=false\nCategories=Game;\nStartupWMClass=" + exe_name + "\n", 0644);
    Tar control;
    control.file("control",
                 "Package: " + pkg + "\nVersion: " + version + "\nArchitecture: amd64\nMaintainer: NativeBoy Studio <nativeboy@localhost>\n"
                 "Installed-Size: " + std::to_string(installed / 1024 + 1) + "\nSection: games\nPriority: optional\nDepends: libc6\n"
                 "Description: " + name + "\n Jogo de Game Boy convertido em programa nativo pelo NativeBoy Studio.\n", 0644);
    control.file("postinst", "#!/bin/sh\nset -e\ncommand -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true\n"
                             "command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true\nexit 0\n", 0755);
    std::string data_gz, control_gz;
    if (!gzip(data.finish(), data_gz) || !gzip(control.finish(), control_gz)) { err = "falha ao compactar o pacote"; return false; }
    std::string deb = "!<arch>\n";
    ar_member(deb, "debian-binary", "2.0\n");
    ar_member(deb, "control.tar.gz", control_gz);
    ar_member(deb, "data.tar.gz", data_gz);
    std::string od = plat::join(games_root, "Instaladores");
    plat::make_dirs(od);
    for (auto& f : plat::list_dir(od, false))                  // older versions of this game's package
        if (f.compare(0, pkg.size() + 1, pkg + "_") == 0 && f.size() > 4 && f.compare(f.size() - 4, 4, ".deb") == 0) plat::remove_file(plat::join(od, f));
    out_file = plat::join(od, pkg + "_" + version + "_amd64.deb");
    if (!plat::write_file(out_file, deb.data(), deb.size())) { err = "não foi possível gravar " + out_file; return false; }
    if (log.line) log.line("Pacote .deb para Linux: " + std::to_string(deb.size() / 1024) + " KB.");
    return true;
}
