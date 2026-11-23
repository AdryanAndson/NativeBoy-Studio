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
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#define NB_VERSION "1.16.0"
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

static const ImVec4 COL_OK(0.35f, 0.85f, 0.55f, 1), COL_WARN(1.0f, 0.78f, 0.30f, 1), COL_ERR(1.0f, 0.42f, 0.38f, 1), COL_INFO(0.6f, 0.75f, 1.0f, 1), COL_DIM(0.6f, 0.62f, 0.66f, 1);


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
    void draw_topbar();
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
    ImU32 bg = ui::theme_retro ? (hov ? IM_COL32(70, 68, 150, 245) : IM_COL32(48, 48, 116, 235))
             : ui::theme_gbc ? (hov ? IM_COL32(74, 54, 132, 240) : IM_COL32(52, 38, 96, 225)) : (hov ? IM_COL32(40, 46, 56, 255) : IM_COL32(30, 34, 42, 255));
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, 12);
    dl->AddRectFilled(p, ImVec2(p.x + 8, p.y + size.y), ImGui::ColorConvertFloat4ToU32(accent), 12, ImDrawFlags_RoundCornersLeft);
    if (hov) dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::ColorConvertFloat4ToU32(accent), 12.0f, 0, 2.0f);
    float fs = ui::base_size;
    ImFont* f = ImGui::GetFont();
    dl->AddText(f, fs * 1.6f, ImVec2(p.x + 28, p.y + 22), IM_COL32(255, 255, 255, 255), title);
    if (tag && *tag) {
        ImVec2 ts = f->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0, tag);
        ImVec2 tp(p.x + size.x - ts.x - 28, p.y + 26);
        dl->AddRectFilled(ImVec2(tp.x - 8, tp.y - 4), ImVec2(tp.x + ts.x + 8, tp.y + ts.y + 4), ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x * 0.5f, accent.y * 0.5f, accent.z * 0.5f, 1)), 6);
        dl->AddText(f, fs * 0.85f, tp, IM_COL32(255, 255, 255, 230), tag);
    }
    dl->AddText(f, fs, ImVec2(p.x + 28, p.y + 22 + fs * 2.2f), IM_COL32(190, 196, 206, 255), desc, nullptr, size.x - 56);
    ImGui::PopID();
    return clicked;
}

void App::draw_topbar() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 52));
    ImGui::Begin("##top", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (ui::theme_gbc) {
        // five colour dots before the name and a thin colour stripe under the bar
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        for (int i = 0; i < 5; i++) dl->AddCircleFilled(ImVec2(p.x + 7 + i * 15.f, p.y + 15), 5.5f, ui::accent(i));
        const float sw = vp->Size.x / 5;
        for (int i = 0; i < 5; i++) dl->AddRectFilled(ImVec2(vp->Pos.x + i * sw, vp->Pos.y + 49), ImVec2(vp->Pos.x + (i + 1) * sw, vp->Pos.y + 52), ui::accent(i));
        ImGui::Dummy(ImVec2(78, 1)); ImGui::SameLine(0, 4);
    }
    ui::push_size(1.3f);
    ImGui::TextColored(ui::theme_gbc ? ImVec4(1, 1, 1, 1) : COL_OK, "NativeBoy");
    ImGui::SameLine(0, 4);
    ImGui::Text("Studio");
    ui::pop_size();
    ImGui::SameLine(0, 30);
    bool busy = job.running;
    ImGui::BeginDisabled(busy);
    auto tab = [&](const char* label, Screen s, std::function<void()> on) {
        bool active = screen == s;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        else ImGui::PushStyleColor(ImGuiCol_Button, ui::theme_retro ? ImVec4(0.24f, 0.24f, 0.54f, 1) : ui::theme_gbc ? ImVec4(0.235f, 0.175f, 0.42f, 1) : ImVec4(0.16f, 0.18f, 0.22f, 1));
        if (ImGui::Button(label)) on();
        ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    tab("Início", SC_HOME, [&] { close_player(); screen = SC_HOME; });
    tab("Biblioteca", SC_LIBRARY, [&] { close_player(); screen = SC_LIBRARY; });
    tab("Jogar ROM", SC_PLAY, [&] { go(ACT_PLAY); });
    tab("Descompilar", SC_DECOMP, [&] { close_player(); go(ACT_DECOMP); });
    tab("Converter para PC", SC_CONVERT, [&] { close_player(); go(ACT_CONVERT); });
    tab("Modificar", SC_MODIFY, [&] { close_player(); go(ACT_MODIFY); });
    tab("Ferramentas", SC_SETTINGS, [&] { close_player(); screen = SC_SETTINGS; });
    ImGui::EndDisabled();
    if (proj) {
        // right-aligned, but never over the tabs: on narrow windows the label goes and the name is shortened
        const float after_tabs = ImGui::GetCursorPosX() + 16;
        const std::string tail = proj->info.cgb_supported ? "  (GBC)" : "  (GB)";
        std::string name = proj->name;
        const float close_w = ImGui::CalcTextSize("Fechar").x + 40, label_w = ImGui::CalcTextSize("Projeto: ").x;
        auto width = [&](const std::string& n) { return ImGui::CalcTextSize((n + tail).c_str()).x; };
        bool label = vp->Size.x - 20 - close_w - width(name) - label_w >= after_tabs;
        while (!label && name.size() > 4 && vp->Size.x - 20 - close_w - width(name + "...") < after_tabs) {
            name.pop_back();
            while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) name.pop_back();   // (whole UTF-8 characters)
        }
        if (name != proj->name) name += "...";
        const std::string t = name + tail;
        ImGui::SameLine(std::max(after_tabs, vp->Size.x - 20 - close_w - width(name) - (label ? label_w : 0)));
        if (label) { ImGui::TextColored(COL_DIM, "Projeto:"); ImGui::SameLine(); }
        ImGui::Text("%s", t.c_str());
        if (name != proj->name && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", proj->name.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Fechar")) { close_player(); proj.reset(); screen = SC_HOME; write_session(); }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

void App::draw_home() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float W = vp->Size.x - 28;
    ui::push_size(2.0f);
    ImGui::Text("O que você quer fazer?");
    ui::pop_size();
    ImGui::TextColored(COL_DIM, "Escolha uma opção ou arraste uma ROM .gb / .gbc para esta janela.");
    ImGui::Spacing();
    float cw = (W - 20) / 2, ch = 150;
    if (big_card("play", "JOGAR ROM", "Roda o jogo original no ambiente de compatibilidade integrado. Isto é EMULAÇÃO: cada instrução é interpretada em tempo real. Jogar também ajuda a descobrir código para a conversão.", "EMULAÇÃO", ImVec4(0.85f, 0.55f, 0.15f, 1), ImVec2(cw, ch))) go(ACT_PLAY);
    ImGui::SameLine(0, 20);
    if (big_card("dec", "DESCOMPILAR ROM", "Analisa o jogo: identifica código, funções, gráficos e músicas e gera um projeto editável (assembly RGBDS que remonta a ROM idêntica, folhas de tiles, capturas VGM).", "ANÁLISE", ImVec4(0.35f, 0.55f, 0.95f, 1), ImVec2(cw, ch))) go(ACT_DECOMP);
    if (big_card("conv", "CONVERTER PARA PC NATIVO", "Tradução estática SM83 -> C++ e compilação de um executável " NB_PLAT ". Sem interpretar instruções quando a tradução está completa; a verificação compara tudo com o original.", "NATIVO x64", ImVec4(0.25f, 0.75f, 0.50f, 1), ImVec2(cw, ch))) go(ACT_CONVERT);
    ImGui::SameLine(0, 20);
    if (big_card("mod", "MODIFICAR JOGO", "Editor visual: redesenhe gráficos, troque paletas, crie pacotes HD, cheats, mods e ganchos C++ para novas funções. Depois gere uma versão modernizada.", "EDITOR", ImVec4(0.75f, 0.40f, 0.85f, 1), ImVec2(cw, ch))) go(ACT_MODIFY);
    ImGui::Spacing(); ImGui::Spacing();
    ImGui::BeginChild("bottom", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::Columns(2, nullptr, false);
    ImGui::SeparatorText("Projetos recentes");
    auto rec = recent();
    if (rec.empty()) ImGui::TextColored(COL_DIM, "Nenhum projeto ainda. Abra uma ROM para começar.");
    for (auto& r : rec) {
        if (!plat::dir_exists(r)) continue;
        Settings s; s.load(plat::join(r, "projeto.ini"));
        static const char* steps[] = {"importado", "código descoberto", "descompilado", "traduzido", "compilado", "verificado", "exportado"};
        int st = std::max(0, std::min(6, s.geti("etapa", 0)));
        std::string label = s.get("nome", plat::file_name(r)) + "   -   " + steps[st] + "##" + r;
        if (ImGui::Selectable(label.c_str())) { open_project(r); }
    }
    if (ImGui::Button("Abrir ROM...")) choose_rom();
    ImGui::SameLine();
    if (ImGui::Button("Abrir projeto existente...")) {
        std::string d = plat::folder_dialog("Selecione a pasta do projeto");
        if (!d.empty()) open_project(d);
    }
    ImGui::NextColumn();
    ImGui::SeparatorText("Ambiente");
    if (tc.ok) { ImGui::TextColored(COL_OK, "Compilador integrado pronto"); ImGui::TextColored(COL_DIM, "%s", tc.version.c_str()); }
    else { ImGui::TextColored(COL_ERR, "Compilador indisponível"); ImGui::TextWrapped("%s", tc.problem.c_str()); }
    ImGui::TextColored(COL_DIM, "Projetos: %s", projects_root().c_str());
    ImGui::TextColored(COL_DIM, "Jogos exportados: %s", games_root().c_str());
    ImGui::TextColored(COL_DIM, "Versão %s", NB_VERSION);
    if (job.running) { float pr; { std::lock_guard<std::mutex> lk(job.mu); pr = job.progress; } ImGui::Text("%s", job.title.c_str()); ImGui::ProgressBar(pr, ImVec2(-1, 0)); }
    ImGui::Columns(1);
    ImGui::EndChild();
}

void App::draw_play(const SDL_Rect& area) {
    if (!player) { screen = SC_HOME; return; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 52));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, 52));
    ImGui::Begin("##playbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ui::badge("MODO EMULAÇÃO - interpretador SM83 (não é o executável nativo)", IM_COL32(170, 110, 20, 255));
    ImGui::SameLine();
    if (ImGui::Button(player->paused ? "Continuar" : "Pausar")) player->paused = !player->paused;
    ImGui::SameLine();
    if (ImGui::Button("Menu do jogo (Esc)")) player->menu_open = !player->menu_open;
    ImGui::SameLine();
    if (ImGui::Button("Reiniciar")) player->reset();
    ImGui::SameLine();
    if (!vgm_recording) { if (ImGui::Button("Gravar música (VGM)")) { player->m.apu->start_vgm(); vgm_recording = true; notify("Gravando a música... clique em Parar para salvar."); } }
    else { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1)); if (ImGui::Button("Parar gravação")) stop_vgm(); ImGui::PopStyleColor(); }
    ImGui::SameLine();
    if (ImGui::Button(show_dbg ? "Fechar depurador" : "Depurador")) show_dbg = !show_dbg;
    ImGui::SameLine();
    if (dumping) ImGui::TextColored(COL_INFO, "Coletando tiles para HD: %zu", dumper.tiles.size());
    else {
        int n = 0; for (u8 v : play_trace) n += v;
        ImGui::TextColored(COL_DIM, "Código descoberto nesta sessão: %s instr.", fmt_num(n).c_str());
    }
    ImGui::SameLine(vp->Size.x - 90);
    if (ImGui::Button("Fechar")) { close_player(); screen = SC_HOME; ImGui::End(); return; }
    ImGui::End();
    player->draw_menu(area);
    player->draw_overlay(area);
    if (show_dbg) draw_debugger(*player, &show_dbg);
    if (player->want_quit) { close_player(); screen = SC_HOME; }
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
    dl->AddRectFilled(p, ImVec2(p.x + w, y1), ImGui::ColorConvertFloat4ToU32(ImVec4(c.x * 0.25f, c.y * 0.25f, c.z * 0.25f, 1)), 10);
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
    ui::push_size(1.5f); ImGui::Text("Descompilar ROM"); ui::pop_size();
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
    if (ImGui::BeginTabBar("dec")) {
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
    ui::push_size(1.5f); ImGui::Text("Converter para PC nativo"); ui::pop_size();
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
    ui::push_size(1.5f); ImGui::Text("Modificar jogo"); ui::pop_size();
    ImGui::SameLine(); ImGui::TextColored(COL_DIM, "  %s%s", proj->name.c_str(), proj->rom_modified() ? "  (ROM de trabalho modificada)" : "");
    if (job.running) { draw_job_panel(0); return; }
    int nb = (int)(ed_rom.size() / 0x4000);
    if (ImGui::BeginTabBar("mod")) {
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
            ImGui::TextWrapped("A desmontagem em desmontagem\\ é um projeto RGBDS completo. Edite os arquivos .asm em qualquer editor e clique abaixo para montar uma nova ROM de trabalho com o RGBDS incluído.");
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
    ui::push_size(1.5f); ImGui::Text("Ferramentas e configurações"); ui::pop_size();
    if (ImGui::BeginTabBar("set")) {
        if (tab_item("Geral")) {
            {
                static const char* tid[] = {"retro", "gbc", "classico"};
                static const char* tn[] = {"Portátil retrô (padrão)", "Roxo translúcido", "Escuro clássico (verde)"};
                int ti = 0; for (int i = 0; i < 3; i++) if (prefs.get("tema", "retro") == tid[i]) ti = i;
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("Aparência do Studio", &ti, tn, 3)) { prefs.set("tema", tid[ti]); prefs.save(); ui::apply_style(false, tid[ti]); }
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
    draw_topbar();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    SDL_Rect area = {0, 104, (int)vp->Size.x, (int)vp->Size.y - 104};
    if (screen == SC_PLAY) draw_play(area);
    else {
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 52));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, vp->Size.y - 52));
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        switch (screen) {
        case SC_HOME: draw_home(); break;
        case SC_DECOMP: draw_decomp(); break;
        case SC_CONVERT: draw_convert(); break;
        case SC_MODIFY: draw_modify(); break;
        case SC_SETTINGS: draw_settings(); break;
        case SC_LIBRARY: draw_library(games_root(), studio_msg()); break;
        default: break;
        }
        ImGui::End();
    }
    draw_dialogs();
    ImGui::Render();
    SDL_SetRenderDrawColor(ren, 14, 15, 18, 255);
    SDL_RenderClear(ren);
    if (ui::theme_retro) {
        // indigo backdrop with a faint dot-matrix grid, like an old handheld screen up close
        int ww = 0, wh = 0; SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_Vertex v[4] = {{{0, 0}, {66, 64, 150, 255}, {0, 0}}, {{(float)ww, 0}, {50, 48, 124, 255}, {0, 0}},
                           {{(float)ww, (float)wh}, {22, 22, 60, 255}, {0, 0}}, {{0, (float)wh}, {36, 36, 92, 255}, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 20, 34);
        for (int x = 0; x < ww; x += 4) SDL_RenderDrawLine(ren, x, 0, x, wh);
        for (int y = 0; y < wh; y += 4) SDL_RenderDrawLine(ren, 0, y, ww, y);
    } else if (ui::theme_gbc) {
        // translucent purple backdrop: lighter at the top-left corner, darker towards the bottom-right
        int ww = 0, wh = 0; SDL_GetRendererOutputSize(ren, &ww, &wh);
        SDL_Vertex v[4] = {{{0, 0}, {92, 62, 176, 255}, {0, 0}}, {{(float)ww, 0}, {58, 38, 122, 255}, {0, 0}},
                           {{(float)ww, (float)wh}, {22, 14, 50, 255}, {0, 0}}, {{0, (float)wh}, {44, 28, 96, 255}, {0, 0}}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(ren, nullptr, v, 4, idx, 6);
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
    app.win = SDL_CreateWindow("NativeBoy Studio", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1360, 860, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ui::apply_style(false, app.prefs.get("tema", "retro").c_str());
    ui::load_fonts(app.app_dir, 18.0f);
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
