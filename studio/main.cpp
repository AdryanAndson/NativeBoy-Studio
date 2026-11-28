// NativeBoy Studio - desktop application (Windows 10/11 x64 and Ubuntu/Lubuntu Linux x64).
#include "../common/platform.h"
#include "../common/player.h"
#include "../common/settings.h"
#include "../common/ui_common.h"
#include "../common/video.h"
#include "../core/apu.h"
#include "../core/autoplay.h"
#include "../recomp/disasm.h"
#include "jobs.h"
#include "project.h"
#include "github_apk.h"
#include "perfis.h"
#include "studio_extras.h"
#include "toolchain.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#define NB_VERSION "1.18.0"
#ifdef _WIN32
#define NB_PLAT "Windows x64"
#define NB_TOOLCHAIN "GCC (w64devkit, incluído)"
#else
#define NB_PLAT "Linux x64"
#define NB_TOOLCHAIN "GCC do sistema (pacote g++)"
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

using namespace gb;

enum Screen { SC_HOME, SC_PLAY, SC_DECOMP, SC_CONVERT, SC_MODIFY, SC_SETTINGS, SC_LIBRARY };
enum Action { ACT_PLAY, ACT_DECOMP, ACT_CONVERT, ACT_MODIFY };

// text colours from the theme (they must read well on the light grey one too)
#define COL_OK (ui::pal().ok)
#define COL_WARN (ui::pal().warn)
#define COL_ERR (ui::pal().err)
#define COL_INFO (ui::pal().info)
#define COL_DIM (ui::pal().dim)


// ------------------------------------------------------------------ updates
#ifndef NB_UPDATE_REPO
#define NB_UPDATE_REPO "AdryanAndson/NativeBoy-Studio"
#endif
static std::vector<int> version_parts(std::string v) {
    std::vector<int> r;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v = v.substr(1);
    int cur = 0; bool any = false;
    for (char c : v) {
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); any = true; }
        else if (c == '.') { r.push_back(cur); cur = 0; any = false; }
        else break;
    }
    if (any || r.empty()) r.push_back(cur);
    return r;
}
static bool version_newer(const std::string& a, const std::string& b) {   // a > b ?
    auto x = version_parts(a), y = version_parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}
static std::string json_str(const std::string& body, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t p = body.find("\"" + key + "\"", from);
    if (p == std::string::npos) return "";
    size_t c = body.find(':', p), q = body.find('"', c + 1);
    if (c == std::string::npos || q == std::string::npos) return "";
    size_t e = body.find('"', q + 1);
    if (at) *at = e;
    return body.substr(q + 1, e - q - 1);
}
// src: "owner/repo", "https://github.com/owner/repo" or the URL of a JSON channel file
static bool check_update(std::string src, std::string& ver, std::string& dl, std::string& err) {
    std::string gh = src;
    const std::string pre = "https://github.com/";
    if (gh.compare(0, pre.size(), pre) == 0) gh = gh.substr(pre.size());
    while (!gh.empty() && gh.back() == '/') gh.pop_back();
    bool is_repo = gh.find("://") == std::string::npos && std::count(gh.begin(), gh.end(), '/') == 1;
    std::string body;
    if (!is_repo) {
        if (!plat::http_get(src, body, err)) return false;
        ver = json_str(body, "versao"); dl = json_str(body, "download");
        if (ver.empty()) { err = "O canal não informou a versão."; return false; }
        return true;
    }
    if (!plat::http_get("https://api.github.com/repos/" + gh + "/releases/latest", body, err)) {
        err = "Não foi possível consultar as versões publicadas em github.com/" + gh + ".\n" + err;
        return false;
    }
    ver = json_str(body, "tag_name");
    if (ver.empty()) { err = "Nenhuma versão publicada em github.com/" + gh + "."; return false; }
    if (ver[0] == 'v' || ver[0] == 'V') ver = ver.substr(1);
    dl = json_str(body, "html_url");
#ifdef _WIN32
    const char* want[] = {"_Setup.exe", ".exe"};
#else
    const char* want[] = {getenv("APPIMAGE") ? ".AppImage" : ".deb", ".tar.gz"};
#endif
    for (const char* w : want) {
        size_t pos = 0, at;
        for (;;) {
            std::string u = json_str(body, "browser_download_url", pos, &at);
            if (u.empty()) break;
            pos = at;
            if (u.size() >= strlen(w) && u.compare(u.size() - strlen(w), strlen(w), w) == 0) { dl = u; return true; }
        }
    }
    return true;
}

// tab items; NB_TEST_TAB=<name> selects a tab at start-up (automated interface tests / screenshots)
static std::string g_goto_tab;          // a tab to open on the next frame (set by the mod assistant)
static bool tab_item(const char* name) {
    static const char* want = getenv("NB_TEST_TAB");
    static int frames_left = 30;
    bool sel = want && frames_left > 0 && !strcmp(want, name);
    if (!g_goto_tab.empty() && g_goto_tab == name) { sel = true; g_goto_tab.clear(); }
    if (want && frames_left > 0 && ImGui::GetFrameCount() > 2) frames_left--;
    return ImGui::BeginTabItem(name, nullptr, sel ? ImGuiTabItemFlags_SetSelected : 0);
}
struct App {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    std::string app_dir, user_dir;
    Settings prefs;
    Toolchain tc;
    bool export_windows = false;   // CLI --windows
    bool export_android = false;   // CLI --android (Android project for the APK)
    bool export_apk = false;       // CLI --apk-github (APK compiled on GitHub; key in NB_GITHUB_TOKEN)
    Toolchain tcw;          // Linux: MinGW-w64 toolchain for Windows .exe (detected on demand)
    Screen screen = SC_HOME;
    std::unique_ptr<Project> proj;
    std::unique_ptr<GamePlayer> player;
    std::vector<u8> play_trace;
    std::vector<Machine::RamExec> play_ram;
    TileDumper dumper;
    bool dumping = false;
    bool vgm_recording = false;
    Job job;
    bool quit = false;

    // dialogs
    bool show_err = false;
    std::string err_title, err_text, err_details;
    bool ask_action = false;
    bool recovery_prompt = false;
    std::string recovery_project;
    std::string toast; Uint32 toast_until = 0;

    // decompile screen
    int sel_func = -1;
    bool code_c = false;      // functions view: pseudo-C instead of assembly
    bool show_dbg = false;    // debugger window while playing
    StudioMsg studio_msg() {
        StudioMsg m;
        m.notify = [this](const std::string& s) { notify(s); };
        m.error = [this](const std::string& t, const std::string& s) { error(t, s); };
        m.test_play = [this]() { start_player(false); };
        m.ren = ren;
        return m;
    }
    char func_filter[64] = "";
    std::vector<std::string> disasm;
    int decomp_tab = 0;
    std::vector<std::string> gfx_files;
    int sel_gfx = 0;
    std::map<std::string, std::pair<SDL_Texture*, ImVec2>> tex_cache;

    // convert screen
    int fix_iters = 2;
    char manual_entry[16] = "";

    // modify screen
    std::vector<u8> ed_rom;
    bool ed_loaded = false, ed_dirty = false;
    int ed_bank = 0, ed_block = 0, ed_tile = 0, ed_color = 3;
    SDL_Texture* ed_tex = nullptr;
    bool ed_tex_dirty = true;
    std::string cheats_text, hooks_text, hooks_list;
    bool texts_loaded = false;
    std::string rgbds_output;
    int hd_scale = 4;
    char mod_name[64] = "minhas_alteracoes";

    // window model: dockable panels around the work area
    bool show_proj_panel = true, show_log_panel = true, show_status = true, want_default_layout = false;
    bool want_about = false, want_shortcuts = false;
    SDL_Rect play_area = {0, 0, 0, 0};      // where the game is drawn (the work panel, last frame)
    float ui_scale = 1.0f;                   // "Tamanho da interface" (font size)
    void draw_menubar();
    void draw_brandbar(float y, float h);
    void draw_statusbar(float y, float h);
    void draw_project_panel();
    void draw_log_window();
    void build_default_layout(unsigned dock_id, float w, float h);
    void handle_shortcuts();
    void set_theme(const std::string& theme, const std::string& shell);
    void update_title();
    // settings
    char url_buf[256] = "";
    // Android: APK built on GitHub
    char gh_token[128] = "";
    bool gh_remember = false, gh_loaded = false, android_open = false;

    std::string projects_root() const { return prefs.get("pasta_projetos", plat::join(plat::join(plat::documents_dir(), "NativeBoy Studio"), "Projetos")); }
    std::string games_root() const { return prefs.get("pasta_jogos", plat::join(plat::join(plat::documents_dir(), "NativeBoy Studio"), "Meus Jogos")); }

    void error(const std::string& title, const std::string& text, const std::string& details = "") {
        err_title = title; err_text = text; err_details = details; show_err = true;
    }
    void notify(const std::string& s) { toast = s; toast_until = SDL_GetTicks() + 3500; }
    void add_recent(const std::string& d) {
        std::vector<std::string> r = recent();
        r.erase(std::remove(r.begin(), r.end(), d), r.end());
        r.insert(r.begin(), d);
        if (r.size() > 8) r.resize(8);
        std::string s;
        for (auto& x : r) s += (s.empty() ? "" : "|") + x;
        prefs.set("recentes", s); prefs.save();
    }
    std::vector<std::string> recent() const {
        std::vector<std::string> r;
        std::string s = prefs.get("recentes", "");
        size_t p = 0;
        while (p < s.size()) { size_t q = s.find('|', p); if (q == std::string::npos) q = s.size(); if (q > p) r.push_back(s.substr(p, q - p)); p = q + 1; }
        return r;
    }
    void write_session() { plat::write_text(plat::join(user_dir, "sessao.lock"), proj ? proj->dir : std::string()); }

    // ------------------------------------------------------------------ project
    bool open_rom(const std::string& path) {
        std::string ext = path.size() > 4 ? path.substr(path.find_last_of('.') + 1) : "";
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == "nbproj" || plat::file_name(path) == "projeto.ini") return open_project(plat::parent(path));
        if (plat::dir_exists(path) && plat::file_exists(plat::join(path, "projeto.ini"))) return open_project(path);
        if (ext != "gb" && ext != "gbc" && ext != "sgb") {
            error("Arquivo não reconhecido", "Selecione uma ROM de Game Boy (.gb) ou Game Boy Color (.gbc).\n\nArquivo: " + plat::file_name(path));
            return false;
        }
        plat::make_dirs(projects_root());
        auto p = std::make_unique<Project>();
        std::string err;
        if (!Project::create_from_rom(path, projects_root(), *p, err)) {
            error("Não foi possível abrir a ROM", err);
            return false;
        }
        if (!err.empty()) notify(err);
        close_player();
        proj = std::move(p);
        ed_loaded = false; texts_loaded = false; sel_func = -1; disasm.clear(); gfx_files.clear();
        add_recent(proj->dir);
        write_session();
        return true;
    }
    bool open_project(const std::string& dir) {
        auto p = std::make_unique<Project>();
        std::string err;
        if (!p->open(dir, err)) { error("Não foi possível abrir o projeto", err); return false; }
        close_player();
        proj = std::move(p);
        ed_loaded = false; texts_loaded = false; sel_func = -1; disasm.clear(); gfx_files.clear();
        add_recent(proj->dir);
        write_session();
        if (plat::file_exists(proj->path("build/.compilando"))) notify("A última compilação deste projeto foi interrompida. Use \"Compilar\" para retomar.");
        return true;
    }
    bool choose_rom() {
        std::string f = plat::open_file_dialog("Selecione uma ROM de Game Boy", "ROMs de Game Boy (*.gb, *.gbc)", "*.gb;*.gbc");
        if (f.empty()) return false;
        return open_rom(f);
    }
    void go(Action a) {
        if (!proj && !choose_rom()) return;
        switch (a) {
        case ACT_PLAY: start_player(false); break;
        case ACT_DECOMP: screen = SC_DECOMP; if (proj->step() < ST_ANALYZED) run_decompile(); break;
        case ACT_CONVERT: screen = SC_CONVERT; break;
        case ACT_MODIFY: screen = SC_MODIFY; break;
        }
    }

    // ------------------------------------------------------------------ emulation
    void start_player(bool collect_tiles) {
        if (!proj) return;
        close_player();
        player = std::make_unique<GamePlayer>();
        std::string err;
        dumping = collect_tiles;
        if (dumping) { dumper.tiles.clear(); player->dumper = &dumper; }
        player->in_studio = true;
        player->studio_mods_dir = proj->path("mods");
        if (!player->start(proj->rom, proj->path("emulacao"), proj->name, nullptr, err)) {
            player.reset();
            error("Não foi possível iniciar a emulação", err);
            return;
        }
        // every code path you reach while playing is recorded for the recompiler
        play_trace.assign(player->m.cart.rom.size(), 0);
        play_ram.clear();
        player->m.trace_bitmap = &play_trace;
        player->m.ram_trace = &play_ram;
        screen = SC_PLAY;
    }
    void close_player() {
        if (!player) return;
        if (vgm_recording) stop_vgm();
        player->m.trace_bitmap = nullptr; player->m.ram_trace = nullptr;
        player->stop();
        if (proj && !play_trace.empty()) {
            int before = 0, after = 0;
            for (u8 v : proj->trace) before += v;
            proj->merge_trace(play_trace, play_ram);
            for (u8 v : proj->trace) after += v;
            if (after > before) notify("Sua sessão de jogo revelou " + fmt_num(after - before) + " novas instruções. Reconverta para traduzi-las.");
        }
        if (dumping && proj) {
            std::string err;
            int n = dumper.write(proj->path("hd/modelos"), hd_scale, err, proj->ini.getb("hd_suave", true));
            if (n) notify(std::to_string(n) + " modelos de tiles salvos em hd/modelos (escala " + std::to_string(hd_scale) + "x)");
            dumping = false;
        }
        player.reset();
        play_trace.clear(); play_ram.clear();
    }
    void stop_vgm() {
        if (!player || !vgm_recording) return;
        auto v = player->m.apu->stop_vgm(proj ? proj->name : "Jogo");
        vgm_recording = false;
        std::string f = proj->path("musica/captura_" + plat::now_stamp() + ".vgm");
        if (plat::write_file(f, v.data(), v.size())) notify("Música salva em musica/" + plat::file_name(f));
    }

    // ------------------------------------------------------------------ jobs
    void run_decompile() {
        Project* p = proj.get();
        int frames = prefs.geti("quadros_descoberta", 3600);
        job.start("Descompilando " + p->name, [this, p, frames](BuildLog& log, std::string& err) {
            if (p->trace.empty()) { job.set_stage("Descoberta de código (executando o jogo)", 0, 0.6f); if (!p->step_discover(frames, log, err)) return false; }
            job.set_stage("Análise e desmontagem", 0.6f, 0.4f);
            return p->step_analyze(log, err);
        }, [this](bool ok) {
            gfx_files.clear(); disasm.clear(); sel_func = -1;
            if (!ok) error("A descompilação não foi concluída", "Veja o registro para detalhes.", job.error);
            else notify("Descompilação concluída: projeto editável criado.");
        });
    }
    // full conversion chain with automatic correction
    void run_convert(bool rediscover, int extra_frames) {
        if (!tc.ok) { error("Compilador não disponível", tc.problem); return; }
        Project* p = proj.get();
        int dframes = extra_frames > 0 ? extra_frames : prefs.geti("quadros_descoberta", 3600);
        int vframes = prefs.geti("quadros_verificacao", 3600);
        int iters = fix_iters;
        job.start("Convertendo " + p->name + " para PC nativo", [this, p, rediscover, dframes, vframes, iters](BuildLog& log, std::string& err) {
            if (p->trace.empty() || rediscover) {
                job.set_stage("1/5 Descoberta de código", 0.0f, 0.2f);
                if (!p->step_discover(dframes, log, err)) return false;
            }
            for (int it = 0; it <= iters; it++) {
                float b = it == 0 ? 0.2f : 0.2f + 0.8f * it / (iters + 1);
                float s = 0.8f / (iters + 1);
                if (it > 0) log.line("Correção automática " + std::to_string(it) + ": traduzindo os trechos registrados pela verificação...");
                job.set_stage("2/5 Análise", b, s * 0.15f);
                if (!p->step_analyze(log, err)) return false;
                job.set_stage("3/5 Tradução SM83 -> C++", b + s * 0.15f, s * 0.1f);
                if (!p->step_generate(log, err)) return false;
                job.set_stage("4/5 Compilação " NB_PLAT, b + s * 0.25f, s * 0.55f);
                if (!p->step_build(tc, log, err)) return false;
                job.set_stage("5/5 Verificação nativo x original", b + s * 0.8f, s * 0.2f);
                if (!p->step_verify(vframes, log, err)) return false;
                if (!p->verify.identical || p->verify.missing.empty()) break;
                if (it == iters) { log.line("Ainda há trechos não traduzidos. Use \"Corrigir\" para continuar."); break; }
                int added = p->add_entries(p->verify.missing);
                log.line(std::to_string(added) + " novos pontos de entrada adicionados.");
                if (added == 0) break;
            }
            return true;
        }, [this](bool ok) {
            if (!ok) {
                std::string d = job.error;
                bool comp = d.find("compilar") != std::string::npos || d.find("link") != std::string::npos;
                error(comp ? "A compilação falhou" : "A conversão não foi concluída",
                      comp ? "O compilador C++ integrado encontrou um erro. Se você editou ganchos (hooks.cpp), revise o código; "
                             "caso contrário, envie o registro abaixo para os desenvolvedores do NativeBoy."
                           : "Veja os detalhes abaixo.",
                      d);
            } else {
                Report r = proj->make_report();
                notify(r.title);
            }
        });
    }
    void run_build_only() {
        if (!tc.ok) { error("Compilador não disponível", tc.problem); return; }
        Project* p = proj.get();
        int vframes = prefs.geti("quadros_verificacao", 3600);
        job.start("Compilando " + p->name, [this, p, vframes](BuildLog& log, std::string& err) {
            job.set_stage("Tradução", 0, 0.15f);
            if (p->step() < ST_GENERATED || !plat::file_exists(p->path("src/game_table.cpp"))) { if (!p->step_generate(log, err)) return false; }
            job.set_stage("Compilação", 0.15f, 0.7f);
            if (!p->step_build(tc, log, err)) return false;
            job.set_stage("Verificação", 0.85f, 0.15f);
            return p->step_verify(vframes, log, err);
        }, [this](bool ok) { if (!ok) error("A compilação falhou", "Veja os detalhes.", job.error); });
    }
    std::string system_info() const {
        char b[1024];
        snprintf(b, sizeof b, "NativeBoy Studio %s (%s)\nsistema=%s\nprocessadores=%d\nmemoria_mb=%d\ncompilador=%s\ncompilador_ok=%s\nproblema=%s\ngerado=%s\n",
                 NB_VERSION, NB_PLAT, SDL_GetPlatform(), SDL_GetCPUCount(), SDL_GetSystemRAM(), tc.version.c_str(), tc.ok ? "sim" : "nao",
                 tc.problem.c_str(), plat::now_stamp().c_str());
        std::string s = b;
        if (tcw.ok) s += "compilador_windows=" + tcw.version + "\n";
        return s;
    }
    void make_diagnostics() {
        if (!proj) return;
        std::string dir = plat::parent(projects_root());
        std::string out = plat::join(dir, "diagnostico_" + proj->exe_name + "_" + plat::now_stamp() + ".zip");
        std::string err;
        if (!proj->write_diagnostics_zip(out, system_info(), err)) error("Pacote de diagnóstico", err);
        else { notify("Pacote de diagnóstico criado"); plat::open_in_explorer(dir); }
    }
    void make_shortcuts_after_export(const std::string& dir, BuildLog& log) {
        bool d = prefs.getb("atalho_area_trabalho", true), m = prefs.getb("atalho_menu", true);
        if (!d && !m) return;
        std::string e;
        if (make_game_shortcuts(dir, d, m, e)) { if (log.line) log.line(std::string("Atalhos criados") + (d ? " na Área de Trabalho" : "") + (d && m ? " e" : "") + (m ? " no menu" : "") + "."); }
        else if (log.line) log.line("Aviso: " + e);
    }
    void run_export(bool windows_too = false) {
        Project* p = proj.get();
        std::string root = games_root();
        job.start("Exportando " + p->name, [this, p, root, windows_too](BuildLog& log, std::string& err) {
            std::string out;
            if (!p->step_export(root, tc, log, err, out)) return false;
            make_shortcuts_after_export(out, log);
            if (windows_too) {
                job.set_stage("Versão para Windows", 0.1f, 0.9f);
                if (log.line) log.line("Gerando também a versão para Windows (MinGW-w64)...");
                if (!p->step_build(tcw, log, err)) return false;
                if (!p->step_export(root, tcw, log, err, out)) return false;
            }
            return true;
        }, [this](bool ok) {
            if (!ok) error("Não foi possível exportar", job.error);
            else { notify("Jogo exportado para Meus Jogos"); plat::open_in_explorer(proj->ini.get("pasta_exportada", "")); }
        });
    }

    // Android project in Meus Jogos/Android/<name> (the game must be exported first); with apk = true
    // the project also goes to GitHub, which compiles the APK
    void run_android(bool apk) {
        Project* p = proj.get();
        std::string root = games_root(), token = gh_token;
        if (apk && token.empty()) { error("APK no GitHub", "Cole a chave de acesso do GitHub (token) no campo acima."); return; }
        if (gh_remember) prefs.set("github_token", token); else prefs.set("github_token", "");
        prefs.save();
        job.start(apk ? "Gerando o APK de " + p->name : "Projeto Android de " + p->name, [this, p, root, token, apk](BuildLog& log, std::string& err) {
            std::string out;
            if (p->ini.get("pasta_exportada", "").empty() || !plat::file_exists(p->ini.get("pasta_exportada", ""))) {
                if (log.line) log.line("Exportando o jogo primeiro...");
                if (!p->step_export(root, tc, log, err, out)) return false;
            }
            if (!p->make_android_project(root, tc, log, err, out)) return false;
            if (!apk) return true;
            ApkResult r;
            const bool ok = build_apk_on_github(out, token, apk_repo_name(p->name), plat::join(root, "Android"), log, r, err);
            p->ini.set("apk_repositorio", r.repo_url);
            if (ok) p->ini.set("apk_arquivo", r.apk);
            p->save();
            return ok;
        }, [this, apk](bool ok) {
            if (!ok) { error(apk ? "Não foi possível gerar o APK" : "Não foi possível criar o projeto Android", job.error); return; }
            if (apk) { notify("APK pronto! Copie para o celular e instale."); plat::open_in_explorer(plat::parent(proj->ini.get("apk_arquivo", ""))); }
            else { notify("Projeto Android criado"); plat::open_in_explorer(proj->ini.get("pasta_android", "")); }
        });
    }

    // ------------------------------------------------------------------ textures
    SDL_Texture* texture_for(const std::string& path, ImVec2& size) {
        auto it = tex_cache.find(path);
        if (it != tex_cache.end()) { size = it->second.second; return it->second.first; }
        std::vector<uint32_t> px; int w, h;
        SDL_Texture* t = nullptr;
        if (load_png(path, px, w, h)) {
            t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            SDL_UpdateTexture(t, nullptr, px.data(), w * 4);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
            size = ImVec2((float)w, (float)h);
        }
        tex_cache[path] = {t, size};
        return t;
    }
    void clear_textures() {
        for (auto& e : tex_cache) if (e.second.first) SDL_DestroyTexture(e.second.first);
        tex_cache.clear();
    }

    void frame();
    void draw_home();
    void draw_play(const SDL_Rect& area);
    void draw_decomp();
    void draw_convert();
    void draw_modify();
    void draw_settings();
    void draw_job_panel(float height);
    void draw_report(const Report& r);
    void draw_dialogs();
};

static bool big_card(const char* id, const char* title, const char* desc, const char* tag, ImVec4 accent, ImVec2 size) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("card", size);
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ui::Pal& P = ui::pal();
    const ImU32 ac = ImGui::ColorConvertFloat4ToU32(accent);
    const float r = ui::theme_gb ? 6.f : 14.f;
    if (ui::theme_gb) {
        // a small screen: dark bezel, green glass, dark-green print
        dl->AddRectFilled(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + size.x + 3, p.y + size.y + 4), IM_COL32(0, 0, 0, 40), r);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(86, 88, 104, 255), r);
        dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8), ImVec2(p.x + size.x - 8, p.y + size.y - 8), hov ? P.card_hover : P.card, 3);
        dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8), ImVec2(p.x + 14, p.y + size.y - 8), ac, 2);
        ui::led(dl, ImVec2(p.x + size.x - 18, p.y + size.y - 18), 4.f, hov);
    } else {
        dl->AddRectFilled(ImVec2(p.x + 2, p.y + 5), ImVec2(p.x + size.x + 2, p.y + size.y + 5), IM_COL32(0, 0, 0, 80), r);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), hov ? P.card_hover : P.card, r);
        dl->AddRectFilled(p, ImVec2(p.x + 10, p.y + size.y), ac, r, ImDrawFlags_RoundCornersLeft);
        if (hov) dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ac, r, 0, 2.0f);
    }
    float fs = ui::base_size;
    const float x0 = p.x + 30;
    dl->AddText(ui::font_pixel, fs * 1.05f, ImVec2(x0, p.y + 26), P.card_text, title);
    if (tag && *tag) {
        ImVec2 ts = ui::font_pixel->CalcTextSizeA(fs * 0.62f, FLT_MAX, 0, tag);
        ImVec2 tp(p.x + size.x - ts.x - 32, p.y + 26);
        dl->AddRectFilled(ImVec2(tp.x - 8, tp.y - 6), ImVec2(tp.x + ts.x + 8, tp.y + ts.y + 6), ui::theme_gb ? P.lcd[0] : ac, ui::theme_gb ? 2.f : 8.f);
        dl->AddText(ui::font_pixel, fs * 0.62f, tp, ui::theme_gb ? P.lcd[3] : IM_COL32(255, 255, 255, 255), tag);
    }
    dl->AddText(ui::font_text, fs * 0.95f, ImVec2(x0, p.y + 34 + fs * 1.6f), P.card_dim, desc, nullptr, size.x - 70);
    ImGui::PopID();
    return clicked;
}

static const char* step_names[] = {"Importado", "Código descoberto", "Descompilado", "Traduzido para C++", "Compilado", "Verificado", "Exportado"};

void App::set_theme(const std::string& theme, const std::string& shell) {
    prefs.set("tema", theme); prefs.set("cor_carcaca", shell); prefs.save();
    ui::apply_theme(theme, shell);
    ImGui::GetStyle().ScaleAllSizes(ui_scale);
    ui::title_bar(win);
}

void App::update_title() {
    std::string t = proj ? proj->name + " - NativeBoy Studio" : std::string("NativeBoy Studio");
    if (job.running) t = "(" + job.title + ") " + t;
    static std::string last;
    if (t != last) { last = t; SDL_SetWindowTitle(win, t.c_str()); }
}

// keyboard shortcuts of the menus (also work with the menus closed)
void App::handle_shortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    const bool busy = job.running;
    auto key = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (screen == SC_PLAY && !ctrl) { if (key(ImGuiKey_F11)) player->want_fullscreen_toggle = true; return; }   // (the game uses the keyboard)
    if (ctrl && key(ImGuiKey_O) && !busy) { if (shift) { std::string d = plat::folder_dialog("Selecione a pasta do projeto"); if (!d.empty()) open_project(d); } else if (choose_rom()) ask_action = true; }
    if (ctrl && key(ImGuiKey_W) && proj && !busy) { close_player(); proj.reset(); screen = SC_HOME; write_session(); }
    if (ctrl && key(ImGuiKey_Q)) quit = true;
    if (ctrl && key(ImGuiKey_Comma)) { close_player(); screen = SC_SETTINGS; }
    if (ctrl && key(ImGuiKey_L)) { close_player(); screen = SC_LIBRARY; }
    if (ctrl && shift && key(ImGuiKey_E) && proj) plat::open_in_explorer(proj->dir);
    if (!ctrl && !busy) {
        if (key(ImGuiKey_F5)) go(ACT_PLAY);
        if (key(ImGuiKey_F6)) { close_player(); go(ACT_DECOMP); }
        if (key(ImGuiKey_F7)) { close_player(); go(ACT_CONVERT); }
        if (key(ImGuiKey_F8)) { close_player(); go(ACT_MODIFY); }
    }
    if (key(ImGuiKey_F11)) { bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP; SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); }
    if (key(ImGuiKey_F1)) want_shortcuts = true;
}

void App::draw_menubar() {
    if (!ImGui::BeginMainMenuBar()) return;
    const bool busy = job.running;
    if (ImGui::BeginMenu("Arquivo")) {
        if (ImGui::MenuItem("Abrir ROM...", "Ctrl+O", false, !busy)) { if (choose_rom()) ask_action = true; }
        if (ImGui::MenuItem("Abrir projeto...", "Ctrl+Shift+O", false, !busy)) { std::string d = plat::folder_dialog("Selecione a pasta do projeto"); if (!d.empty()) open_project(d); }
        if (ImGui::BeginMenu("Projetos recentes", !busy)) {
            auto rec = recent();
            int n = 0;
            for (auto& r : rec) {
                if (!plat::dir_exists(r)) continue;
                Settings st; st.load(plat::join(r, "projeto.ini"));
                const int k = std::max(0, std::min(6, st.geti("etapa", 0)));
                if (ImGui::MenuItem((st.get("nome", plat::file_name(r)) + "##" + r).c_str(), step_names[k])) open_project(r);
                n++;
            }
            if (!n) ImGui::MenuItem("(nenhum)", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Abrir pasta do projeto", "Ctrl+Shift+E", false, proj != nullptr)) plat::open_in_explorer(proj->dir);
        if (ImGui::MenuItem("Abrir pasta Meus Jogos")) { plat::make_dirs(games_root()); plat::open_in_explorer(games_root()); }
        if (ImGui::MenuItem("Fechar projeto", "Ctrl+W", false, proj && !busy)) { close_player(); proj.reset(); screen = SC_HOME; write_session(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Sair", "Ctrl+Q")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Jogo")) {
        if (ImGui::MenuItem("Jogar ROM (emulação)", "F5", screen == SC_PLAY, !busy)) go(ACT_PLAY);
        if (ImGui::MenuItem("Descompilar", "F6", screen == SC_DECOMP, !busy)) { close_player(); go(ACT_DECOMP); }
        if (ImGui::MenuItem("Converter para PC nativo", "F7", screen == SC_CONVERT, !busy)) { close_player(); go(ACT_CONVERT); }
        if (ImGui::MenuItem("Modificar", "F8", screen == SC_MODIFY, !busy)) { close_player(); go(ACT_MODIFY); }
        ImGui::Separator();
        const bool exe = proj && plat::file_exists(proj->exe_path());
        if (ImGui::MenuItem("Rodar o executável nativo", nullptr, false, exe)) plat::launch(proj->exe_path(), "", plat::parent(proj->exe_path()));
        if (ImGui::MenuItem("Biblioteca de jogos", "Ctrl+L", screen == SC_LIBRARY)) { close_player(); screen = SC_LIBRARY; }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Exibir")) {
        ImGui::MenuItem("Painel do projeto", nullptr, &show_proj_panel);
        ImGui::MenuItem("Registro de atividades", nullptr, &show_log_panel);
        ImGui::MenuItem("Barra de status", nullptr, &show_status);
        if (ImGui::MenuItem("Restaurar a disposição dos painéis")) want_default_layout = true;
        ImGui::Separator();
        if (ImGui::BeginMenu("Tema")) {
            const std::string cur = prefs.get("tema", "gbc"), shell = prefs.get("cor_carcaca", "uva");
            if (ImGui::MenuItem(ui::theme_names[0], nullptr, cur == "gb")) set_theme("gb", shell);
            ImGui::SeparatorText("GB Color");
            int ns = 0; const ui::Shell* sh = ui::shells(&ns);
            for (int i = 0; i < ns; i++) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(sh[i].color | 0xFF000000));
                ImGui::Bullet();
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::MenuItem(sh[i].name, nullptr, cur == "gbc" && shell == sh[i].id)) set_theme("gbc", sh[i].id);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tamanho da interface")) {
            static const float sizes[] = {0.85f, 1.0f, 1.15f, 1.3f, 1.5f};
            static const char* names[] = {"Pequeno", "Normal", "Grande", "Maior", "Enorme"};
            for (int i = 0; i < 5; i++) if (ImGui::MenuItem(names[i], nullptr, std::fabs(ui_scale - sizes[i]) < 0.01f)) {
                ui_scale = sizes[i];
                prefs.set("tamanho_interface", std::to_string((int)(ui_scale * 100))); prefs.save();
                ImGui::GetStyle().FontScaleMain = ui_scale;
                set_theme(prefs.get("tema", "gbc"), prefs.get("cor_carcaca", "uva"));
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (ImGui::MenuItem("Tela cheia", "F11", fs)) SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ferramentas")) {
        if (ImGui::MenuItem("Preferências e ferramentas", "Ctrl+,", screen == SC_SETTINGS)) { close_player(); screen = SC_SETTINGS; }
        if (ImGui::MenuItem("Pasta de dados do Studio")) plat::open_in_explorer(user_dir);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ajuda")) {
        if (ImGui::MenuItem("Atalhos de teclado", "F1")) want_shortcuts = true;
        if (ImGui::MenuItem("Página do projeto no GitHub")) plat::open_url("https://github.com/" NB_UPDATE_REPO);
        ImGui::Separator();
        if (ImGui::MenuItem("Sobre o NativeBoy Studio")) want_about = true;
        ImGui::EndMenu();
    }
    // the work in progress, at the right of the menus (like the progress of a native program)
    if (job.running) {
        float pr; { std::lock_guard<std::mutex> lk(job.mu); pr = job.progress; }
        const float w = 220;
        ImGui::SameLine(ImGui::GetWindowWidth() - w - 16);
        char lb[16]; snprintf(lb, sizeof lb, "%d%%", (int)(pr * 100));
        ImGui::ProgressBar(pr, ImVec2(w, ImGui::GetTextLineHeight() + 2), lb);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", job.title.c_str());
    }
    ImGui::EndMainMenuBar();
}

// brand and the work modes (like the cartridge slots of the studio)
void App::draw_brandbar(float y, float h) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ui::Pal& P = ui::pal();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(ui::theme_gb ? P.shell : (P.shell_dark & 0x00FFFFFF) | 0xF0000000));
    ImGui::Begin("##marca", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float x = vp->Pos.x;
    // power light + name in pixel letters
    ui::led(dl, ImVec2(x + 22, y + h / 2), 5.f, true);
    ImGui::SetCursorScreenPos(ImVec2(x + 40, y + h / 2 - ui::base_size * 0.62f));
    ui::push_pixel(1.15f);
    const ImU32 name_col = ui::theme_gb ? P.print : IM_COL32(255, 255, 255, 255);
    ImVec2 np = ImGui::GetCursorScreenPos();
    dl->AddText(ImVec2(np.x + 2, np.y + 2), ui::theme_gb ? IM_COL32(255, 255, 255, 120) : IM_COL32(0, 0, 0, 120), "NATIVEBOY");
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(name_col), "NATIVEBOY");
    ImGui::PopFont();
    ImGui::SameLine(0, 8);
    ui::push_head(1.0f);
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y + h / 2 - ImGui::GetTextLineHeight() / 2 - 1));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::theme_gb ? P.key_a : ui::accent(3)), "Studio");
    ui::pop_font();
    // work modes
    ImGui::SameLine(0, 30);
    const bool busy = job.running;
    ImGui::BeginDisabled(busy);
    auto mode = [&](const char* label, Screen sc, std::function<void()> on, int k) {
        const bool active = screen == sc;
        ui::push_head(0.95f);
        ImVec2 ts = ImGui::CalcTextSize(label);
        ImVec2 sz(ts.x + 26, h - 18);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, y + 9));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(label);
        if (ImGui::InvisibleButton("m", sz)) on();
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 bg = 0, fg = name_col;
        if (ui::theme_gb) {
            if (active) { bg = P.lcd[3]; fg = P.lcd[0]; } else if (hov) { bg = P.shell_light; }
            if (bg) dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, 3);
            if (active) dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), P.lcd[1], 3.f, 0, 2.f);
        } else {
            if (active) bg = ui::accent(k); else if (hov) bg = IM_COL32(255, 255, 255, 28);
            if (bg) dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), bg, 10);
            if (!active) dl->AddRectFilled(ImVec2(p.x + 10, p.y + sz.y - 4), ImVec2(p.x + sz.x - 10, p.y + sz.y - 2), (ui::accent(k) & 0x00FFFFFF) | 0x90000000, 1);
            fg = IM_COL32(255, 255, 255, active ? 255 : 220);
        }
        dl->AddText(ImVec2(p.x + 13, p.y + (sz.y - ts.y) / 2), fg, label);
        ui::pop_font();
        ImGui::SameLine(0, 6);
    };
    mode("INÍCIO", SC_HOME, [&] { close_player(); screen = SC_HOME; }, 4);
    mode("BIBLIOTECA", SC_LIBRARY, [&] { close_player(); screen = SC_LIBRARY; }, 3);
    mode("JOGAR", SC_PLAY, [&] { go(ACT_PLAY); }, 0);
    mode("DESCOMPILAR", SC_DECOMP, [&] { close_player(); go(ACT_DECOMP); }, 4);
    mode("CONVERTER", SC_CONVERT, [&] { close_player(); go(ACT_CONVERT); }, 2);
    mode("MODIFICAR", SC_MODIFY, [&] { close_player(); go(ACT_MODIFY); }, 1);
    mode("FERRAMENTAS", SC_SETTINGS, [&] { close_player(); screen = SC_SETTINGS; }, 3);
    ImGui::EndDisabled();
    // the cartridge of the open project, at the right
    if (proj) {
        const float after = ImGui::GetCursorScreenPos().x + 10;
        std::string name = proj->name;
        const float cw = 26, max_w = vp->Pos.x + vp->Size.x - 20 - after - cw - 12;
        while (name.size() > 4 && ImGui::CalcTextSize((name + "...").c_str()).x > max_w) { name.pop_back(); while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) name.pop_back(); }
        if (name != proj->name) name += "...";
        const float tw = ImGui::CalcTextSize(name.c_str()).x;
        if (max_w > 40) {
            const float cx = vp->Pos.x + vp->Size.x - 20 - tw - cw - 12;
            // cartridge: grey for GB games, the theme colour for GBC ones
            const ImU32 cc = proj->info.cgb_supported ? ui::accent(proj->info.cgb_only ? 0 : 4) : IM_COL32(160, 158, 170, 255);
            const float cy = y + h / 2 - 15;
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + 30), cc, 3);
            dl->AddRectFilled(ImVec2(cx + cw - 6, cy), ImVec2(cx + cw, cy + 6), ui::theme_gb ? P.shell : P.shell_dark);
            dl->AddRectFilled(ImVec2(cx + 4, cy + 8), ImVec2(cx + cw - 4, cy + 22), IM_COL32(240, 236, 220, 255), 2);
            dl->AddRectFilled(ImVec2(cx + 6, cy + 10), ImVec2(cx + cw - 6, cy + 14), cc);
            ImGui::SetCursorScreenPos(ImVec2(cx + cw + 10, y + h / 2 - ImGui::GetTextLineHeight() / 2));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(name_col), "%s", name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s - %s\n%s", proj->name.c_str(), proj->info.cgb_only ? "Somente GB Color" : proj->info.cgb_supported ? "GB + GB Color" : "GB",
                                                          proj->info.mbc_name.c_str(), step_names[std::max(0, std::min(6, proj->step()))]);
        }
    }
    ui::stripe(dl, vp->Pos.x, vp->Pos.x + vp->Size.x, y + h - (ui::theme_gb ? 8 : 3), ui::theme_gb ? 3.f : 3.f);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void App::draw_statusbar(float y, float h) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ui::Pal& P = ui::pal();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, y));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPosY((h - ImGui::GetTextLineHeight()) / 2);
    const ImVec2 lp = ImGui::GetCursorScreenPos();
    ui::led(dl, ImVec2(lp.x + 5, lp.y + ImGui::GetTextLineHeight() / 2), 3.5f, true);
    ImGui::Dummy(ImVec2(12, 1)); ImGui::SameLine();
    if (job.running) {
        float pr; std::string stage;
        { std::lock_guard<std::mutex> lk(job.mu); pr = job.progress; stage = job.stage; }
        ImGui::Text("%s", job.title.c_str());
        ImGui::SameLine();
        ImGui::ProgressBar(pr, ImVec2(180, ImGui::GetTextLineHeight()), stage.c_str());
    } else if (proj) {
        ImGui::Text("%s", proj->name.c_str());
        ImGui::SameLine(); ImGui::TextColored(COL_DIM, "|"); ImGui::SameLine();
        ImGui::Text("%s", step_names[std::max(0, std::min(6, proj->step()))]);
        if (proj->verify.ran) {
            const double tot = (double)proj->verify.native_entries + proj->verify.fb_rom + proj->verify.fb_ram;
            ImGui::SameLine(); ImGui::TextColored(COL_DIM, "|"); ImGui::SameLine();
            ImGui::TextColored(proj->verify.fb_rom + proj->verify.fb_ram == 0 ? COL_OK : COL_WARN, "Nativo: %.1f%%", tot > 0 ? 100.0 * proj->verify.native_entries / tot : 0.0);
        }
    } else ImGui::TextColored(COL_DIM, "Pronto. Abra uma ROM (Ctrl+O) ou arraste uma para a janela.");
    // right side: compiler and version
    char right[160];
    snprintf(right, sizeof right, "%s   %s   v%s", tc.ok ? "Compilador pronto" : "Compilador indisponível", ui::theme_gb ? "GB" : "GB Color", NB_VERSION);
    const float rw = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, vp->Size.x - rw - 16));
    ImGui::TextColored(tc.ok ? COL_DIM : COL_ERR, "%s", right);
    (void)P;
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// left panel: the open project as a cartridge, its steps and how much of it runs natively
void App::draw_project_panel() {
    const ui::Pal& P = ui::pal();
    if (!proj) {
        ui::section("SEM PROJETO");
        ImGui::TextWrapped("Abra uma ROM .gb / .gbc (Ctrl+O) ou arraste uma para a janela.");
        if (ImGui::Button("Abrir ROM...", ImVec2(-1, 0)) && choose_rom()) ask_action = true;
        ui::section("RECENTES");
        int n = 0;
        for (auto& r : recent()) {
            if (!plat::dir_exists(r)) continue;
            Settings st; st.load(plat::join(r, "projeto.ini"));
            const int k = std::max(0, std::min(6, st.geti("etapa", 0)));
            if (ImGui::Selectable((st.get("nome", plat::file_name(r)) + "##" + r).c_str())) open_project(r);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", r.c_str(), step_names[k]);
            n++;
        }
        if (!n) ImGui::TextColored(COL_DIM, "Nenhum projeto ainda.");
        return;
    }
    // cartridge with the game name on its label
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float W = std::min(ImGui::GetContentRegionAvail().x, 230.f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float ch = W * 0.62f;
    const ImU32 body = proj->info.cgb_supported ? ui::accent(proj->info.cgb_only ? 0 : 4) : IM_COL32(168, 166, 176, 255);
    dl->AddRectFilled(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + W + 3, p.y + ch + 4), IM_COL32(0, 0, 0, 50), 6);
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + ch), body, 6);
    dl->AddRectFilled(ImVec2(p.x + W - 22, p.y), ImVec2(p.x + W, p.y + 18), ImGui::GetColorU32(ImGuiCol_WindowBg));
    for (int i = 0; i < 4; i++) dl->AddRectFilled(ImVec2(p.x + 12 + i * 10, p.y + 6), ImVec2(p.x + 18 + i * 10, p.y + 9), IM_COL32(0, 0, 0, 50));
    const ImVec2 la(p.x + 14, p.y + 18), lb(p.x + W - 14, p.y + ch - 12);
    dl->AddRectFilled(la, lb, IM_COL32(242, 238, 224, 255), 4);
    dl->AddRectFilled(ImVec2(la.x, la.y), ImVec2(lb.x, la.y + 10), body, 4, ImDrawFlags_RoundCornersTop);
    std::string nm = proj->name;
    dl->PushClipRect(ImVec2(la.x + 6, la.y), ImVec2(lb.x - 6, lb.y), true);
    dl->AddText(ui::font_head, ui::base_size * 1.15f, ImVec2(la.x + 8, la.y + 16), IM_COL32(40, 36, 60, 255), nm.c_str(), nullptr, lb.x - la.x - 16);
    dl->AddText(ui::font_pixel, ui::base_size * 0.5f, ImVec2(la.x + 8, lb.y - 16), IM_COL32(90, 86, 110, 255), proj->info.cgb_only ? "GB COLOR" : proj->info.cgb_supported ? "GB / GB COLOR" : "GB");
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(W, ch + 8));
    ImGui::TextColored(COL_DIM, "%s | %d KB | %s", proj->info.mbc_name.c_str(), (int)(proj->rom.size() / 1024), proj->info.has_battery ? "com save" : "sem save");
    // steps of the conversion
    ui::section("ETAPAS");
    const int st = proj->step();
    for (int i = 0; i < 7; i++) {
        const bool done = st >= i;
        ImVec2 q = ImGui::GetCursorScreenPos();
        const float lh = ImGui::GetTextLineHeight();
        ImU32 dot = done ? (ui::theme_gb ? P.lcd[1] : ui::accent(2)) : ImGui::GetColorU32(ImGuiCol_FrameBg);
        dl->AddRectFilled(ImVec2(q.x, q.y + lh * 0.2f), ImVec2(q.x + lh * 0.6f, q.y + lh * 0.8f), dot, ui::theme_gb ? 1.f : 4.f);
        if (i < 6) dl->AddLine(ImVec2(q.x + lh * 0.3f, q.y + lh * 0.8f), ImVec2(q.x + lh * 0.3f, q.y + lh + ImGui::GetStyle().ItemSpacing.y * 0.6f + lh * 0.2f), dot, 2);
        ImGui::Dummy(ImVec2(lh * 0.6f, lh * 0.85f)); ImGui::SameLine();
        if (done) ImGui::TextUnformatted(step_names[i]); else ImGui::TextColored(COL_DIM, "%s", step_names[i]);
    }
    // how much runs natively (verification)
    ui::section("NATIVIDADE");
    if (proj->verify.ran) {
        const double tot = (double)proj->verify.native_entries + proj->verify.fb_rom + proj->verify.fb_ram;
        const float f = tot > 0 ? (float)(proj->verify.native_entries / tot) : 0.f;
        // eight-segment bar like a battery
        const ImVec2 b = ImGui::GetCursorScreenPos();
        const float bw = ImGui::GetContentRegionAvail().x - 8, seg = bw / 10;
        for (int i = 0; i < 10; i++) {
            const bool on = f * 10 >= i + 0.5f;
            dl->AddRectFilled(ImVec2(b.x + i * seg, b.y), ImVec2(b.x + (i + 1) * seg - 3, b.y + 14),
                              on ? (ui::theme_gb ? P.lcd[1] : ui::accent(f > 0.999f ? 2 : 3)) : ImGui::GetColorU32(ImGuiCol_FrameBg), 2);
        }
        ImGui::Dummy(ImVec2(bw, 18));
        ImGui::Text("%.2f%% nativo", f * 100);
        if (proj->verify.fb_rom + proj->verify.fb_ram)
            ImGui::TextColored(COL_DIM, "%s chamadas caíram na emulação", fmt_num((long long)(proj->verify.fb_rom + proj->verify.fb_ram)).c_str());
        else ImGui::TextColored(COL_OK, "Tudo roda em código nativo");
        ImGui::TextColored(proj->verify.identical ? COL_OK : COL_WARN, proj->verify.identical ? "Idêntico ao original" : "Diferente do original");
    } else ImGui::TextColored(COL_DIM, "Converta (F7) para medir.");
    ui::section("AÇÕES");
    const bool busy = job.running;
    ImGui::BeginDisabled(busy);
    const float bw2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Jogar", ImVec2(bw2, 0))) go(ACT_PLAY);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F5");
    ImGui::SameLine();
    if (ImGui::Button("Converter", ImVec2(bw2, 0))) { close_player(); go(ACT_CONVERT); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F7");
    if (ImGui::Button("Descompilar", ImVec2(bw2, 0))) { close_player(); go(ACT_DECOMP); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F6");
    ImGui::SameLine();
    if (ImGui::Button("Modificar", ImVec2(bw2, 0))) { close_player(); go(ACT_MODIFY); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F8");
    ImGui::EndDisabled();
    if (ImGui::Button("Abrir pasta do projeto", ImVec2(-1, 0))) plat::open_in_explorer(proj->dir);
    if (plat::file_exists(proj->exe_path()) && ImGui::Button("Rodar o executável nativo", ImVec2(-1, 0))) plat::launch(proj->exe_path(), "", plat::parent(proj->exe_path()));
}

void App::draw_log_window() {
    std::vector<std::string> lines;
    float prog; std::string stage;
    { std::lock_guard<std::mutex> lk(job.mu); lines = job.lines; prog = job.progress; stage = job.stage; }
    if (job.running) {
        ImGui::Text("%s", job.title.c_str());
        ImGui::ProgressBar(prog, ImVec2(-120, 0), stage.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) job.cancel = true;
    } else if (!lines.empty()) {
        ImGui::TextColored(job.ok ? COL_OK : COL_ERR, "%s: %s", job.title.c_str(), job.ok ? "concluído" : "não concluído");
        ImGui::SameLine();
        if (ImGui::SmallButton("Copiar")) { std::string all; for (auto& l : lines) all += l + "\n"; ImGui::SetClipboardText(all.c_str()); }
    } else ImGui::TextColored(COL_DIM, "Nenhuma tarefa ainda. Conversões, compilações e exportações aparecem aqui.");
    ImGui::BeginChild("linhas", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& l : lines) {
        if (l.rfind("ERRO", 0) == 0) ImGui::TextColored(COL_ERR, "%s", l.c_str());
        else if (l.rfind("==", 0) == 0) ImGui::TextColored(COL_INFO, "%s", l.c_str());
        else ImGui::TextUnformatted(l.c_str());
    }
    if (job.running) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

void App::build_default_layout(unsigned id, float w, float h) {
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImVec2(w, h));
    ImGuiID center = id, left = 0, bottom = 0;
    left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, std::min(0.24f, 300.f / std::max(w, 1.f)), nullptr, &center);
    bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Projeto###painel_projeto", left);
    ImGui::DockBuilderDockWindow("Registro de atividades###painel_registro", bottom);
    ImGui::DockBuilderDockWindow("Área de trabalho###area", center);
    ImGui::DockBuilderFinish(id);
}

void App::draw_home() {
    const float W = ImGui::GetContentRegionAvail().x;
    ui::title("O QUE VAMOS FAZER?", 1.6f);
    ImGui::TextColored(COL_DIM, "Escolha uma opção ou arraste uma ROM .gb / .gbc para esta janela.");
    ImGui::Spacing();
    const float gap = 18;
    const bool two = W > 760;
    const float cw = two ? (W - gap) / 2 : W, ch = 150;
    auto next = [&](bool right) { if (two && right) ImGui::SameLine(0, gap); };
    if (big_card("play", "JOGAR ROM", "Roda o jogo original no ambiente de compatibilidade integrado. Isto é EMULAÇÃO: cada instrução é interpretada em tempo real. Jogar também ajuda a descobrir código para a conversão.", "EMULAÇÃO", ImGui::ColorConvertU32ToFloat4(ui::accent(3)), ImVec2(cw, ch))) go(ACT_PLAY);
    next(true);
    if (big_card("dec", "DESCOMPILAR", "Analisa o jogo: identifica código, funções, gráficos e músicas e gera um projeto editável (assembly RGBDS que remonta a ROM idêntica, folhas de tiles, capturas VGM).", "ANÁLISE", ImGui::ColorConvertU32ToFloat4(ui::accent(4)), ImVec2(cw, ch))) { close_player(); go(ACT_DECOMP); }
    if (big_card("conv", "CONVERTER", "Tradução estática SM83 -> C++ e compilação de um executável " NB_PLAT " nativo. Sem interpretar instruções quando a tradução está completa; a verificação compara tudo com o original.", "NATIVO", ImGui::ColorConvertU32ToFloat4(ui::accent(2)), ImVec2(cw, ch))) { close_player(); go(ACT_CONVERT); }
    next(true);
    if (big_card("mod", "MODIFICAR", "Editor visual: redesenhe gráficos, troque paletas, crie pacotes HD, cheats, mods e ganchos C++ para novas funções. Depois gere uma versão modernizada.", "EDITOR", ImGui::ColorConvertU32ToFloat4(ui::accent(1)), ImVec2(cw, ch))) { close_player(); go(ACT_MODIFY); }
    ImGui::Spacing();
    // below: shortcuts and the environment, side by side
    if (ImGui::BeginTable("##inicio_info", two ? 2 : 1, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ui::section("ATALHOS");
        static const char* sc[][2] = {{"Ctrl+O", "Abrir ROM"}, {"F5", "Jogar (emulação)"}, {"F6", "Descompilar"}, {"F7", "Converter para PC nativo"},
                                      {"F8", "Modificar"}, {"Ctrl+L", "Biblioteca"}, {"F11", "Tela cheia"}, {"F1", "Todos os atalhos"}};
        for (auto& k : sc) {
            ui::push_head(1.0f);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::theme_gb ? ui::accent(0) : ui::accent(3)), "%s", k[0]);
            ui::pop_font();
            ImGui::SameLine(110);
            ImGui::TextUnformatted(k[1]);
        }
        if (!show_proj_panel) {
            ui::section("RECENTES");
            for (auto& r : recent()) {
                if (!plat::dir_exists(r)) continue;
                Settings st; st.load(plat::join(r, "projeto.ini"));
                if (ImGui::Selectable((st.get("nome", plat::file_name(r)) + "##" + r).c_str())) open_project(r);
            }
        }
        ImGui::TableNextColumn();
        ui::section("AMBIENTE");
        if (tc.ok) { ImGui::TextColored(COL_OK, "Compilador integrado pronto"); ImGui::TextColored(COL_DIM, "%s", tc.version.c_str()); }
        else { ImGui::TextColored(COL_ERR, "Compilador indisponível"); ImGui::TextWrapped("%s", tc.problem.c_str()); }
        ImGui::TextColored(COL_DIM, "Projetos: %s", projects_root().c_str());
        ImGui::TextColored(COL_DIM, "Jogos exportados: %s", games_root().c_str());
        ImGui::TextColored(COL_DIM, "Versão %s", NB_VERSION);
        ImGui::EndTable();
    }
}

void App::draw_play(const SDL_Rect&) {
    if (!player) { screen = SC_HOME; return; }
    // toolbar of the work panel; the game is drawn in the rest of the panel
    ui::badge("EMULAÇÃO", IM_COL32(170, 110, 20, 255));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interpretador SM83: aqui o jogo original é emulado.\nO executável convertido (F7) roda em código nativo.");
    ImGui::SameLine();
    if (ImGui::Button(player->paused ? "Continuar" : "Pausar")) player->paused = !player->paused;
    ImGui::SameLine();
    if (ImGui::Button("Menu do jogo (Esc)")) player->menu_open = !player->menu_open;
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar")) player->reset();
    ImGui::SameLine();
    if (!vgm_recording) { if (ImGui::Button("Gravar música")) { player->m.apu->start_vgm(); vgm_recording = true; notify("Gravando a música... clique em Parar para salvar."); } }
    else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1)); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1)); if (ImGui::Button("Parar gravação")) stop_vgm(); ImGui::PopStyleColor(2); }
    ImGui::SameLine();
    if (ImGui::Button(show_dbg ? "Fechar depurador" : "Depurador")) show_dbg = !show_dbg;
    ImGui::SameLine();
    if (dumping) ImGui::TextColored(COL_INFO, "Tiles para HD: %zu", dumper.tiles.size());
    else {
        int n = 0; for (u8 v : play_trace) n += v;
        ImGui::TextColored(COL_DIM, "Código descoberto: %s instr.", fmt_num(n).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cada trecho do jogo que você alcança jogando é registrado\npara o conversor traduzir em código nativo.");
    }
    const float cw = ImGui::CalcTextSize("Fechar").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - cw));
    if (ImGui::Button("Fechar")) { close_player(); screen = SC_HOME; return; }
    // the game: the rest of the panel (in window pixels, for the SDL renderer)
    ImVec2 p = ImGui::GetCursorScreenPos(), av = ImGui::GetContentRegionAvail();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    play_area = {(int)(p.x - vp->Pos.x), (int)(p.y - vp->Pos.y), std::max(1, (int)av.x), std::max(1, (int)av.y)};
    ImGui::Dummy(av);
}

void App::draw_job_panel(float height) {
    ImGui::BeginChild("job", ImVec2(0, height), ImGuiChildFlags_Borders);
    std::vector<std::string> lines;
    float prog; std::string stage;
    { std::lock_guard<std::mutex> lk(job.mu); lines = job.lines; prog = job.progress; stage = job.stage; }
    if (job.running) {
        ImGui::Text("%s", job.title.c_str());
        ImGui::ProgressBar(prog, ImVec2(-120, 0), stage.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Cancelar")) job.cancel = true;
    } else if (!lines.empty()) {
        ImGui::TextColored(job.ok ? COL_OK : COL_ERR, "%s: %s", job.title.c_str(), job.ok ? "concluído" : "não concluído");
    } else ImGui::TextColored(COL_DIM, "Registro de atividades");
    ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& l : lines) {
        if (l.rfind("ERRO", 0) == 0) ImGui::TextColored(COL_ERR, "%s", l.c_str());
        else if (l.rfind("==", 0) == 0) ImGui::TextColored(COL_INFO, "%s", l.c_str());
        else ImGui::TextUnformatted(l.c_str());
    }
    if (job.running) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::EndChild();
}

void App::draw_report(const Report& r) {
    ImVec4 c = r.status == 0 ? COL_OK : r.status == 1 ? COL_WARN : r.status == 2 ? COL_ERR : COL_DIM;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::Indent(18);
    ImGui::Dummy(ImVec2(0, 2));
    ui::push_size(1.35f);
    ImGui::TextColored(c, "%s", r.title.c_str());
    ui::pop_size();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - 40);
    ImGui::TextUnformatted(r.summary.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Unindent(18);
    float y1 = ImGui::GetCursorScreenPos().y;
    dl->ChannelsSetCurrent(0);
    const ImVec4 bg = ui::theme_gb ? ImVec4(0.86f + c.x * 0.12f, 0.86f + c.y * 0.12f, 0.86f + c.z * 0.12f, 1) : ImVec4(c.x * 0.25f, c.y * 0.25f, c.z * 0.25f, 1);
    dl->AddRectFilled(p, ImVec2(p.x + w, y1), ImGui::ColorConvertFloat4ToU32(bg), ui::theme_gb ? 4.f : 10.f);
    dl->AddRectFilled(p, ImVec2(p.x + 6, y1), ImGui::ColorConvertFloat4ToU32(c), 10, ImDrawFlags_RoundCornersLeft);
    dl->ChannelsMerge();
    ImGui::Spacing();
    for (auto& it : r.items) {
        const char* icon = it.level == 0 ? "[OK]" : it.level == 1 ? "[!]" : it.level == 2 ? "[X]" : "[i]";
        ImVec4 ic = it.level == 0 ? COL_OK : it.level == 1 ? COL_WARN : it.level == 2 ? COL_ERR : COL_INFO;
        ImGui::TextColored(ic, "%s", icon);
        ImGui::SameLine(ui::base_size * 3.4f);
        ImGui::TextWrapped("%s", it.text.c_str());
    }
}

void App::draw_decomp() {
    if (!proj) { screen = SC_HOME; return; }
    ui::title("DESCOMPILAR", 1.4f);
    ImGui::SameLine(); ImGui::TextColored(COL_DIM, "  %s - %s", proj->name.c_str(), proj->info.mbc_name.c_str());
    if (job.running) { draw_job_panel(0); return; }
    if (!proj->an_valid) {
        if (proj->step() >= ST_ANALYZED && !job.running) {
            ImGui::Text("Carregando a análise do projeto...");
            run_decompile();
            return;
        }
        ImGui::TextWrapped("A ROM ainda não foi analisada. A análise executa o jogo por alguns minutos de tempo de jogo com entrada automática para encontrar o código, depois identifica funções, gráficos e músicas.");
        if (ImGui::Button("Analisar agora", ImVec2(220, 40))) run_decompile();
        draw_job_panel(0);
        return;
    }
    if (ImGui::BeginTabBar("dec", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        if (tab_item("Resumo")) {
            const RomInfo& ri = proj->info;
            ImGui::Columns(2, nullptr, false);
            ImGui::SeparatorText("Cabeçalho do cartucho");
            ImGui::Text("Título: %s", ri.title.c_str());
            ImGui::Text("Tipo: %s", ri.mbc_name.c_str());
            ImGui::Text("Sistema: %s", ri.cgb_only ? "somente GBC" : ri.cgb_supported ? "GBC + GB" : "GB");
            ImGui::Text("ROM: %d KB (%d bancos)  RAM: %d KB", (int)(proj->rom.size() / 1024), ri.rom_banks, ri.ram_size / 1024);
            ImGui::Text("Checksum do cabeçalho: %02X (%s)", ri.header_checksum, ri.header_checksum == ri.header_checksum_calc ? "correto" : "incorreto");
            ImGui::Text("Checksum global: %04X (%s)", ri.global_checksum, ri.global_checksum == ri.global_checksum_calc ? "correto" : "difere");
            ImGui::NextColumn();
            ImGui::SeparatorText("Resultados recuperados");
            int sound = 0, traced = 0;
            for (auto& f : proj->an.funcs) { sound += f.sound; traced += f.traced; }
            ImGui::Text("Instruções identificadas: %s", fmt_num(proj->an.n_instr).c_str());
            ImGui::Text("Funções: %s (%d executadas na descoberta)", fmt_num(proj->an.funcs.size()).c_str(), traced);
            ImGui::Text("Rotinas de som (escrevem nos registradores de áudio): %d", sound);
            ImGui::Text("Trechos de código copiados para RAM: %d", (int)proj->an.ram_code.size());
            ImGui::Text("Saltos indiretos (jp hl): %d", (int)proj->an.indirect.size());
            ImGui::Text("Tabelas de saltos reconhecidas: %d", (int)proj->an.jump_tables.size());
            ImGui::Text("Nomes do arquivo de símbolos: %d", (int)(proj->an.sym.rom.size() + proj->an.sym.ram.size()));
            ImGui::Columns(1);
            ImGui::SeparatorText("Arquivos do projeto editável");
            if (ImGui::Button("Abrir desmontagem (RGBDS)")) plat::open_in_explorer(proj->path("desmontagem"));
            ImGui::SameLine(); if (ImGui::Button("Abrir gráficos")) plat::open_in_explorer(proj->path("graficos"));
            ImGui::SameLine(); if (ImGui::Button("Abrir músicas")) plat::open_in_explorer(proj->path("musica"));
            ImGui::SameLine(); if (ImGui::Button("Abrir relatório")) plat::open_in_explorer(proj->path("analise/relatorio.txt"));
            ImGui::SameLine(); if (ImGui::Button("Pasta do projeto")) plat::open_in_explorer(proj->dir);
            ImGui::Spacing();
            if (ImGui::Button("Analisar mais (descoberta adicional de 2 min)")) {
                Project* p = proj.get();
                job.start("Descoberta adicional", [this, p](BuildLog& log, std::string& err) {
                    job.set_stage("Descoberta", 0, 0.7f);
                    if (!p->step_discover(7200, log, err)) return false;
                    job.set_stage("Análise", 0.7f, 0.3f);
                    return p->step_analyze(log, err);
                }, [this](bool ok) { disasm.clear(); if (!ok) error("Falha na análise", job.error); });
            }
            ImGui::SameLine();
            if (ImGui::Button("Jogar para descobrir mais código")) start_player(false);
            ImGui::SeparatorText("Nomes de funções e variáveis");
            ImGui::TextWrapped("Se você tem um arquivo de símbolos (.sym) do jogo (por exemplo, de um projeto de desmontagem da comunidade, do RGBDS, BGB ou no$gmb), "
                               "importe-o: as funções, rótulos e variáveis passam a aparecer com nomes legíveis na desmontagem, no pseudocódigo C e no código C++ gerado. "
                               "Um arquivo .sym com o mesmo nome da ROM, na mesma pasta, é importado automaticamente.");
            if (ImGui::Button("Importar arquivo de símbolos (.sym)...")) {
                std::string f = plat::open_file_dialog("Selecione o arquivo de símbolos", "Símbolos (*.sym)", "*.sym");
                if (!f.empty()) {
                    std::string txt; nb::Symbols test;
                    if (!plat::read_text(f, txt) || nb::load_sym_file(txt, proj->an.rom.size(), test) == 0)
                        error("Arquivo de símbolos", "Nenhum nome válido encontrado. O formato esperado é uma linha por símbolo: BANCO:ENDEREÇO Nome (ex.: 01:4A2F CarregarMapa).");
                    else if (!plat::copy_file(f, proj->path("analise/simbolos.sym"))) error("Arquivo de símbolos", "Não foi possível copiar o arquivo para o projeto.");
                    else {
                        Project* p = proj.get();
                        job.start("Aplicando os nomes", [p](BuildLog& log, std::string& err) { return p->step_analyze(log, err); },
                                  [this](bool ok) { disasm.clear(); sel_func = -1; if (!ok) error("Falha na análise", job.error); else notify("Nomes importados. Reconverta o jogo para levar os nomes ao código C++."); });
                    }
                }
            }
            if (plat::file_exists(proj->path("analise/simbolos.sym"))) {
                ImGui::SameLine();
                if (ImGui::Button("Remover símbolos")) {
                    plat::remove_file(proj->path("analise/simbolos.sym"));
                    Project* p = proj.get();
                    job.start("Removendo os nomes", [p](BuildLog& log, std::string& err) { return p->step_analyze(log, err); }, [this](bool) { disasm.clear(); sel_func = -1; });
                }
            }
            ImGui::EndTabItem();
        }
        if (tab_item("Funções e código")) {
            ImGui::BeginChild("flist", ImVec2(360, 0), ImGuiChildFlags_Borders);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filt", "filtrar (nome, banco, endereço)", func_filter, sizeof func_filter);
            std::string flt = func_filter;
            for (auto& c : flt) c = (char)toupper((unsigned char)c);
            ImGuiListClipper clip;
            std::vector<int> idx;
            for (int i = 0; i < (int)proj->an.funcs.size(); i++) {
                std::string n = proj->an.funcs[i].name;
                std::string up = n; for (auto& c : up) c = (char)toupper((unsigned char)c);
                if (flt.empty() || up.find(flt) != std::string::npos) idx.push_back(i);
            }
            clip.Begin((int)idx.size());
            while (clip.Step())
                for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
                    auto& f = proj->an.funcs[idx[k]];
                    char lb[96];
                    snprintf(lb, sizeof lb, "%s  (%d)%s##%d", f.name.c_str(), f.n_instr, f.traced ? "" : " *", idx[k]);
                    if (ImGui::Selectable(lb, sel_func == idx[k])) { sel_func = idx[k]; disasm = code_c ? nb::pseudo_c_lines(proj->an, idx[k], 4000) : nb::disasm_lines(proj->an, f.off, 600); }
                }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginGroup();
            bool prev = code_c;
            if (ImGui::RadioButton("Assembly (RGBDS)", !code_c)) code_c = false;
            ImGui::SameLine(); if (ImGui::RadioButton("Pseudocódigo C (leitura)", code_c)) code_c = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tradução legível de cada instrução para C, com nomes de variáveis, comparações e tabelas de saltos.\nServe para entender o jogo; o C++ que compila de verdade fica em src\\.");
            if (prev != code_c && sel_func >= 0 && sel_func < (int)proj->an.funcs.size())
                disasm = code_c ? nb::pseudo_c_lines(proj->an, sel_func, 4000) : nb::disasm_lines(proj->an, proj->an.funcs[sel_func].off, 600);
            ImGui::BeginChild("code", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            if (sel_func < 0) ImGui::TextColored(COL_DIM, "Selecione uma função para ver o código. (*) = não executada durante a descoberta.");
            for (auto& l : disasm) {
                if (!l.empty() && l.back() == ':') ImGui::TextColored(COL_INFO, "%s", l.c_str());
                else if (l.find("; ---") != std::string::npos || l.find("/*") != std::string::npos) ImGui::TextColored(COL_DIM, "%s", l.c_str());
                else ImGui::TextUnformatted(l.c_str());
            }
            ImGui::EndChild();
            ImGui::EndGroup();
            ImGui::EndTabItem();
        }
        if (tab_item("Gráficos")) {
            if (gfx_files.empty()) {
                gfx_files = plat::list_dir(proj->path("graficos"), false);
                std::stable_sort(gfx_files.begin(), gfx_files.end(), [](const std::string& a, const std::string& b) { return (a.rfind("vram", 0) == 0) > (b.rfind("vram", 0) == 0); });
            }
            ImGui::BeginChild("glist", ImVec2(260, 0), ImGuiChildFlags_Borders);
            for (int i = 0; i < (int)gfx_files.size(); i++)
                if (ImGui::Selectable(gfx_files[i].c_str(), sel_gfx == i)) sel_gfx = i;
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("gview", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            if (sel_gfx < (int)gfx_files.size()) {
                ImVec2 sz;
                SDL_Texture* t = texture_for(proj->path("graficos/" + gfx_files[sel_gfx]), sz);
                if (t) {
                    ImGui::TextColored(COL_DIM, "%s - %dx%d. vram_*: gráficos capturados durante o jogo. rom_banco_*: dados brutos da ROM (código avermelhado).", gfx_files[sel_gfx].c_str(), (int)sz.x, (int)sz.y);
                    float sc = std::max(1.0f, std::floor(std::min(4.0f, (ImGui::GetContentRegionAvail().x - 10) / sz.x)));
                    ImGui::Image(ui::tex_ref(t), ImVec2(sz.x * sc, sz.y * sc));
                }
            } else ImGui::TextColored(COL_DIM, "Nenhum gráfico.");
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (tab_item("Música")) {
            ImGui::TextWrapped("As músicas são extraídas gravando os comandos enviados ao chip de som em formato VGM (Video Game Music), que reproduz fielmente a trilha original em players como foobar2000 (plugin VGM), VGMPlay ou Winamp (in_vgm).");
            auto files = plat::list_dir(proj->path("musica"), false);
            for (auto& f : files) {
                std::vector<u8> d; plat::read_file(proj->path("musica/" + f), d);
                ImGui::BulletText("%s  (%s KB)", f.c_str(), fmt_num(d.size() / 1024).c_str());
            }
            if (files.empty()) ImGui::TextColored(COL_DIM, "Nenhuma captura ainda.");
            if (ImGui::Button("Gravar uma música jogando")) { start_player(false); notify("Use \"Gravar música (VGM)\" na barra superior."); }
            ImGui::SameLine();
            if (ImGui::Button("Abrir pasta de músicas")) plat::open_in_explorer(proj->path("musica"));
            ImGui::SeparatorText("Rotinas de som identificadas no código");
            int n = 0;
            for (auto& f : proj->an.funcs) if (f.sound) { ImGui::BulletText("%s (%d instruções)", f.name.c_str(), f.n_instr); if (++n > 200) break; }
            if (!n) ImGui::TextColored(COL_DIM, "Nenhuma rotina com escrita direta nos registradores de som foi identificada.");
            ImGui::EndTabItem();
        }
        if (tab_item("Registro")) { draw_job_panel(0); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

void App::draw_convert() {
    if (!proj) { screen = SC_HOME; return; }
    ui::title("CONVERTER PARA PC", 1.4f);
    ImGui::SameLine(); ImGui::TextColored(COL_DIM, "  %s", proj->name.c_str());
    bool busy = job.running;
    int st = proj->step();
    bool has_exe = plat::file_exists(proj->exe_path());
    Report rep = busy ? Report() : proj->make_report();

    ImGui::BeginChild("steps", ImVec2(420, 0), ImGuiChildFlags_Borders);
    struct S { const char* t; bool done; };
    S steps[7] = {{"1. ROM selecionada", true},
                  {"2. Converter para PC", st >= ST_GENERATED},
                  {"3. Análise e tradução", st >= ST_GENERATED},
                  {"4. Relatório de compatibilidade", proj->verify.ran},
                  {"5. Corrigir problemas", proj->verify.ran && proj->verify.missing.empty() && proj->verify.identical},
                  {"6. Compilar", has_exe},
                  {"7. Jogar no PC", has_exe}};
    for (auto& s : steps) { ImGui::TextColored(s.done ? COL_OK : COL_DIM, "%s", s.done ? "[OK]" : "[  ]"); ImGui::SameLine(60); ImGui::Text("%s", s.t); }
    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.38f, 1));
    if (ImGui::Button("CONVERTER PARA PC", ImVec2(-1, 48))) run_convert(false, 0);
    ImGui::PopStyleColor();
    ImGui::TextColored(COL_DIM, "Faz tudo automaticamente: descoberta, tradução,\ncompilação, verificação e correção.");
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("rodadas de correção automática", &fix_iters, 0, 5);
    ImGui::SeparatorText("Corrigir problemas");
    if (ImGui::Button("Corrigir automaticamente", ImVec2(-1, 0))) {
        if (!proj->verify.missing.empty()) proj->add_entries(proj->verify.missing);
        run_convert(false, 0);
    }
    if (ImGui::Button("Descoberta mais longa (5 min de jogo)", ImVec2(-1, 0))) run_convert(true, 18000);
    {
        bool ex = proj->ini.getb("explorar", true);
        if (ImGui::Checkbox("Exploração automática (acha mais código)", &ex)) { proj->ini.set("explorar", ex ? "1" : "0"); proj->save(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Depois de jogar sozinho, o Studio volta a momentos salvos e tenta outros jeitos de jogar\n(andar, apertar botões, abrir menus), guardando os momentos que revelam código novo.\nMais código descoberto = menos partes emuladas no executável.");
    }
    if (ImGui::Button("Jogar para descobrir código (emulação)", ImVec2(-1, 0))) start_player(false);
    if (ImGui::Button("Importar log do jogo (missing_code.txt)", ImVec2(-1, 0))) {
        std::string f = plat::open_file_dialog("Selecione config/missing_code.txt do jogo exportado", "Registro de código", "*.txt");
        if (!f.empty()) { std::string err; int n = proj->import_missing(f, err); if (n < 0) error("Falha ao importar", err); else notify(std::to_string(n) + " novos pontos de entrada importados. Clique em CONVERTER."); }
    }
    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##me", "BB:AAAA", manual_entry, sizeof manual_entry);
    ImGui::SameLine();
    if (ImGui::Button("Adicionar entrada manual")) {
        int n = proj->add_entries({manual_entry});
        notify(n ? "Ponto de entrada adicionado." : "Formato inválido ou já existente (use banco:endereço, ex.: 01:4A3C).");
    }
    ImGui::SeparatorText("Compilar e jogar");
    if (ImGui::Button("Compilar novamente", ImVec2(-1, 0))) run_build_only();
    ImGui::BeginDisabled(!has_exe);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.75f, 1));
    if (ImGui::Button("JOGAR NO PC (executável nativo)", ImVec2(-1, 44))) {
        if (!plat::launch(proj->exe_path(), "", proj->path("build"))) error("Não foi possível iniciar o jogo", "Execute manualmente: " + proj->exe_path());
    }
    ImGui::PopStyleColor();
    {
        bool one = proj->ini.getb("exe_unico", true);
        if (ImGui::Checkbox("Gerar também um executável único (um arquivo só, fácil de compartilhar)", &one)) { proj->ini.set("exe_unico", one ? "1" : "0"); proj->save(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Além da pasta do jogo, cria \"Meus Jogos/Executável único/%s\": um único arquivo com o jogo,\nas imagens, as músicas e as configurações dentro. Ao abrir, ele se instala sozinho na pasta de dados\ndo usuário (onde ficam os saves). Para usar num pendrive, crie ao lado dele uma pasta \"<nome> - dados\".", (proj->exe_name + tc.exe_suffix()).c_str());
    }
    {
        bool inst = proj->ini.getb("instalador", true);
        if (ImGui::Checkbox("Gerar também um instalador (Windows: Instalar.exe; Linux: pacote .deb)", &inst)) { proj->ini.set("instalador", inst ? "1" : "0"); proj->save(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cria \"Meus Jogos/Instaladores\" com um instalador de verdade para o jogo:\n- Windows: instala para o usuário (sem administrador), cria atalhos e aparece em \"Aplicativos instalados\" com desinstalador.\n- Linux: pacote .deb que instala em /opt e aparece no menu de aplicativos; os saves ficam em ~/.local/share/nativeboy.");
    }
    if (ImGui::Button("Exportar para Meus Jogos", ImVec2(-1, 0))) run_export(false);
    {
        std::string one = proj->ini.get("exe_unico_arquivo", "");
        if (!one.empty() && plat::file_exists(one) && ImGui::Button("Abrir pasta do executável único", ImVec2(-1, 0))) plat::open_in_explorer(plat::parent(one));
        std::string ins = proj->ini.get(tc.win_target() ? "instalador_windows" : "instalador_linux", "");
        if (!ins.empty() && plat::file_exists(ins) && ImGui::Button("Abrir pasta dos instaladores", ImVec2(-1, 0))) plat::open_in_explorer(plat::parent(ins));
    }
#ifndef _WIN32
    if (ImGui::Button("Exportar para Meus Jogos + versão Windows (.exe)", ImVec2(-1, 0))) {
        if (!tcw.ok && !tcw.detect_cross_windows(tc, user_dir)) error("Versão para Windows", tcw.problem);
        else run_export(true);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Compila o mesmo código traduzido também para Windows 10/11 64 bits, com o MinGW-w64.\nO resultado vai para \"Meus Jogos/%s (Windows)\" e pode ser copiado para um PC com Windows.", proj->name.c_str());
#endif
    // ---- Android
    if (!gh_loaded) {
        gh_loaded = true;
        const std::string t = prefs.get("github_token", "");
        gh_remember = !t.empty();
        strncpy(gh_token, t.c_str(), sizeof gh_token - 1);
    }
    ImGui::SetNextItemOpen(android_open, ImGuiCond_Once);
    if ((android_open = ImGui::CollapsingHeader("Celular Android (APK)"))) {
        ImGui::TextWrapped("Gera um aplicativo Android (.apk) do jogo, com controles na tela, que funciona em pé e deitado. "
                           "O APK é compilado de graça pelo GitHub, num repositório PRIVADO da sua conta.");
        ImGui::TextColored(COL_DIM, "1. Crie uma chave de acesso (marque \"repo\" e \"workflow\"):");
        if (ImGui::SmallButton("Abrir a página do GitHub para criar a chave"))
            plat::open_url("https://github.com/settings/tokens/new?scopes=repo,workflow&description=NativeBoy%20Studio");
        ImGui::TextColored(COL_DIM, "2. Cole a chave aqui:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##ghtoken", "ghp_...", gh_token, sizeof gh_token, ImGuiInputTextFlags_Password);
        ImGui::Checkbox("Lembrar a chave neste computador", &gh_remember);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A chave fica gravada nas preferências do Studio, só neste computador.\nDesmarque em computadores compartilhados.");
        if (ImGui::Button("Gerar APK no GitHub", ImVec2(-1, 0))) run_android(true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Envia o projeto Android para o repositório privado \"%s\" e espera o GitHub compilar\n(uns 10 minutos na primeira vez, menos depois). O APK vai para \"Meus Jogos/Android\".\nPara instalar: copie o APK para o celular e abra (permita \"instalar apps desconhecidos\").", apk_repo_name(proj->name).c_str());
        if (ImGui::Button("Só criar o projeto Android (para o Android Studio)", ImVec2(-1, 0))) run_android(false);
        const std::string apk = proj->ini.get("apk_arquivo", ""), repo = proj->ini.get("apk_repositorio", "");
        if (!apk.empty() && plat::file_exists(apk) && ImGui::Button("Abrir pasta do APK", ImVec2(-1, 0))) plat::open_in_explorer(plat::parent(apk));
        if (!repo.empty() && ImGui::SmallButton("Ver o repositório no GitHub")) plat::open_url(repo);
    }
    if (ImGui::Button("Abrir pasta do executável", ImVec2(-1, 0))) plat::open_in_explorer(proj->path("build"));
    if (ImGui::Button("Gerar pacote de diagnóstico (.zip)", ImVec2(-1, 0))) make_diagnostics();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Junta relatórios, registros de compilação e verificação e dados do sistema num .zip\npara enviar a quem for ajudar. Não inclui a ROM nem o código do jogo.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", ImVec2(0, 0));
    if (busy) draw_job_panel(0);
    else {
        ImGui::BeginChild("rep", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.6f), ImGuiChildFlags_Borders);
        if (!proj->an_valid && proj->step() >= ST_ANALYZED) {
            Project* p = proj.get();
            job.start("Carregando a análise do projeto", [p](BuildLog& log, std::string& err) { return p->step_analyze(log, err); }, nullptr);
        }
        draw_report(rep);
        if (proj->verify.ran && !proj->verify.serial.empty()) { ImGui::SeparatorText("Saída serial do jogo"); ImGui::TextWrapped("%s", proj->verify.serial.c_str()); }
        if (!proj->ini.get("pasta_exportada", "").empty()) { ImGui::Spacing(); ImGui::TextColored(COL_OK, "Exportado em: %s", proj->ini.get("pasta_exportada", "").c_str()); }
        ImGui::EndChild();
        draw_job_panel(0);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------- modify
static const u32 ED_SHADES[4] = {0xFFFFFFFF, 0xFFA8A8A8, 0xFF545454, 0xFF000000};

void App::draw_modify() {
    if (!proj) { screen = SC_HOME; return; }
    if (!ed_loaded) { ed_rom = proj->rom; ed_loaded = true; ed_dirty = false; ed_tex_dirty = true; }
    if (!texts_loaded) {
        cheats_text.clear(); hooks_text.clear(); hooks_list.clear();
        plat::read_text(proj->path("mods/cheats.txt"), cheats_text);
        if (!plat::read_text(proj->path("mods/hooks.cpp"), hooks_text))
            hooks_text =
                "// Ganchos C++ do seu jogo (opcional). Compilados junto com o código traduzido.\n"
                "// Exemplo: deixar a vida sempre cheia escrevendo na RAM a cada quadro.\n"
                "#include \"gb.h\"\n#include \"cpu_ops.h\"\nusing namespace gb;\n\n"
                "void nb_user_init(Machine& m) {\n    // chamado uma vez, quando o jogo inicia\n}\n\n"
                "void nb_user_frame(Machine& m) {\n    // chamado a cada quadro (59,7 vezes por segundo)\n    // exemplo: wr(m, 0xC0A0, 3);\n}\n\n"
                "// Ganchos por endereço: liste em 'Ganchos por endereço' (ex.: 01:4A3C meu_gancho)\n"
                "// bool meu_gancho(Machine& m) { /* ... */ return false; /* true = você alterou m.pc */ }\n";
        plat::read_text(proj->path("mods/ganchos.txt"), hooks_list);
        texts_loaded = true;
    }
    ui::title("MODIFICAR JOGO", 1.4f);
    ImGui::SameLine(); ImGui::TextColored(COL_DIM, "  %s%s", proj->name.c_str(), proj->rom_modified() ? "  (ROM de trabalho modificada)" : "");
    if (job.running) { draw_job_panel(0); return; }
    int nb = (int)(ed_rom.size() / 0x4000);
    if (ImGui::BeginTabBar("mod", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        if (tab_item("Assistente")) { draw_mod_assistant(*proj, studio_msg(), g_goto_tab); ImGui::EndTabItem(); }
        if (tab_item("Editor de sprites")) { draw_sprite_editor(*proj, studio_msg()); ImGui::EndTabItem(); }
        if (tab_item("Gráficos (tiles)")) {
            ImGui::SetNextItemWidth(160); ImGui::SliderInt("Banco", &ed_bank, 0, nb - 1); ImGui::SameLine();
            ImGui::SetNextItemWidth(160); if (ImGui::SliderInt("Bloco de 4 KB", &ed_block, 0, 3)) ed_tex_dirty = true;
            ImGui::SameLine();
            static int last_bank = -1;
            if (last_bank != ed_bank) { ed_tex_dirty = true; last_bank = ed_bank; }
            size_t base = (size_t)ed_bank * 0x4000 + ed_block * 0x1000;
            if (!ed_tex) { ed_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 128); SDL_SetTextureScaleMode(ed_tex, SDL_ScaleModeNearest); }
            if (ed_tex_dirty) {
                std::vector<u32> px(128 * 128);
                for (int t = 0; t < 256; t++)
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x++) {
                            const u8* d = &ed_rom[base + t * 16 + y * 2];
                            int ci = (((d[1] >> (7 - x)) & 1) << 1) | ((d[0] >> (7 - x)) & 1);
                            px[((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x] = ED_SHADES[ci];
                        }
                SDL_UpdateTexture(ed_tex, nullptr, px.data(), 128 * 4);
                ed_tex_dirty = false;
            }
            if (ImGui::Button("Próxima área com gráficos >>") && proj->an_valid) {
                size_t cur = (size_t)ed_bank * 0x4000 + ed_block * 0x1000;
                for (size_t b = cur + 0x1000; b + 0x1000 <= ed_rom.size(); b += 0x1000) {
                    int code = 0, ff = 0;
                    for (size_t i = 0; i < 0x1000; i++) { if (proj->an.fl[b + i] & nb::AF_BODY) code++; if (ed_rom[b + i] == 0xFF || ed_rom[b + i] == 0) ff++; }
                    if (code < 0x100 && ff < 0xC00) { ed_bank = (int)(b / 0x4000); ed_block = (int)((b % 0x4000) / 0x1000); ed_tex_dirty = true; break; }
                }
            }
            if (!proj->an_valid) { ImGui::SameLine(); ImGui::TextColored(COL_DIM, "(descompile a ROM para localizar gráficos automaticamente)"); }
            ImGui::Text("Endereço: banco %02X, $%04X-$%04X", ed_bank, (unsigned)((base % 0x4000) + (ed_bank ? 0x4000 : 0)), (unsigned)((base % 0x4000) + (ed_bank ? 0x4000 : 0) + 0xFFF));
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::Image(ui::tex_ref(ed_tex), ImVec2(384, 384));
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                ImVec2 mp = ImGui::GetMousePos();
                int tx = (int)((mp.x - p0.x) / 24), ty = (int)((mp.y - p0.y) / 24);
                if (tx >= 0 && tx < 16 && ty >= 0 && ty < 16) ed_tile = ty * 16 + tx;
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 sp(p0.x + (ed_tile % 16) * 24, p0.y + (ed_tile / 16) * 24);
            dl->AddRect(sp, ImVec2(sp.x + 24, sp.y + 24), IM_COL32(255, 60, 60, 255), 0.0f, 0, 2.0f);
            ImGui::SameLine();
            ImGui::BeginGroup();
            size_t toff = base + ed_tile * 16;
            ImGui::Text("Tile %d (ROM $%06X)", ed_tile, (unsigned)toff);
            bool is_code = false;
            if (proj->an_valid) for (int i = 0; i < 16; i++) if (proj->an.fl[toff + i] & nb::AF_BODY) is_code = true;
            if (is_code) ImGui::TextColored(COL_WARN, "Atenção: estes bytes foram identificados como CÓDIGO.\nEditar pode quebrar o jogo.");
            ImVec2 e0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("pix", ImVec2(256, 256));
            bool hov = ImGui::IsItemHovered();
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    const u8* d = &ed_rom[toff + y * 2];
                    int ci = (((d[1] >> (7 - x)) & 1) << 1) | ((d[0] >> (7 - x)) & 1);
                    u32 c = ED_SHADES[ci];
                    dl->AddRectFilled(ImVec2(e0.x + x * 32, e0.y + y * 32), ImVec2(e0.x + x * 32 + 31, e0.y + y * 32 + 31),
                                      IM_COL32((c >> 16) & 255, (c >> 8) & 255, c & 255, 255));
                }
            if (hov && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
                ImVec2 mp = ImGui::GetMousePos();
                int x = (int)((mp.x - e0.x) / 32), y = (int)((mp.y - e0.y) / 32);
                int col = ImGui::IsMouseDown(1) ? 0 : ed_color;
                if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                    u8& lo = ed_rom[toff + y * 2]; u8& hi = ed_rom[toff + y * 2 + 1];
                    u8 bit = (u8)(1 << (7 - x));
                    u8 nlo = (col & 1) ? (lo | bit) : (lo & ~bit), nhi = (col & 2) ? (hi | bit) : (hi & ~bit);
                    if (nlo != lo || nhi != hi) { lo = nlo; hi = nhi; ed_dirty = true; ed_tex_dirty = true; }
                }
            }
            ImGui::Text("Cor:");
            for (int i = 0; i < 4; i++) {
                ImGui::SameLine();
                u32 c = ED_SHADES[i];
                ImVec4 cv(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1);
                ImGui::PushID(i);
                if (ImGui::ColorButton("c", cv, 0, ImVec2(ed_color == i ? 34 : 26, ed_color == i ? 34 : 26))) ed_color = i;
                ImGui::PopID();
            }
            ImGui::TextColored(COL_DIM, "Botão esquerdo pinta, direito apaga (cor 0).");
            ImGui::EndGroup();
            ImGui::Spacing();
            if (ImGui::Button("Exportar este bloco como PNG")) {
                std::string f = plat::save_file_dialog("Salvar folha de tiles", "Imagem PNG", "*.png", "tiles_banco" + std::to_string(ed_bank) + "_" + std::to_string(ed_block) + ".png");
                if (!f.empty()) {
                    std::vector<u32> px(128 * 128);
                    for (int t = 0; t < 256; t++) for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                        const u8* d = &ed_rom[base + t * 16 + y * 2];
                        int ci = (((d[1] >> (7 - x)) & 1) << 1) | ((d[0] >> (7 - x)) & 1);
                        px[((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x] = ED_SHADES[ci];
                    }
                    if (save_png(f, px.data(), 128, 128)) notify("Folha exportada. Edite em qualquer programa de desenho usando 4 tons de cinza.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Importar PNG para este bloco")) {
                std::string f = plat::open_file_dialog("Importar folha de tiles (128x128, 4 tons)", "Imagem PNG", "*.png");
                std::vector<u32> px; int w, h;
                if (!f.empty()) {
                    if (!load_png(f, px, w, h) || w != 128 || h != 128) error("Imagem inválida", "A imagem precisa ter exatamente 128x128 pixels (16x16 tiles).");
                    else {
                        for (int t = 0; t < 256; t++) for (int y = 0; y < 8; y++) {
                            u8 lo = 0, hi = 0;
                            for (int x = 0; x < 8; x++) {
                                u32 c = px[((t / 16) * 8 + y) * 128 + (t % 16) * 8 + x];
                                int lum = (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) / 10;
                                int ci = lum > 212 ? 0 : lum > 126 ? 1 : lum > 42 ? 2 : 3;
                                if (ci & 1) lo |= (u8)(1 << (7 - x));
                                if (ci & 2) hi |= (u8)(1 << (7 - x));
                            }
                            ed_rom[base + t * 16 + y * 2] = lo; ed_rom[base + t * 16 + y * 2 + 1] = hi;
                        }
                        ed_dirty = true; ed_tex_dirty = true; notify("Folha importada (lembre-se de salvar).");
                    }
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!ed_dirty);
            if (ImGui::Button("Salvar alterações na ROM de trabalho")) {
                if (proj->write_working_rom(ed_rom, "edição de gráficos")) { ed_dirty = false; notify("Alterações salvas (backup criado). Converta novamente para gerar o jogo modificado."); }
                else error("Falha ao salvar", "Não foi possível gravar a ROM de trabalho.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Descartar")) { ed_rom = proj->rom; ed_dirty = false; ed_tex_dirty = true; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Testar (emulação)")) { if (ed_dirty) proj->write_working_rom(ed_rom, "teste de edição"), ed_dirty = false; start_player(false); }
            ImGui::EndTabItem();
        }
        if (tab_item("Cores e melhorias")) {
            auto swatch = [](const char* id, uint32_t c) {
                ImGui::ColorButton(id, ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1), ImGuiColorEditFlags_NoTooltip, ImVec2(22, 20));
            };
            if (proj->info.cgb_supported) ImGui::TextWrapped("Jogos de Game Boy Color definem as próprias cores. O jogador pode ligar ou desligar a correção de cor do LCD no menu do jogo.");
            else {
                ImGui::TextWrapped("Paleta padrão do jogo exportado (o jogador pode trocar no menu). As paletas coloridas pintam o cenário e os personagens com cores diferentes.");
                std::string cur = proj->ini.get("paleta_padrao", "classico");
                if (ImGui::BeginTable("pals", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Paleta"); ImGui::TableSetupColumn("Cenário"); ImGui::TableSetupColumn("Sprites 1"); ImGui::TableSetupColumn("Sprites 2");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < DMG_PALETTE_COUNT; i++) {
                        const DmgPalette& p = DMG_PALETTES[i];
                        ImGui::PushID(i);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGui::RadioButton(p.name, cur == p.id)) { proj->ini.set("paleta_padrao", p.id); proj->save(); }
                        const uint32_t* sets[3] = {p.bg, p.obj0, p.obj1};
                        for (int s3 = 0; s3 < 3; s3++) {
                            ImGui::TableNextColumn();
                            for (int k = 0; k < 4; k++) { ImGui::PushID(s3 * 4 + k); swatch("##c", sets[s3][k]); ImGui::PopID(); if (k < 3) ImGui::SameLine(0, 2); }
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::SeparatorText("Paleta personalizada");
                static uint32_t cc[12];
                static bool cc_init = false;
                if (!cc_init) {
                    if (!palette_from_string(proj->ini.get("cores_personalizadas", ""), cc)) {
                        const DmgPalette& d = DMG_PALETTES[7];
                        memcpy(cc, d.bg, 16); memcpy(cc + 4, d.obj0, 16); memcpy(cc + 8, d.obj1, 16);
                    }
                    cc_init = true;
                }
                static const char* layer[3] = {"Cenário", "Sprites 1", "Sprites 2"};
                bool changed = false;
                for (int l = 0; l < 3; l++) {
                    ImGui::Text("%-10s", layer[l]);
                    for (int k = 0; k < 4; k++) {
                        ImGui::SameLine(120 + k * 44);
                        float col[3] = {((cc[l * 4 + k] >> 16) & 255) / 255.f, ((cc[l * 4 + k] >> 8) & 255) / 255.f, (cc[l * 4 + k] & 255) / 255.f};
                        ImGui::PushID(l * 4 + k);
                        if (ImGui::ColorEdit3("##e", col, ImGuiColorEditFlags_NoInputs)) {
                            cc[l * 4 + k] = 0xFF000000u | ((u32)(col[0] * 255) << 16) | ((u32)(col[1] * 255) << 8) | (u32)(col[2] * 255);
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::TextColored(COL_DIM, "Da cor mais clara (esquerda) para a mais escura. Clique em um quadrado para escolher a cor.");
                if (changed) { proj->ini.set("cores_personalizadas", palette_to_string(cc)); proj->save(); }
                if (ImGui::RadioButton("Usar a paleta personalizada como padrão", cur == "personalizada")) {
                    proj->ini.set("cores_personalizadas", palette_to_string(cc)); proj->ini.set("paleta_padrao", "personalizada"); proj->save();
                }
            }
            ImGui::SeparatorText("Melhorias ligadas por padrão no jogo exportado");
            ImGui::TextColored(COL_DIM, "O jogador pode mudar tudo no menu do jogo, e o modo fidelidade desliga todas de uma vez.");
            auto flag = [&](const char* label, const char* key, const char* tip) {
                bool v = proj->ini.getb(key, false);
                if (ImGui::Checkbox(label, &v)) { proj->ini.set(key, v ? "1" : "0"); proj->save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            };
            {   // profile for this game
                std::string pf = proj->ini.get("perfil", "");
                std::string on = proj->ini.get("nome_oficial", "");
                if (!on.empty()) ImGui::TextColored(COL_DIM, "Jogo reconhecido: %s", on.c_str());
                {
                    std::string d;
                    for (const char* k : {"genero", "ano", "editora", "jogadores"}) {
                        std::string v = proj->ini.get(k, "");
                        if (v.empty()) continue;
                        if (!strcmp(k, "jogadores")) v = v == "1" ? "1 jogador" : "1 a " + v + " jogadores";
                        d += (d.empty() ? "" : "  ·  ") + v;
                    }
                    if (!d.empty()) ImGui::TextColored(COL_DIM, "%s", d.c_str());
                }
                const GameProfile* gp = find_profile(proj->info.title, app_dir, &proj->rom);
                if (!pf.empty()) {
                    ImGui::TextColored(COL_OK, "Perfil do jogo: %s", pf.c_str());
                    ImGui::SameLine(); ImGui::TextColored(COL_DIM, "(%s)", profile_kind_name(proj->ini.get("perfil_tipo", "")));
                    if (!proj->ini.getb("perfil_testado", false)) ImGui::TextColored(COL_DIM, "Este perfil segue o tipo do jogo e ainda não foi conferido com o jogo real: teste e ajuste abaixo se precisar.");
                } else if (gp) ImGui::TextColored(COL_DIM, "Há um perfil para este jogo: %s", gp->name.c_str());
                else ImGui::TextColored(COL_DIM, "Sem perfil específico para este jogo (ajustes gerais). Você pode criar um em perfis_jogos.txt.");
                if (gp) { ImGui::SameLine(); if (ImGui::SmallButton(pf.empty() ? "Aplicar perfil" : "Reaplicar perfil")) { apply_profile(*gp, proj->ini); proj->save(); notify("Perfil aplicado: " + gp->name); } }
            }
            flag("Tela widescreen 16:9 (experimental)", "padrao_widescreen", "Mostra mais do cenário dos lados. Teste no modo Jogar ROM: em alguns jogos as bordas mostram trechos ainda não carregados.");
            if (proj->ini.getb("padrao_widescreen", false)) {
                static const char* edges[] = {"Mostrar tudo", "Esconder trechos antigos (recomendado)", "Esconder e esmaecer as bordas"};
                int ev = std::max(0, std::min(2, proj->ini.geti("padrao_bordas_wide", 1)));
                ImGui::SetNextItemWidth(320);
                if (ImGui::Combo("Bordas do widescreen", &ev, edges, 3)) { proj->ini.seti("padrao_bordas_wide", ev); proj->save(); }
                bool mem = proj->ini.getb("padrao_memoria_cenario", true), hud = proj->ini.getb("padrao_hud_largo", true);
                if (ImGui::Checkbox("Memória do cenário (lembra o que já passou pela tela)", &mem)) { proj->ini.set("padrao_memoria_cenario", mem ? "1" : "0"); proj->save(); }
                if (ImGui::Checkbox("Estender barras de status na largura toda", &hud)) { proj->ini.set("padrao_hud_largo", hud ? "1" : "0"); proj->save(); }
                {   // the window of the exported game drawn as a handheld
                    static const char* vci[] = {"desligado", "gb", "gbc"};
                    static const char* vcn[] = {"Janela normal", "Visual de console: clássico (estilo Game Boy)", "Visual de console: Color (estilo Game Boy Color)"};
                    int vc = 0; for (int i = 0; i < 3; i++) if (proj->ini.get("padrao_visual_console", "desligado") == vci[i]) vc = i;
                    ImGui::SetNextItemWidth(420);
                    if (ImGui::Combo("##visual_console", &vc, vcn, 3)) { proj->ini.set("padrao_visual_console", vci[vc]); proj->save(); }
                    if (vc) {
                        static const char* cci[] = {"", "cinza", "roxo_translucido", "roxo", "amarelo", "azul_petroleo", "vermelho", "verde", "preto"};
                        static const char* ccn[] = {"Cor padrão do estilo", "Cinza clássico", "Roxo translúcido", "Roxo", "Amarelo", "Azul-petróleo", "Vermelho", "Verde", "Preto"};
                        int cc = 0; for (int i = 0; i < 9; i++) if (proj->ini.get("padrao_console_cor", "") == cci[i]) cc = i;
                        ImGui::SetNextItemWidth(260);
                        if (ImGui::Combo("Cor do aparelho##cc", &cc, ccn, 9)) { proj->ini.set("padrao_console_cor", cci[cc]); proj->save(); }
                    }
                    bool an = proj->ini.getb("padrao_animacao_ligar", true);
                    if (ImGui::Checkbox("Ligar e desligar como o aparelho (animação e som ao abrir e fechar)", &an)) { proj->ini.set("padrao_animacao_ligar", an ? "1" : "0"); proj->save(); }
                }
                bool uwd = proj->ini.getb("padrao_ultrawide", false);
                if (ImGui::Checkbox("Ultrawide (até 352 pixels, usando a memória do cenário)", &uwd)) { proj->ini.set("padrao_ultrawide", uwd ? "1" : "0"); proj->save(); }
                static const char* rown[] = {"Expansão vertical desligada", "16 linhas a mais acima e abaixo", "32 linhas a mais acima e abaixo"};
                int rw = proj->ini.geti("padrao_linhas_extras", 0) >= 32 ? 2 : proj->ini.geti("padrao_linhas_extras", 0) >= 16 ? 1 : 0;
                ImGui::SetNextItemWidth(320);
                if (ImGui::Combo("##linhas", &rw, rown, 3)) { proj->ini.seti("padrao_linhas_extras", rw * 16); proj->save(); }
                bool smw = proj->ini.getb("padrao_wide_inteligente", true);
                if (ImGui::Checkbox("Widescreen inteligente (volta à largura original em menus e telas com restos nas laterais)", &smw)) { proj->ini.set("padrao_wide_inteligente", smw ? "1" : "0"); proj->save(); }
            }
            flag("Sem limite de sprites", "padrao_sem_limite_sprites", "Desenha todos os sprites de cada linha, reduzindo as piscadas.");
            flag("Rastro do LCD (ghosting)", "padrao_rastro_lcd", "Imita a tela lenta do aparelho original.");
            flag("Sombra sob os personagens", "padrao_sombra_sprites", "Sprites projetam uma sombra suave no cenário.");
            flag("Vinheta (cantos mais escuros)", "padrao_vinheta", "Escurece de leve os cantos da imagem.");
            {
                int cv = proj->ini.geti("padrao_cores_vivas", 0), br = proj->ini.geti("padrao_brilho", 0);
                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderInt("Cores vivas (padrão)", &cv, 0, 100)) proj->ini.seti("padrao_cores_vivas", cv);
                if (ImGui::IsItemDeactivatedAfterEdit()) proj->save();
                ImGui::SetNextItemWidth(220);
                if (ImGui::SliderInt("Brilho das luzes (padrão)", &br, 0, 100)) proj->ini.seti("padrao_brilho", br);
                if (ImGui::IsItemDeactivatedAfterEdit()) proj->save();
            }
            {
                bool ti = proj->ini.getb("padrao_tela_inicial", true);
                if (ImGui::Checkbox("Tela inicial com menu (Continuar / Novo jogo / Opções)", &ti)) { proj->ini.set("padrao_tela_inicial", ti ? "1" : "0"); proj->save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("O jogo exportado abre numa tela inicial como um jogo de PC, usando a imagem de abertura ou o ícone de fundo.");
            }
            if (proj->info.sgb_flag == 0x03 && !proj->info.cgb_supported) {
                ImGui::SeparatorText("Super Game Boy");
                ImGui::TextWrapped("Este jogo foi feito com suporte ao Super Game Boy: ele mesmo escolhe cores para cada tela e desenha uma moldura em volta da imagem.");
                bool v = proj->ini.getb("sgb", true);
                if (ImGui::Checkbox("Modo Super Game Boy (cores e moldura do próprio jogo)", &v)) {
                    proj->ini.set("sgb", v ? "1" : "0");
                    if (proj->step() > ST_IMPORTED) proj->ini.seti("etapa", ST_IMPORTED);   // the game takes a different code path: rediscover
                    proj->save();
                }
                ImGui::TextColored(COL_WARN, "Mudar esta opção exige converter de novo (o jogo executa outro código no modo SGB).");
                bool b = proj->ini.getb("padrao_sgb_borda", true);
                if (ImGui::Checkbox("Mostrar a moldura por padrão", &b)) { proj->ini.set("padrao_sgb_borda", b ? "1" : "0"); proj->save(); }
            }
            {
                static const char* side_ids[] = {"desfocado", "bordas", "moldura", "imagem", "preto"};
                static const char* side_names[] = {"Luz ambiente (imagem desfocada)", "Estender as bordas", "Moldura de console", "Imagem de fundo (capa)", "Preto"};
                std::string cur = proj->ini.get("padrao_fundo_laterais", "desfocado");
                int si = 0; for (int i = 0; i < 5; i++) if (cur == side_ids[i]) si = i;
                ImGui::SetNextItemWidth(320);
                if (ImGui::Combo("Laterais da tela", &si, side_names, 5)) { proj->ini.set("padrao_fundo_laterais", side_ids[si]); proj->save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("O que aparece em volta do jogo quando a janela é maior ou mais larga que a imagem.");
                if (si == 2) {
                    static const char* th_ids[] = {"cinza", "roxo", "preto", "verde", "vermelho", "azul"};
                    static const char* th_names[] = {"Cinza clássico", "Roxo", "Preto", "Verde", "Vermelho", "Azul"};
                    std::string ct = proj->ini.get("padrao_moldura_cor", "cinza");
                    int ti = 0; for (int i = 0; i < 6; i++) if (ct == th_ids[i]) ti = i;
                    ImGui::SetNextItemWidth(320);
                    if (ImGui::Combo("Cor da moldura", &ti, th_names, 6)) { proj->ini.set("padrao_moldura_cor", th_ids[ti]); proj->save(); }
                }
                if (si == 3) {
                    bool has = plat::file_exists(proj->path("mods/fundo.png"));
                    ImGui::TextColored(COL_DIM, "%s", has ? "Imagem escolhida: mods/fundo.png (vai junto no jogo exportado)." : "Nenhuma imagem: o jogo usa a luz ambiente.");
                    if (ImGui::Button("Escolher imagem de capa (PNG)...")) {
                        std::string f = plat::open_file_dialog("Imagem de fundo das laterais", "Imagem PNG", "*.png");
                        std::vector<uint32_t> px; int w, h;
                        if (!f.empty() && load_png(f, px, w, h)) { plat::copy_file(f, proj->path("mods/fundo.png")); notify("Imagem de capa definida."); }
                        else if (!f.empty()) error("Imagem de fundo", "Não foi possível ler a imagem. Use um arquivo PNG.");
                    }
                    if (has) { ImGui::SameLine(); if (ImGui::Button("Remover imagem##capa")) plat::remove_file(proj->path("mods/fundo.png")); }
                }
            }
            const char* const* fids = FILTER_IDS;
            std::string fcur = proj->ini.get("padrao_filtro", "nitido");
            int fi = 0; for (int i = 0; i < FILTER_COUNT; i++) if (fcur == fids[i]) fi = i;
            ImGui::SetNextItemWidth(320);
            if (ImGui::Combo("Filtro padrão", &fi, FILTER_NAMES, FILTER_COUNT)) { proj->ini.set("padrao_filtro", fids[fi]); proj->save(); }
            ImGui::Spacing();
            if (ImGui::Button("Testar agora (emulação)")) {
                start_player(false);
                if (player) {
                    player->cfg.set("paleta", proj->ini.get("paleta_padrao", "classico"));
                    player->cfg.set("cores_personalizadas", proj->ini.get("cores_personalizadas", ""));
                    player->cfg.set("widescreen", proj->ini.get("padrao_widescreen", "0"));
                    player->cfg.set("bordas_wide", proj->ini.get("padrao_bordas_wide", "1"));
                    player->cfg.set("ultrawide", proj->ini.get("padrao_ultrawide", "0"));
                    player->cfg.set("linhas_extras", proj->ini.get("padrao_linhas_extras", "0"));
                    player->cfg.set("escala", proj->ini.get("padrao_escala", "inteira"));
                    player->cfg.set("moldura_formato", proj->ini.get("padrao_moldura_formato", "portatil"));
                    player->cfg.set("fundo_laterais", proj->ini.get("padrao_fundo_laterais", "desfocado"));
                    player->cfg.set("moldura_cor", proj->ini.get("padrao_moldura_cor", "cinza"));
                    player->cfg.set("sombra_sprites", proj->ini.get("padrao_sombra_sprites", "0"));
                    player->cfg.set("vinheta", proj->ini.get("padrao_vinheta", "0"));
                    player->cfg.set("cores_vivas", proj->ini.get("padrao_cores_vivas", "0"));
                    player->cfg.set("brilho", proj->ini.get("padrao_brilho", "0"));
                    player->cfg.set("sem_limite_sprites", proj->ini.get("padrao_sem_limite_sprites", "0"));
                    player->cfg.set("rastro_lcd", proj->ini.get("padrao_rastro_lcd", "0"));
                    player->cfg.set("filtro", proj->ini.get("padrao_filtro", "nitido"));
                    player->cfg.set("sgb", proj->ini.get("sgb", "1"));
                    player->cfg.set("sgb_borda", proj->ini.get("padrao_sgb_borda", "1"));
                    player->cfg.save();
                    player->reset();
                    player->apply_settings();
                }
            }
            ImGui::TextColored(COL_DIM, "Mudou algo? Clique em \"Exportar para Meus Jogos\" na tela Converter para atualizar o jogo exportado.");
            ImGui::EndTabItem();
        }
        if (tab_item("Tradução")) { draw_translation_tab(*proj, studio_msg()); ImGui::EndTabItem(); }
        if (tab_item("Trilha sonora")) { draw_music_tab(*proj, studio_msg()); ImGui::EndTabItem(); }
        if (tab_item("Ícone e abertura")) { draw_icon_tab(*proj, studio_msg()); ImGui::EndTabItem(); }
        if (tab_item("Conquistas")) { draw_achievements_editor(*proj, studio_msg()); ImGui::EndTabItem(); }
        if (tab_item("Patches e mods")) {
            ImGui::TextWrapped("Patches IPS alteram dados da ROM (gráficos, textos, fases). Você pode aplicá-los à ROM de trabalho (o executável é reconstruído com eles) ou empacotar suas alterações como um mod que o jogador liga/desliga no menu.");
            if (ImGui::Button("Aplicar patch IPS à ROM de trabalho...")) {
                std::string f = plat::open_file_dialog("Selecione um patch IPS", "Patch IPS", "*.ips");
                if (!f.empty()) {
                    std::vector<u8> ips, r = proj->rom; std::string err;
                    plat::read_file(f, ips);
                    if (!apply_ips(r, ips, err)) error("Patch inválido", err);
                    else { proj->write_working_rom(r, "patch " + plat::file_name(f)); ed_loaded = false; notify("Patch aplicado. Converta novamente para gerar o jogo modificado."); }
                }
            }
            ImGui::Spacing();
            ImGui::SetNextItemWidth(260); ImGui::InputText("Nome do mod", mod_name, sizeof mod_name);
            ImGui::SameLine();
            if (ImGui::Button("Criar mod com as alterações atuais")) {
                std::vector<u8> orig;
                plat::read_file(proj->path("rom/" + proj->ini.get("rom_original", "original.gb")), orig);
                std::vector<u8> ips = {'P', 'A', 'T', 'C', 'H'};
                const std::vector<u8>& cur = proj->rom;
                size_t i = 0;
                int recs = 0;
                while (i < cur.size()) {
                    if (i < orig.size() && cur[i] == orig[i]) { i++; continue; }
                    size_t j = i;
                    while (j < cur.size() && j - i < 0xFFFF && !(j < orig.size() && cur[j] == orig[j])) j++;
                    if (i == 0x454F46) i--;   // avoid the "EOF" offset
                    ips.push_back((u8)(i >> 16)); ips.push_back((u8)(i >> 8)); ips.push_back((u8)i);
                    ips.push_back((u8)((j - i) >> 8)); ips.push_back((u8)(j - i));
                    ips.insert(ips.end(), cur.begin() + i, cur.begin() + j);
                    i = j; recs++;
                }
                ips.insert(ips.end(), {'E', 'O', 'F'});
                if (!recs) error("Nada para empacotar", "A ROM de trabalho é igual à original. Faça alguma alteração primeiro.");
                else {
                    std::string md = proj->path(std::string("mods/pacotes/") + mod_name);
                    plat::make_dirs(md);
                    plat::write_file(plat::join(md, "patch.ips"), ips.data(), ips.size());
                    plat::write_text(plat::join(md, "mod.ini"), std::string("nome=") + mod_name + "\ndescricao=Criado no NativeBoy Studio\n");
                    notify("Mod criado (" + std::to_string(recs) + " trechos). Ele vai junto ao exportar o jogo.");
                }
            }
            ImGui::SeparatorText("Mods do projeto (incluídos na exportação)");
            for (auto& m : plat::list_dir(proj->path("mods/pacotes"), true)) ImGui::BulletText("%s", m.c_str());
            if (ImGui::Button("Abrir pasta de mods")) { plat::make_dirs(proj->path("mods/pacotes")); plat::open_in_explorer(proj->path("mods/pacotes")); }
            ImGui::EndTabItem();
        }
        if (tab_item("Gráficos HD")) {
            ImGui::TextWrapped("Pacotes HD trocam cada peça (tile) do jogo por uma imagem em alta resolução, sem alterar o jogo. "
                               "Jogue para coletar as peças, redesenhe a folha com todas elas e importe de volta.");
            const std::string mdir = proj->path("hd/modelos");
            ImGui::SeparatorText("1. Coletar as peças do jogo");
            ImGui::SetNextItemWidth(160); ImGui::SliderInt("Escala", &hd_scale, 2, 8, "%dx");
            ImGui::SameLine();
            bool smooth = proj->ini.getb("hd_suave", true);
            if (ImGui::Checkbox("Começar suavizado (xBR)", &smooth)) { proj->ini.set("hd_suave", smooth ? "1" : "0"); proj->save(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Os modelos já saem com curvas e diagonais arredondadas, em vez de pixels quadrados grandes.\nSó um ponto de partida: o pacote fica bonito quando você redesenha.");
            if (ImGui::Button("Jogar e coletar")) { start_player(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jogue normalmente: cada peça que aparecer na tela é guardada. Feche o jogo para gerar os modelos.");
            ImGui::SameLine();
            if (ImGui::Button("Coletar automaticamente (sessão de 2 min)")) {
                Project* p = proj.get(); int sc = hd_scale;
                job.start("Coletando tiles", [p, sc, smooth](BuildLog& log, std::string& err) {
                    Machine m;
                    if (!m.load_rom(p->rom, err)) return false;
                    m.track_pixels = true;
                    TileDumper d;
                    for (int f = 0; f < 7200; f++) { m.buttons = autoplay_buttons(f); m.run_frame(nullptr); m.apu->out.clear(); d.collect(m); if (f % 120 == 0) log.progress(f / 7200.f); }
                    int n = d.write(p->path("hd/modelos"), sc, err, smooth);
                    log.line(std::to_string(n) + " modelos salvos em hd/modelos");
                    return n > 0;
                }, [this](bool ok) { if (ok) notify("Peças coletadas: abra a folha para redesenhar"); });
            }
            ImGui::SeparatorText("2. Redesenhar");
            const bool has_sheet = plat::file_exists(plat::join(mdir, "folha_hd.png"));
            ImGui::BeginDisabled(!has_sheet);
            if (ImGui::Button("Abrir a folha (todas as peças numa imagem)")) plat::open_in_explorer(plat::join(mdir, "folha_hd.png"));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Abre folha_hd.png no programa de imagens padrão. Primeiro vêm os personagens, depois o cenário.\nRedesenhe por cima sem mudar o tamanho da imagem e salve.");
            ImGui::SameLine();
            if (ImGui::Button("Importar a folha redesenhada")) {
                std::string err;
                int n = hd_import_sheet(mdir, err);
                if (n < 0) error("Pacote HD", err);
                else notify(n ? std::to_string(n) + " peça(s) atualizada(s) a partir da folha" : "A folha não tem mudanças");
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Abrir pasta das peças")) { plat::make_dirs(mdir); plat::open_in_explorer(mdir); }
            if (!has_sheet) ImGui::TextDisabled("Colete as peças primeiro (passo 1).");
            {
                int total = 0, redrawn = 0; hd_pack_stats(mdir, total, redrawn);
                if (total) {
                    ImGui::ProgressBar(total ? (float)redrawn / total : 0.f, ImVec2(260, 0));
                    ImGui::SameLine(); ImGui::Text("%d de %d peças redesenhadas", redrawn, total);
                }
            }
            ImGui::SeparatorText("3. Usar no jogo");
            if (ImGui::Button("Importar pacote HD pronto (pasta)...")) {
                std::string d = plat::folder_dialog("Selecione a pasta do pacote HD");
                if (!d.empty()) { plat::copy_tree(d, proj->path("hd/" + plat::file_name(d))); notify("Pacote importado."); }
            }
            bool def = proj->ini.getb("hd_padrao", false);
            if (ImGui::Checkbox("Ativar o primeiro pacote HD por padrão no jogo exportado", &def)) { proj->ini.set("hd_padrao", def ? "1" : "0"); proj->save(); }
            ImGui::TextDisabled("No jogo, o jogador escolhe o pacote em Menu > Vídeo > Pacote HD.");
            ImGui::SeparatorText("Pacotes do projeto");
            for (auto& p : plat::list_dir(proj->path("hd"), true)) {
                int total = 0, redrawn = 0; hd_pack_stats(proj->path("hd/" + p), total, redrawn);
                ImGui::BulletText("%s: %d peças, %d redesenhadas", p.c_str(), total, redrawn);
            }
            ImGui::EndTabItem();
        }
        if (tab_item("Cheats")) {
            ImGui::TextWrapped("Códigos GameShark (formato 01VVAAAA), um por linha. Aplicados a cada quadro no jogo exportado (desligados no modo fidelidade). Linhas com # são comentários.");
            static char buf[16384];
            static bool init = false;
            if (!init || !texts_loaded) { strncpy(buf, cheats_text.c_str(), sizeof buf - 1); init = true; }
            ImGui::InputTextMultiline("##cheats", buf, sizeof buf, ImVec2(-1, 300));
            if (ImGui::Button("Salvar cheats")) { cheats_text = buf; plat::write_text(proj->path("mods/cheats.txt"), cheats_text); notify("Cheats salvos."); }
            ImGui::EndTabItem();
        }
        if (tab_item("Novas funções (C++)")) {
            ImGui::TextWrapped("Para quem programa: adicione funcionalidades em C++ que rodam junto com o código nativo do jogo. nb_user_frame é chamada a cada quadro; ganchos por endereço rodam antes de uma instrução específica.");
            static char hb[65536];
            static char hl[4096];
            static bool hinit = false;
            if (!hinit) { strncpy(hb, hooks_text.c_str(), sizeof hb - 1); strncpy(hl, hooks_list.c_str(), sizeof hl - 1); hinit = true; }
            ImGui::InputTextMultiline("##hooks", hb, sizeof hb, ImVec2(-1, 320), ImGuiInputTextFlags_AllowTabInput);
            ImGui::Text("Ganchos por endereço (banco:endereço nome_da_função, um por linha):");
            ImGui::InputTextMultiline("##hl", hl, sizeof hl, ImVec2(-1, 80));
            if (ImGui::Button("Salvar e recompilar")) {
                proj->backup("edição de ganchos");
                plat::write_text(proj->path("mods/hooks.cpp"), hb);
                plat::write_text(proj->path("mods/ganchos.txt"), hl);
                hooks_text = hb; hooks_list = hl;
                proj->ini.seti("etapa", std::min(proj->step(), (int)ST_ANALYZED)); proj->save();
                screen = SC_CONVERT;
                run_build_only();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remover ganchos")) { plat::remove_file(proj->path("mods/hooks.cpp")); plat::remove_file(proj->path("mods/ganchos.txt")); hinit = false; texts_loaded = false; notify("Ganchos removidos."); }
            ImGui::EndTabItem();
        }
        if (tab_item("Código-fonte (assembly)")) {
            ImGui::TextWrapped("A desmontagem em desmontagem/ é um projeto RGBDS completo. Edite os arquivos .asm em qualquer editor e clique abaixo para montar uma nova ROM de trabalho com o RGBDS incluído.");
            if (ImGui::Button("Abrir pasta da desmontagem")) plat::open_in_explorer(proj->path("desmontagem"));
            ImGui::SameLine();
            if (ImGui::Button("Montar ROM a partir da desmontagem")) {
                std::string ext = ".exe";
#ifndef _WIN32
                ext = "";
#endif
                std::string asmb = plat::join(tc.rgbds, "rgbasm" + ext), lnk = plat::join(tc.rgbds, "rgblink" + ext), fix = plat::join(tc.rgbds, "rgbfix" + ext);
                std::string cwd = proj->path("desmontagem");
                if (!plat::file_exists(plat::join(cwd, "main.asm"))) error("Desmontagem não encontrada", "Use Descompilar ROM primeiro.");
                else if (!plat::file_exists(asmb)) error("RGBDS não encontrado", "O montador RGBDS não está instalado em " + tc.rgbds);
                else {
                    plat::ProcResult a = plat::run(plat::quote(asmb) + " -o main.o main.asm", cwd);
                    rgbds_output = a.output;
                    if (a.code == 0) {
                        plat::ProcResult l = plat::run(plat::quote(lnk) + " -o montada.gb main.o", cwd);
                        rgbds_output += l.output;
                        if (l.code == 0) {
                            std::vector<u8> r;
                            plat::read_file(plat::join(cwd, "montada.gb"), r);
                            bool same = r == proj->rom;
                            if (!same) { plat::run(plat::quote(fix) + " -v montada.gb", cwd); plat::read_file(plat::join(cwd, "montada.gb"), r); }
                            if (same) notify("Montagem idêntica à ROM de trabalho (nenhuma alteração).");
                            else { proj->write_working_rom(r, "montagem RGBDS"); ed_loaded = false; notify("Nova ROM de trabalho montada. Converta novamente para gerar o executável."); }
                            rgbds_output += same ? "\nResultado: idêntico byte a byte.\n" : "\nResultado: ROM modificada aplicada ao projeto.\n";
                        }
                    }
                }
            }
            if (!rgbds_output.empty()) { ImGui::BeginChild("rgbo", ImVec2(0, 200), ImGuiChildFlags_Borders); ImGui::TextUnformatted(rgbds_output.c_str()); ImGui::EndChild(); }
            ImGui::EndTabItem();
        }
        if (tab_item("Backups")) {
            ImGui::TextWrapped("Antes de cada alteração o Studio guarda uma cópia do projeto. Restaure qualquer ponto anterior:");
            for (auto& b : proj->list_backups()) {
                std::string why; plat::read_text(proj->path("backups/" + b + "/motivo.txt"), why);
                ImGui::PushID(b.c_str());
                if (ImGui::Button("Restaurar")) { std::string err; if (proj->restore_backup(b, err)) { ed_loaded = false; texts_loaded = false; notify("Backup restaurado."); } else error("Falha", err); }
                ImGui::SameLine(); ImGui::Text("%s  -  %s", b.c_str(), why.c_str());
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void App::draw_settings() {
    ui::title("FERRAMENTAS", 1.4f);
    if (ImGui::BeginTabBar("set", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        if (tab_item("Geral")) {
            {
                // theme picker: the two handhelds, and the shell colours of the colour one
                ui::section("APARÊNCIA");
                const std::string cur = prefs.get("tema", "gbc"), shell = prefs.get("cor_carcaca", "uva");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                auto handheld = [&](const char* id, const char* label, ImU32 body, bool sel) {
                    ImGui::PushID(id);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const ImVec2 sz(110, 160);
                    const bool click = ImGui::InvisibleButton("h", ImVec2(sz.x, sz.y + 26));
                    const bool hov = ImGui::IsItemHovered();
                    dl->AddRectFilled(ImVec2(p.x + 3, p.y + 4), ImVec2(p.x + sz.x + 3, p.y + sz.y + 4), IM_COL32(0, 0, 0, 50), 10);
                    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), body, 10);
                    dl->AddRectFilled(ImVec2(p.x + sz.x - 26, p.y + sz.y - 30), ImVec2(p.x + sz.x, p.y + sz.y), body, 30, ImDrawFlags_RoundCornersBottomRight);
                    dl->AddRectFilled(ImVec2(p.x + 12, p.y + 12), ImVec2(p.x + sz.x - 12, p.y + 82), IM_COL32(80, 82, 98, 255), 6);
                    dl->AddRectFilled(ImVec2(p.x + 24, p.y + 20), ImVec2(p.x + sz.x - 24, p.y + 74), strcmp(id, "gb") ? IM_COL32(200, 214, 176, 255) : ui::pal().lcd[3], 2);
                    dl->AddRectFilled(ImVec2(p.x + 18, p.y + 104), ImVec2(p.x + 46, p.y + 112), IM_COL32(40, 40, 50, 255), 2);
                    dl->AddRectFilled(ImVec2(p.x + 28, p.y + 94), ImVec2(p.x + 36, p.y + 122), IM_COL32(40, 40, 50, 255), 2);
                    dl->AddCircleFilled(ImVec2(p.x + 76, p.y + 112), 7, strcmp(id, "gb") ? IM_COL32(50, 46, 70, 255) : IM_COL32(156, 38, 89, 255));
                    dl->AddCircleFilled(ImVec2(p.x + 94, p.y + 104), 7, strcmp(id, "gb") ? IM_COL32(50, 46, 70, 255) : IM_COL32(156, 38, 89, 255));
                    if (sel || hov) dl->AddRect(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + sz.x + 4, p.y + sz.y + 4), sel ? ui::accent(3) : IM_COL32(255, 255, 255, 90), 12, 0, sel ? 3.f : 1.5f);
                    const ImVec2 ts = ImGui::CalcTextSize(label);
                    dl->AddText(ImVec2(p.x + (sz.x - ts.x) / 2, p.y + sz.y + 6), ImGui::GetColorU32(ImGuiCol_Text), label);
                    ImGui::PopID();
                    return click;
                };
                int ns = 0; const ui::Shell* sh = ui::shells(&ns);
                ImU32 gbc_body = sh[0].color; for (int i = 0; i < ns; i++) if (shell == sh[i].id) gbc_body = sh[i].color;
                if (handheld("gb", "GB clássico", IM_COL32(200, 196, 190, 255), cur == "gb")) set_theme("gb", shell);
                ImGui::SameLine(0, 28);
                if (handheld("gbc", "GB Color", gbc_body, cur != "gb")) set_theme("gbc", shell);
                ImGui::SameLine(0, 28);
                ImGui::BeginGroup();
                ImGui::TextColored(COL_DIM, "Cor da carcaça (GB Color):");
                for (int i = 0; i < ns; i++) {
                    ImGui::PushID(i);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("c", ImVec2(34, 34))) set_theme("gbc", sh[i].id);
                    const bool hov = ImGui::IsItemHovered();
                    if (hov) ImGui::SetTooltip("%s", sh[i].name);
                    dl->AddCircleFilled(ImVec2(p.x + 17, p.y + 17), 14, sh[i].color);
                    if (!strcmp(sh[i].id, "atomico")) dl->AddCircleFilled(ImVec2(p.x + 13, p.y + 13), 5, IM_COL32(255, 255, 255, 90));
                    if (cur != "gb" && shell == sh[i].id) dl->AddCircle(ImVec2(p.x + 17, p.y + 17), 17, ui::accent(3), 0, 3);
                    else if (hov) dl->AddCircle(ImVec2(p.x + 17, p.y + 17), 17, IM_COL32(255, 255, 255, 120), 0, 1.5f);
                    ImGui::PopID();
                    if (i % 3 != 2) ImGui::SameLine();
                }
                ImGui::Spacing();
                ImGui::SetNextItemWidth(180);
                int us = (int)std::lround(ui_scale * 100);
                if (ImGui::SliderInt("Tamanho da interface", &us, 80, 160, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
                    ui_scale = us / 100.f;
                    ImGui::GetStyle().FontScaleMain = ui_scale;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) { prefs.set("tamanho_interface", std::to_string(us)); prefs.save(); set_theme(cur == "gb" ? "gb" : "gbc", shell); }
                ImGui::EndGroup();
                ui::section("PASTAS");
            }
            ImGui::Text("Pasta de projetos: %s", projects_root().c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Alterar##p")) { std::string d = plat::folder_dialog("Pasta de projetos"); if (!d.empty()) { prefs.set("pasta_projetos", d); prefs.save(); } }
            ImGui::Text("Meus Jogos: %s", games_root().c_str());
            ImGui::SameLine(); if (ImGui::SmallButton("Alterar##g")) { std::string d = plat::folder_dialog("Pasta Meus Jogos"); if (!d.empty()) { prefs.set("pasta_jogos", d); prefs.save(); } }
            int df = prefs.geti("quadros_descoberta", 3600), vf = prefs.geti("quadros_verificacao", 3600);
            ImGui::SetNextItemWidth(300);
            if (ImGui::SliderInt("Duração da descoberta automática (quadros)", &df, 600, 36000)) { prefs.seti("quadros_descoberta", df); prefs.save(); }
            ImGui::SetNextItemWidth(300);
            if (ImGui::SliderInt("Duração da verificação (quadros)", &vf, 600, 36000)) { prefs.seti("quadros_verificacao", vf); prefs.save(); }
            ImGui::TextColored(COL_DIM, "60 quadros = 1 segundo de jogo.");
            bool ad = prefs.getb("atalho_area_trabalho", true), am = prefs.getb("atalho_menu", true);
            if (ImGui::Checkbox("Criar atalho na Área de Trabalho ao exportar", &ad)) { prefs.seti("atalho_area_trabalho", ad); prefs.save(); }
            if (ImGui::Checkbox("Criar atalho no menu Iniciar / menu de aplicativos ao exportar", &am)) { prefs.seti("atalho_menu", am); prefs.save(); }
            ImGui::EndTabItem();
        }
        if (tab_item("Compilador")) {
            if (tc.ok) { ImGui::TextColored(COL_OK, "Pronto: %s", tc.version.c_str()); ImGui::Text("Local: %s", tc.gxx.c_str()); ImGui::Text("Cache do runtime: %s", tc.cache_dir.c_str()); }
            else { ImGui::TextColored(COL_ERR, "Indisponível"); ImGui::TextWrapped("%s", tc.problem.c_str()); }
            if (ImGui::Button("Detectar novamente")) tc.detect(app_dir, user_dir);
            ImGui::SameLine();
            if (ImGui::Button("Limpar cache do runtime")) { plat::remove_tree(tc.cache_dir); plat::make_dirs(tc.cache_dir); notify("Cache limpo: o runtime será recompilado na próxima conversão."); }
            ImGui::SameLine();
            if (ImGui::Button("Testar compilador")) {
                job.start("Teste do compilador", [this](BuildLog& log, std::string& err) {
                    std::vector<std::string> o;
                    return ensure_runtime_objects(tc, o, log, err);
                }, [this](bool ok) { if (ok) notify("Compilador funcionando."); else error("Teste do compilador falhou", job.error); });
            }
            if (job.running) draw_job_panel(200);
            ImGui::EndTabItem();
        }
        if (tab_item("Atualizações")) {
            ImGui::Text("Versão instalada: %s", NB_VERSION);
            ImGui::TextWrapped("Fonte das atualizações: um repositório do GitHub (dono/repositório, ex.: fulano/nativeboy-studio) "
                               "ou a URL de um arquivo JSON com {\"versao\": \"x.y.z\", \"download\": \"https://...\"}.");
            if (!url_buf[0]) strncpy(url_buf, prefs.get("url_atualizacao", NB_UPDATE_REPO).c_str(), sizeof url_buf - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##url", "dono/repositório ou https://...", url_buf, sizeof url_buf)) { prefs.set("url_atualizacao", url_buf); prefs.save(); }
            if (ImGui::Button("Verificar atualizações")) {
                std::string ver, dl, err;
                if (!url_buf[0]) error("Fonte não configurada", "Informe o repositório do GitHub ou a URL do canal de atualizações acima.");
                else if (!check_update(url_buf, ver, dl, err)) error("Não foi possível verificar", err);
                else if (!version_newer(ver, NB_VERSION)) notify("Você já tem a versão mais recente (" + ver + ").");
                else {
                    notify("Nova versão disponível: " + ver);
                    if (!dl.empty()) plat::open_url(dl);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Instalar atualização a partir de arquivo...")) {
#ifdef _WIN32
                std::string f = plat::open_file_dialog("Selecione NativeBoyStudio_Setup.exe", "Instalador", "*.exe");
                if (!f.empty() && plat::launch(f, "", plat::parent(f))) quit = true;
#else
                std::string f = plat::open_file_dialog("Selecione o pacote nativeboy-studio_*.deb", "Pacote Debian/Ubuntu", "*.deb");
                if (!f.empty()) { plat::open_in_explorer(f); notify("Instale o pacote no instalador de programas e reabra o Studio."); }
#endif
            }
            ImGui::TextColored(COL_DIM, "Seus projetos e jogos ficam em Documentos e são preservados nas atualizações.");
            ImGui::EndTabItem();
        }
        if (tab_item("Sobre")) {
            ImGui::Text("NativeBoy Studio %s", NB_VERSION);
            ImGui::TextWrapped("Ferramenta de engenharia reversa e recompilação estática de jogos de Game Boy / Game Boy Color para executáveis nativos de " NB_PLAT ".");
            ImGui::BulletText("Emulação (Jogar ROM): interpretador SM83 de referência.");
            ImGui::BulletText("Nativo (Converter): código SM83 traduzido para C++ e compilado com " NB_TOOLCHAIN ".");
            ImGui::BulletText("Desmontagem compatível com RGBDS.");
            ImGui::TextWrapped("Componentes de terceiros: SDL2 (zlib), Dear ImGui (MIT), stb (domínio público/MIT), w64devkit/GCC (GPL com exceção de runtime), RGBDS (MIT), fonte DejaVu.");
            ImGui::TextWrapped("Use apenas ROMs de jogos que você possui legalmente. Executáveis exportados contêm o código e os dados do jogo original.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void App::draw_dialogs() {
    // ---- Ajuda > Atalhos de teclado (F1)
    if (want_shortcuts) { ImGui::OpenPopup("Atalhos de teclado"); want_shortcuts = false; }
    if (ImGui::BeginPopupModal("Atalhos de teclado", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const char* rows[][2] = {
            {"Ctrl+O", "Abrir ROM"}, {"Ctrl+Shift+O", "Abrir projeto"}, {"Ctrl+W", "Fechar projeto"}, {"Ctrl+Shift+E", "Abrir pasta do projeto"},
            {"F5", "Jogar (emulação)"}, {"F6", "Descompilar"}, {"F7", "Converter para PC nativo"}, {"F8", "Modificar"},
            {"Ctrl+L", "Biblioteca"}, {"Ctrl+,", "Ferramentas e preferências"}, {"F11", "Tela cheia"}, {"Ctrl+Q", "Sair"},
            {"Esc (jogando)", "Menu do jogo"}, {"F9 (depurador)", "Pausar / continuar"}, {"Arrastar arquivo", "Abrir ROM, projeto, patch .ips ou lista .txt"}};
        if (ImGui::BeginTable("##atalhos", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            for (auto& r : rows) {
                ImGui::TableNextColumn();
                ui::push_head(1.0f);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::theme_gb ? ui::accent(0) : ui::accent(3)), "%s", r[0]);
                ui::pop_font();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
        ImGui::TextColored(COL_DIM, "Exibir > Restaurar a disposição dos painéis volta os painéis ao lugar original.");
        if (ImGui::Button("Fechar", ImVec2(-1, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // ---- Ajuda > Sobre
    if (want_about) { ImGui::OpenPopup("Sobre o NativeBoy Studio"); want_about = false; }
    if (ImGui::BeginPopupModal("Sobre o NativeBoy Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ui::lcd_frame(dl, p, ImVec2(p.x + 420, p.y + 110), 8);
        dl->AddText(ui::font_pixel, 24, ImVec2(p.x + 40, p.y + 30), ui::pal().lcd[0], "NATIVEBOY");
        dl->AddText(ui::font_pixel, 16, ImVec2(p.x + 40, p.y + 64), ui::pal().lcd[1], "STUDIO " NB_VERSION);
        ImGui::Dummy(ImVec2(420, 118));
        ImGui::TextWrapped("Converte jogos de GB e GB Color em programas nativos para Windows e Linux,\ncom emulação, descompilação e modificações.");
        ImGui::TextColored(COL_DIM, "Interface: Dear ImGui (MIT) e SDL2 (zlib).");
        ImGui::TextColored(COL_DIM, "Fontes: Press Start 2P e Pixelify Sans (SIL Open Font License), DejaVu Sans.");
        ImGui::TextColored(COL_DIM, "Dados de jogos: CC BY-SA 4.0 (veja a pasta licencas).");
        if (ImGui::Button("Abrir a pasta de licenças")) plat::open_in_explorer(plat::join(app_dir, "licencas"));
        ImGui::SameLine();
        if (ImGui::Button("Fechar") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (show_err) { ImGui::OpenPopup("Ops##err"); show_err = false; }
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Ops##err", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        ui::push_size(1.3f); ImGui::TextColored(COL_ERR, "%s", err_title.c_str()); ui::pop_size();
        ImGui::TextWrapped("%s", err_text.c_str());
        if (!err_details.empty()) {
            if (ImGui::CollapsingHeader("Detalhes técnicos")) {
                ImGui::BeginChild("det", ImVec2(0, 220), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(err_details.c_str());
                ImGui::EndChild();
                if (ImGui::Button("Copiar detalhes")) ImGui::SetClipboardText(err_details.c_str());
            }
        }
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ask_action) { ImGui::OpenPopup("ROM carregada"); ask_action = false; }
    if (ImGui::BeginPopupModal("ROM carregada", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s (%s) - o que deseja fazer?", proj ? proj->name.c_str() : "", proj ? proj->info.mbc_name.c_str() : "");
        if (ImGui::Button("Jogar ROM (emulação)", ImVec2(260, 40))) { ImGui::CloseCurrentPopup(); go(ACT_PLAY); }
        ImGui::SameLine();
        if (ImGui::Button("Descompilar ROM", ImVec2(260, 40))) { ImGui::CloseCurrentPopup(); go(ACT_DECOMP); }
        if (ImGui::Button("Converter para PC nativo", ImVec2(260, 40))) { ImGui::CloseCurrentPopup(); go(ACT_CONVERT); }
        ImGui::SameLine();
        if (ImGui::Button("Modificar jogo", ImVec2(260, 40))) { ImGui::CloseCurrentPopup(); go(ACT_MODIFY); }
        if (ImGui::Button("Cancelar")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (recovery_prompt) { ImGui::OpenPopup("Recuperação"); recovery_prompt = false; }
    if (ImGui::BeginPopupModal("Recuperação", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("O NativeBoy Studio não foi encerrado corretamente da última vez.");
        ImGui::Text("Projeto aberto: %s", recovery_project.c_str());
        ImGui::TextColored(COL_DIM, "Seus arquivos são gravados de forma segura e há backups automáticos em backups\\.");
        if (ImGui::Button("Reabrir o projeto", ImVec2(200, 0))) { ImGui::CloseCurrentPopup(); if (open_project(recovery_project)) screen = SC_CONVERT; }
        ImGui::SameLine();
        if (ImGui::Button("Ignorar", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!toast.empty() && SDL_GetTicks() < toast_until) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 ts = ImGui::CalcTextSize(toast.c_str());
        ImVec2 p(vp->Size.x - ts.x - 40, vp->Size.y - ts.y - 40);
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + ts.x + 24, p.y + ts.y + 16), IM_COL32(30, 110, 80, 235), 8);
        dl->AddText(ImVec2(p.x + 12, p.y + 8), IM_COL32(255, 255, 255, 255), toast.c_str());
    }
}

void App::frame() {
    job.poll();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        ImGui_ImplSDL2_ProcessEvent(&e);
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_DROPFILE) {
            std::string f = e.drop.file;
            SDL_free(e.drop.file);
            if (job.running) { notify("Aguarde a tarefa atual terminar."); continue; }
            std::string low = f; for (auto& c : low) c = (char)tolower((unsigned char)c);
            if (proj && low.size() > 4 && low.substr(low.size() - 4) == ".txt") {
                std::string err; int n = proj->import_missing(f, err);
                if (n >= 0) { notify(std::to_string(n) + " pontos de entrada importados."); screen = SC_CONVERT; }
                continue;
            }
            if (proj && low.size() > 4 && low.substr(low.size() - 4) == ".ips") {
                std::vector<u8> ips, r = proj->rom; std::string err;
                plat::read_file(f, ips);
                if (apply_ips(r, ips, err)) { proj->write_working_rom(r, "patch " + plat::file_name(f)); ed_loaded = false; notify("Patch aplicado à ROM de trabalho."); }
                else error("Patch inválido", err);
                continue;
            }
            Screen target = screen;
            if (open_rom(f)) {
                if (target == SC_HOME || target == SC_SETTINGS || target == SC_LIBRARY) ask_action = true;
                else if (target == SC_PLAY) go(ACT_PLAY);
                else if (target == SC_DECOMP) go(ACT_DECOMP);
                else screen = target;
            }
        }
        if (player && screen == SC_PLAY) player->handle_event(e);
    }
    if (player && screen == SC_PLAY) {
        player->update();
        if (player->want_fullscreen_toggle) {
            player->want_fullscreen_toggle = false;
            bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
    }
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    handle_shortcuts();
    update_title();
    draw_menubar();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float mb = ImGui::GetFrameHeight();
    const float bh = std::round(ui::base_size * ui_scale * 2.9f);
    const float sh = show_status ? std::round(ImGui::GetFrameHeight() + 4) : 0.f;
    draw_brandbar(vp->Pos.y + mb, bh);
    if (show_status) draw_statusbar(vp->Pos.y + vp->Size.y - sh, sh);
    // the docking area between the bars
    const float top = mb + bh;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + top));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - top - sh));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##doca", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    const ImGuiID dock = ImGui::GetID("doca_principal");
    if (want_default_layout || !ImGui::DockBuilderGetNode(dock)) {
        build_default_layout(dock, vp->Size.x, vp->Size.y - top - sh);
        if (want_default_layout) { show_proj_panel = show_log_panel = true; }
        want_default_layout = false;
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
    // side panels
    if (show_proj_panel) {
        if (ImGui::Begin("Projeto###painel_projeto", &show_proj_panel)) draw_project_panel();
        ImGui::End();
    }
    if (show_log_panel) {
        if (ImGui::Begin("Registro de atividades###painel_registro", &show_log_panel)) draw_log_window();
        ImGui::End();
    }
    // the work area (title = the current mode)
    static const char* work_title[] = {"Início", "Jogando (emulação)", "Descompilar", "Converter para PC nativo", "Modificar", "Ferramentas e preferências", "Biblioteca"};
    const std::string wt = std::string(work_title[screen]) + "###area";
    const bool playing = screen == SC_PLAY && player;
    if (playing) ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin(wt.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | (playing ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0));
    if (playing) ImGui::PopStyleColor();
    switch (screen) {
    case SC_HOME: draw_home(); break;
    case SC_PLAY: draw_play(play_area); break;
    case SC_DECOMP: draw_decomp(); break;
    case SC_CONVERT: draw_convert(); break;
    case SC_MODIFY: draw_modify(); break;
    case SC_SETTINGS: draw_settings(); break;
    case SC_LIBRARY: draw_library(games_root(), studio_msg()); break;
    default: break;
    }
    ImGui::End();
    if (playing && player) { player->draw_menu(play_area); player->draw_overlay(play_area); }
    if (playing && show_dbg && player) draw_debugger(*player, &show_dbg);
    if (player && player->want_quit) { close_player(); screen = SC_HOME; }
    SDL_Rect area = play_area;
    draw_dialogs();
    ImGui::Render();
    SDL_SetRenderDrawColor(ren, 14, 15, 18, 255);
    SDL_RenderClear(ren);
    {
        // backdrop: the shell of the handheld (GB: plain grey plastic with a fine texture;
        // GBC: the shell colour fading into the night)
        int ww = 0, wh = 0; SDL_GetRendererOutputSize(ren, &ww, &wh);
        const ui::Pal& P = ui::pal();
        auto cl = [](ImU32 c, float k, Uint8 a = 255) { SDL_Color o; o.r = (Uint8)std::min(255.f, (c & 0xFF) * k); o.g = (Uint8)std::min(255.f, ((c >> 8) & 0xFF) * k); o.b = (Uint8)std::min(255.f, ((c >> 16) & 0xFF) * k); o.a = a; return o; };
        SDL_Color c0, c1, c2, c3;
        if (ui::theme_gb) { c0 = cl(P.shell, 1.04f); c1 = cl(P.shell, 1.0f); c2 = cl(P.shell, 0.9f); c3 = cl(P.shell, 0.95f); }
        else { c0 = cl(P.shell, 0.42f); c1 = cl(P.shell, 0.28f); c2 = cl(P.shell, 0.10f); c3 = cl(P.shell, 0.18f); }
        SDL_Vertex v[4] = {{{0, 0}, c0, {0, 0}}, {{(float)ww, 0}, c1, {0, 0}}, {{(float)ww, (float)wh}, c2, {0, 0}}, {{0, (float)wh}, c3, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
        if (ui::theme_gb) {   // speaker-grille dots in the bottom-right corner
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 22);
            for (int y = wh - 160; y < wh - 30; y += 14) for (int x = ww - 190; x < ww - 30; x += 14) { SDL_Rect d = {x, y, 5, 5}; SDL_RenderFillRect(ren, &d); }
        }
    }
    if (player && screen == SC_PLAY) player->render(ren, area);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
    SDL_RenderPresent(ren);
    if (!(player && screen == SC_PLAY) && !job.running && !(SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS)) SDL_Delay(30);
}

// ------------------------------------------------------------------ command line mode
static int cli_convert(App& app, const std::string& rom, int dframes, int vframes, bool do_export) {
    auto say = [](const std::string& s) { printf("%s\n", s.c_str()); fflush(stdout); };
    Project p;
    std::string err;
    plat::make_dirs(app.projects_root());
    if (!Project::create_from_rom(rom, app.projects_root(), p, err)) { say("ERRO: " + err); return 2; }
    say("Projeto: " + p.dir);
    if (!p.ini.get("nome_oficial", "").empty()) say("RECONHECIDO: " + p.ini.get("nome_oficial", ""));
    {
        std::string d;
        for (const char* k : {"genero", "ano", "editora", "desenvolvedora", "jogadores", "serie"})
            if (!p.ini.get(k, "").empty()) d += std::string(d.empty() ? "" : " | ") + k + "=" + p.ini.get(k, "");
        if (!d.empty()) say("DADOS: " + d);
    }
    if (!p.ini.get("perfil", "").empty()) say("PERFIL: " + p.ini.get("perfil", "") + " (" + profile_kind_name(p.ini.get("perfil_tipo", "")) + ")");
    if (!app.tc.ok) { say("ERRO: " + app.tc.problem); return 3; }
    BuildLog log;
    log.line = say;
    log.progress = [](float) {};
    if (p.trace.empty() && !p.step_discover(dframes, log, err)) { say("ERRO: " + err); return 4; }
    for (int it = 0; it < 3; it++) {
        if (!p.step_analyze(log, err) || !p.step_generate(log, err) || !p.step_build(app.tc, log, err) || !p.step_verify(vframes, log, err)) { say("ERRO: " + err); return 5; }
        if (p.verify.missing.empty() || !p.verify.identical) break;
        say("Correção automática: " + std::to_string(p.add_entries(p.verify.missing)) + " entradas");
    }
    Report r = p.make_report();
    say("STATUS: " + r.title);
    for (auto& i : r.items) say("  - " + i.text);
    if (do_export) { std::string out; if (!p.step_export(app.games_root(), app.tc, log, err, out)) { say("ERRO: " + err); return 6; } say("EXPORTADO: " + out);
        if (getenv("NB_ATALHOS")) { std::string e; say(make_game_shortcuts(out, true, true, e) ? "ATALHOS: ok" : "ATALHOS: " + e); } }
    if (do_export && app.export_android) {
        std::string out;
        if (!p.make_android_project(app.games_root(), app.tc, log, err, out)) { say("ERRO: " + err); return 8; }
        say("ANDROID: " + out);
        if (app.export_apk) {
            const char* tk = getenv("NB_GITHUB_TOKEN");
            if (!tk || !*tk) { say("ERRO: defina NB_GITHUB_TOKEN com a chave do GitHub"); return 9; }
            ApkResult r;
            if (!build_apk_on_github(out, tk, apk_repo_name(p.name), plat::join(app.games_root(), "Android"), log, r, err)) { say("ERRO: " + err); return 9; }
            say("APK: " + r.apk);
        }
    }
    if (do_export && app.export_windows) {
        Toolchain w; std::string out;
        if (!w.detect_cross_windows(app.tc, app.user_dir)) { say("ERRO: " + w.problem); return 7; }
        if (!p.step_build(w, log, err) || !p.step_export(app.games_root(), w, log, err, out)) { say("ERRO: " + err); return 7; }
        say("EXPORTADO (Windows): " + out);
    }
    return r.status == 0 ? 0 : 1;
}

int main(int argc_, char** argv_) {
    // command-line arguments as UTF-8 (the narrow argv uses the ANSI code page and breaks names like "Saúde")
    std::vector<std::string> args_utf8;
#ifdef _WIN32
    {
        int wc = 0;
        LPWSTR* wv = CommandLineToArgvW(GetCommandLineW(), &wc);
        for (int i = 0; wv && i < wc; i++) {
            int n = WideCharToMultiByte(CP_UTF8, 0, wv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string u(n > 0 ? n - 1 : 0, 0);
            if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wv[i], -1, &u[0], n, nullptr, nullptr);
            args_utf8.push_back(u);
        }
        if (wv) LocalFree(wv);
    }
#else
    for (int i = 0; i < argc_; i++) args_utf8.push_back(argv_[i]);
#endif
    std::vector<char*> argv_ptrs;
    for (auto& a : args_utf8) argv_ptrs.push_back(&a[0]);
    int argc = (int)argv_ptrs.size();
    char** argv = argv_ptrs.data();
    App app;
    app.app_dir = plat::exe_dir();
    app.user_dir = plat::user_data_dir("NativeBoyStudio");
    app.prefs.load(plat::join(app.user_dir, "preferencias.ini"));
    app.prefs.path = plat::join(app.user_dir, "preferencias.ini");
    std::string override_root;
    for (int i = 1; i + 1 < argc; i++) if (!strcmp(argv[i], "--sdk-root")) override_root = argv[i + 1];
    app.tc.detect(override_root.empty() ? app.app_dir : override_root, app.user_dir);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--convert") && i + 1 < argc) {
#ifdef _WIN32
            if (AttachConsole(ATTACH_PARENT_PROCESS)) { freopen("CONOUT$", "w", stdout); freopen("CONOUT$", "w", stderr); }
#endif
            int df = 3600, vf = 3600; bool ex = false;
            for (int k = i + 2; k < argc; k++) {
                if (!strcmp(argv[k], "--frames") && k + 1 < argc) df = atoi(argv[k + 1]);
                if (!strcmp(argv[k], "--verify") && k + 1 < argc) vf = atoi(argv[k + 1]);
                if (!strcmp(argv[k], "--export")) ex = true;
                if (!strcmp(argv[k], "--windows")) { ex = true; app.export_windows = true; }
                if (!strcmp(argv[k], "--android")) { ex = true; app.export_android = true; }
                if (!strcmp(argv[k], "--apk-github")) { ex = true; app.export_android = true; app.export_apk = true; }
                if (!strcmp(argv[k], "--projects") && k + 1 < argc) app.prefs.set("pasta_projetos", argv[k + 1]);
                if (!strcmp(argv[k], "--games") && k + 1 < argc) app.prefs.set("pasta_jogos", argv[k + 1]);
            }
            return cli_convert(app, argv[i + 1], df, vf, ex);
        }
    }

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        plat::message_box("NativeBoy Studio", std::string("Não foi possível iniciar a interface gráfica:\n") + SDL_GetError(), true);
        return 1;
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    // the window comes back with the size, place and state it had (like a native program)
    const int wx = app.prefs.geti("janela_x", SDL_WINDOWPOS_CENTERED), wy = app.prefs.geti("janela_y", SDL_WINDOWPOS_CENTERED);
    const int ww0 = std::max(1100, app.prefs.geti("janela_w", 1360)), wh0 = std::max(700, app.prefs.geti("janela_h", 860));
    app.win = SDL_CreateWindow("NativeBoy Studio", wx, wy, ww0, wh0, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (app.win && app.prefs.getb("janela_max", false)) SDL_MaximizeWindow(app.win);
    if (app.win) SDL_ShowWindow(app.win);
    if (!app.win) { plat::message_box("NativeBoy Studio", std::string("Não foi possível criar a janela:\n") + SDL_GetError(), true); return 1; }
    SDL_SetWindowMinimumSize(app.win, 1100, 700);
    app.ren = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app.ren) app.ren = SDL_CreateRenderer(app.win, -1, SDL_RENDERER_SOFTWARE);
    if (!app.ren) { plat::message_box("NativeBoy Studio", std::string("Não foi possível iniciar o renderizador:\n") + SDL_GetError(), true); return 1; }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    std::string ini = plat::join(app.user_dir, "imgui.ini");
    static std::string ini_keep = ini;
    ImGui::GetIO().IniFilename = ini_keep.c_str();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui::GetIO().ConfigDockingWithShift = false;
    // theme: the old "retro" / "classico" / "gbc" became the GB Color one (grape shell)
    {
        std::string t = app.prefs.get("tema", "gbc");
        if (t != "gb") t = "gbc";
        app.ui_scale = std::max(0.8f, std::min(1.6f, app.prefs.geti("tamanho_interface", 100) / 100.f));
        ui::load_fonts(app.app_dir, 18.0f);
        ImGui::GetStyle().FontScaleMain = app.ui_scale;
        app.set_theme(t, app.prefs.get("cor_carcaca", "uva"));
    }
    ImGui_ImplSDL2_InitForSDLRenderer(app.win, app.ren);
    ImGui_ImplSDLRenderer2_Init(app.ren);

    // crash recovery
    std::string lock;
    if (plat::read_text(plat::join(app.user_dir, "sessao.lock"), lock) && !trim(lock).empty() && plat::dir_exists(trim(lock))) {
        app.recovery_prompt = true;
        app.recovery_project = trim(lock);
    }
    app.write_session();
    // first launch: prepare the integrated compiler in the background (compiles the runtime once)
    if (app.tc.ok && plat::list_dir(app.tc.cache_dir, false).size() < app.tc.runtime_sources().size()) {
        App* ap = &app;
        app.job.start("Preparando o compilador integrado (somente na primeira vez)", [ap](BuildLog& log, std::string& err) {
            std::vector<std::string> o;
            return ensure_runtime_objects(ap->tc, o, log, err);
        }, [ap](bool ok) { ap->notify(ok ? "Compilador integrado pronto." : "Não foi possível preparar o compilador. Veja Ferramentas > Compilador."); });
    }
    // ROM passed on the command line (double click on a .gb with file association)
    for (int i = 1; i < argc; i++) if (argv[i][0] != '-' && (plat::file_exists(argv[i]) || plat::dir_exists(argv[i]))) { if (app.open_rom(argv[i])) app.ask_action = true; app.recovery_prompt = false; }
    // automated interface tests: NB_TEST_ACTION=jogar|modificar|descompilar|converter (+ NB_TEST_DEBUG=1)
    if (const char* ts = getenv("NB_TEST_SCREEN")) if (!strcmp(ts, "biblioteca")) { app.screen = SC_LIBRARY; app.recovery_prompt = false; }
    if (const char* ta = getenv("NB_TEST_ACTION")) {
        if (app.proj) {
            app.ask_action = false;
            if (!strcmp(ta, "jogar")) { app.go(ACT_PLAY); app.show_dbg = getenv("NB_TEST_DEBUG") != nullptr; }
            else if (!strcmp(ta, "modificar")) app.go(ACT_MODIFY);
            else if (!strcmp(ta, "descompilar")) app.go(ACT_DECOMP);
            else if (!strcmp(ta, "converter")) app.go(ACT_CONVERT);
        }
    }

    while (!app.quit) app.frame();
    {   // remember the window
        const Uint32 fl = SDL_GetWindowFlags(app.win);
        app.prefs.set("janela_max", (fl & SDL_WINDOW_MAXIMIZED) ? "1" : "0");
        if (!(fl & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_MINIMIZED))) {
            int x, y, w, h; SDL_GetWindowPosition(app.win, &x, &y); SDL_GetWindowSize(app.win, &w, &h);
            app.prefs.set("janela_x", std::to_string(x)); app.prefs.set("janela_y", std::to_string(y));
            app.prefs.set("janela_w", std::to_string(w)); app.prefs.set("janela_h", std::to_string(h));
        }
        app.prefs.save();
    }

    app.job.cancel = true;
    if (app.job.th.joinable()) app.job.th.join();
    app.close_player();
    app.clear_textures();
    plat::remove_file(plat::join(app.user_dir, "sessao.lock"));
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(app.ren);
    SDL_DestroyWindow(app.win);
    SDL_Quit();
    return 0;
}
