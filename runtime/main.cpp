// Entry point of every exported (statically recompiled) game.
// The game's translated code and its resources are linked into this executable;
// the original ROM file is not needed.
#include "../common/platform.h"
#include "../common/player.h"
#include "../common/video.h"
#include "../common/ui_common.h"
#include "../core/gb.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>
#endif
#include <cstdlib>
#include <cstring>
#include <string>

gb::NativeResolver* nb_game_resolver();
const unsigned char* nb_game_rom(unsigned* size);
const char* nb_game_name();
void nb_user_init(gb::Machine& m);
void nb_user_frame(gb::Machine& m);
int nb_selftest(int frames, const char* out_path);
bool nb_game_sgb();

// full screen like a PC game: borderless (desktop resolution) or exclusive with a chosen resolution
static void set_fullscreen(SDL_Window* w, GamePlayer& p, bool on) {
    if (!on) SDL_SetWindowFullscreen(w, 0);
    else if (p.cfg.get("tipo_tela_cheia", "sem_bordas") == "exclusiva") {
        SDL_DisplayMode want{}, got{};
        int rw = 0, rh = 0, hz = 0;
        if (sscanf(p.cfg.get("resolucao", "").c_str(), "%dx%d @ %d", &rw, &rh, &hz) >= 2) { want.w = rw; want.h = rh; want.refresh_rate = hz; }
        else SDL_GetDesktopDisplayMode(0, &want);
        if (SDL_GetClosestDisplayMode(0, &want, &got)) SDL_SetWindowDisplayMode(w, &got);
        SDL_SetWindowFullscreen(w, 0);
        SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN);
    } else SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);
    p.cfg.set("fullscreen", on ? "1" : "0");
    p.cfg.save();
}
// console look: the window has no borders and is dragged by the drawn body; its edges still resize it
static SDL_HitTestResult console_hit_test(SDL_Window* w, const SDL_Point* p, void* data) {
    GamePlayer* pl = (GamePlayer*)data;
    if (!pl->con.on || pl->menu_open || pl->start_screen || pl->ask_continue || (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN)) return SDL_HITTEST_NORMAL;
    if (ImGui::GetIO().WantCaptureMouse) return SDL_HITTEST_NORMAL;        // the bar at the top, notices...
    int ww, wh; SDL_GetWindowSize(w, &ww, &wh);
    const int e = 8;
    const bool L = p->x < e, R = p->x >= ww - e, T = p->y < e, B = p->y >= wh - e;
    if (T && L) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (T && R) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (B && L) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (B && R) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (T) return SDL_HITTEST_RESIZE_TOP;
    if (B) return SDL_HITTEST_RESIZE_BOTTOM;
    if (L) return SDL_HITTEST_RESIZE_LEFT;
    if (R) return SDL_HITTEST_RESIZE_RIGHT;
    int ow = ww, oh = wh;
    if (SDL_Renderer* r = SDL_GetRenderer(w)) SDL_GetRendererOutputSize(r, &ow, &oh);
    GamePlayer::ConsoleHit h = pl->console_hit(p->x * (float)ow / ww, p->y * (float)oh / wh);
    return h.kind == GamePlayer::ConsoleHit::BODY || h.kind == GamePlayer::ConsoleHit::NONE ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
}

static bool is_fullscreen(SDL_Window* w) { return (SDL_GetWindowFlags(w) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0; }

// Windows 10/11: dark title bar, so the window looks like a modern dark app
static void dark_title_bar(SDL_Window* w) {
#ifdef _WIN32
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(w, &info)) return;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    SetAttr set = (SetAttr)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        BOOL on = TRUE;
        if (set(info.info.win.window, 20, &on, sizeof on) != S_OK) set(info.info.win.window, 19, &on, sizeof on);   // 19 on older Windows 10
        DWORD caption = 0x00561E2E, text = 0x00FFF0F4;    // Windows 11: purple title bar (COLORREF is 0x00BBGGRR) with light text
        set(info.info.win.window, 35, &caption, sizeof caption);
        set(info.info.win.window, 36, &text, sizeof text);
    }
#else
    (void)w;
#endif
}

// remembers where the window was (size, position, maximized) for the next time
static void save_window(SDL_Window* w, GamePlayer& p) {
    if (is_fullscreen(w)) return;
    Uint32 fl = SDL_GetWindowFlags(w);
    p.cfg.set("janela_maximizada", (fl & SDL_WINDOW_MAXIMIZED) ? "1" : "0");
    if (!(fl & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))) {
        int x, y, ww, hh;
        SDL_GetWindowPosition(w, &x, &y); SDL_GetWindowSize(w, &ww, &hh);
        p.cfg.seti("janela_x", x); p.cfg.seti("janela_y", y); p.cfg.seti("janela_w", ww); p.cfg.seti("janela_h", hh);
    }
}

#ifdef __ANDROID__
#include <android/log.h>
// steps of the start in the Android log (adb logcat -s NativeBoy)
#define NB_STEP(...) __android_log_print(ANDROID_LOG_INFO, "NativeBoy", __VA_ARGS__)
#else
#define NB_STEP(...) ((void)0)
#endif
#ifdef __ANDROID__
// Android: the game's files (assets/, config/) travel inside the APK. On the first start, and after an
// update of the app, they are copied to the app's private folder, where the game reads them like on a PC.
// arquivos.txt (made by the Studio) lists them. The player's own settings are never overwritten.
static bool apk_read(const std::string& path, std::string& out) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return false;
    out.clear();
    char buf[16384]; size_t n;
    while ((n = SDL_RWread(rw, buf, 1, sizeof buf)) > 0) out.append(buf, n);
    SDL_RWclose(rw);
    return true;
}
static void apk_unpack(const std::string& base) {
    std::string list, have;
    if (!apk_read("arquivos.txt", list)) return;
    plat::read_text(plat::join(base, "arquivos.txt"), have);
    if (have == list) return;
    size_t p = 0;
    while (p < list.size()) {
        size_t e = list.find('\n', p);
        std::string f = list.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? list.size() : e + 1;
        while (!f.empty() && (f.back() == '\r' || f.back() == ' ')) f.pop_back();
        if (f.empty() || f[0] == '#') continue;
        const std::string dst = plat::join(base, f);
        if (f == "config/settings.ini" && plat::file_exists(dst)) continue;
        std::string data;
        if (!apk_read(f, data)) continue;
        size_t sl = dst.rfind('/');
        if (sl != std::string::npos) plat::make_dirs(dst.substr(0, sl));
        plat::write_file(dst, data.data(), data.size());
    }
    plat::write_text(plat::join(base, "arquivos.txt"), list);
}
#endif

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest") && i + 2 < argc) return nb_selftest(atoi(argv[i + 1]), argv[i + 2]);
    }
#ifdef NB_MOBILE
    // any orientation: standing up shows the drawn console, lying down the full screen with touch buttons
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback");
#endif
#ifdef __ANDROID__
    // SDL may start main again in the same process while Android is closing the screen; then there is
    // no screen to draw on, and an error box would hang the closing: leave at once
    static bool started = false;
    if (started) { NB_STEP("início repetido ignorado"); return 0; }
    started = true;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) return 1;
    NB_STEP("SDL iniciado; pasta %s", plat::exe_dir().c_str());
    apk_unpack(plat::exe_dir());
    NB_STEP("arquivos do APK prontos");
#endif
    std::string base = plat::exe_dir();
    // installed by a package (Linux .deb in /opt): the game folder is read-only, so saves, settings and
    // captures go to the user's data folder; the creator's default settings are copied there once
    std::string data = base;
    {
        Settings inst; inst.load(plat::join(base, "config/instalado.ini"));
        std::string id = inst.get("id", "");
        if (!id.empty()) {
            data = plat::join(plat::join(plat::user_data_dir(), "nativeboy/jogos"), id);
            plat::make_dirs(plat::join(data, "config"));
            for (auto& f : plat::list_dir(plat::join(base, "config"), false)) {
                if (f == "instalado.ini" || plat::file_exists(plat::join(plat::join(data, "config"), f))) continue;
                plat::copy_file(plat::join(plat::join(base, "config"), f), plat::join(plat::join(data, "config"), f));
            }
        }
    }
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        plat::message_box(nb_game_name(), std::string("Não foi possível iniciar o vídeo/áudio:\n") + SDL_GetError(), true);
        return 1;
    }
    unsigned rsz = 0;
    const unsigned char* rd = nb_game_rom(&rsz);
    std::vector<gb::u8> rom(rd, rd + rsz);

    GamePlayer player;
    player.user_dir = data;
    Settings pre; pre.load(plat::join(data, "config/settings.ini"));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fidelidade")) pre.set("fidelity", "1");
        if (!strcmp(argv[i], "--tela-cheia")) pre.set("fullscreen", "1");
    }
    int scale = std::max(2, std::min(8, pre.geti("janela", 4)));
    std::string title = nb_game_name();
#ifndef _WIN32
    {   // groups the window with its launcher (.desktop StartupWMClass) in Linux taskbars
        std::string exe = plat::file_name(argv[0]);
        SDL_SetHint("SDL_VIDEO_X11_WMCLASS", exe.c_str());
        SDL_SetHint(SDL_HINT_APP_NAME, nb_game_name());
    }
#endif
    gb::RomInfo ri; gb::parse_header(rom, ri);
    bool sgb_border = ri.sgb_flag == 0x03 && !ri.cgb_supported && pre.getb("sgb", nb_game_sgb()) && pre.getb("sgb_borda", true) && !pre.getb("fidelity", false);
    int base_w = sgb_border ? 256 : (pre.getb("widescreen", false) && !pre.getb("fidelity", false)) ? 256 : 160;
    int base_h = sgb_border ? 224 : 144;
    // last size/position, if it is still on a connected screen
    int wx = SDL_WINDOWPOS_CENTERED, wy = SDL_WINDOWPOS_CENTERED, ww = base_w * scale, wh = base_h * scale;
    if (pre.geti("janela_w", 0) >= 160 && pre.geti("janela_h", 0) >= 144) {
        int px = pre.geti("janela_x", 0), py = pre.geti("janela_y", 0);
        for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) {
            SDL_Rect r; SDL_GetDisplayUsableBounds(d, &r);
            if (px + 40 >= r.x && py >= r.y && px + 40 < r.x + r.w && py + 20 < r.y + r.h) { wx = px; wy = py; ww = pre.geti("janela_w", ww); wh = pre.geti("janela_h", wh); break; }
        }
    }
#ifdef NB_MOBILE
    SDL_Window* win = SDL_CreateWindow(title.c_str(), wx, wy, ww, wh, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN
#ifdef __ANDROID__
                                       | SDL_WINDOW_FULLSCREEN
#endif
                                       );
#else
    SDL_Window* win = SDL_CreateWindow(title.c_str(), wx, wy, ww, wh,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
#endif
    if (!win) { plat::message_box(nb_game_name(), std::string("Não foi possível criar a janela:\n") + SDL_GetError(), true); return 1; }
    NB_STEP("janela criada");
    {   // the game's own icon on the window / taskbar
        std::vector<uint32_t> ipx; int iw, ih;
        if (load_png(plat::join(base, "assets/icone.png"), ipx, iw, ih)) {
            SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormatFrom(ipx.data(), iw, ih, 32, iw * 4, SDL_PIXELFORMAT_ARGB8888);
            if (sf) { SDL_SetWindowIcon(win, sf); SDL_FreeSurface(sf); }
        }
    }
    SDL_SetWindowMinimumSize(win, 160, 144);
    dark_title_bar(win);
    if (pre.getb("janela_maximizada", false)) SDL_MaximizeWindow(win);
    if (pre.getb("sempre_no_topo", false)) SDL_SetWindowAlwaysOnTop(win, SDL_TRUE);
    SDL_ShowWindow(win);
    bool vsync = pre.getb("vsync", true);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { plat::message_box(nb_game_name(), std::string("Não foi possível iniciar o renderizador:\n") + SDL_GetError(), true); return 1; }
    NB_STEP("renderizador criado");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    ui::apply_style();
    ui::load_fonts(base, 18.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);
    NB_STEP("interface pronta");

    std::string err;
    // the converted code was verified with (or without) the SGB features: use the same default
    {
        Settings c; c.load(plat::join(data, "config/settings.ini"));
        if (!c.kv.count("sgb")) { c.path = plat::join(data, "config/settings.ini"); plat::make_dirs(plat::join(data, "config")); c.set("sgb", nb_game_sgb() ? "1" : "0"); c.save(); }
    }
    if (!player.start(rom, base, nb_game_name(), nb_game_resolver(), err)) {
        plat::message_box(nb_game_name(), "Não foi possível iniciar o jogo:\n" + err, true);
        return 1;
    }
    for (auto& kv : pre.kv) if (kv.first == "fidelity" || kv.first == "fullscreen") player.cfg.set(kv.first, kv.second);
    player.apply_settings();
    nb_user_init(player.m);
    NB_STEP("jogo iniciado");
    if (player.cfg.getb("fullscreen", false)) set_fullscreen(win, player, true);
    int cur_scale = scale;
    bool cur_vsync = vsync;

    bool quit = false, cur_console = false, first_console_check = true;
    bool auto_paused = false, cur_on_top = pre.getb("sempre_no_topo", false);
    Uint32 last_mouse = SDL_GetTicks(), last_click = 0;
    std::string cur_title = title;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) { NB_STEP("pedido para fechar (SDL_QUIT)"); player.power_off_begin(); }        // the screen turns off first (if enabled)
            // phone: the app goes to the background (home button, a call). Android may close it there
            // without warning, so everything is saved now; the game waits paused until it comes back
            if (e.type == SDL_APP_WILLENTERBACKGROUND || e.type == SDL_APP_TERMINATING) {
                NB_STEP(e.type == SDL_APP_TERMINATING ? "o Android vai fechar o app" : "app foi para o fundo");
                player.suspend_save();
                if (!player.paused) { player.paused = true; auto_paused = true; }
            }
            if (e.type == SDL_APP_DIDENTERFOREGROUND && auto_paused) { player.paused = false; auto_paused = false; player.frame_ready = true; }
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) { NB_STEP("janela fechada"); player.power_off_begin(); }
                // pause (and silence) while the window is in the background or minimized, like PC games
                bool lost = e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_MINIMIZED;
                bool back = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_RESTORED;
                if (lost && player.cfg.getb("pausar_sem_foco", true) && !player.paused && !player.video.on()) { player.paused = true; auto_paused = true; }
                if (back && auto_paused) { player.paused = false; auto_paused = false; }
                if (e.window.event == SDL_WINDOWEVENT_MOVED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) save_window(win, player);
            }
            if (e.type == SDL_MOUSEMOTION) last_mouse = SDL_GetTicks();
            // double click on the picture = full screen
            bool on_picture = true;                                   // (with the console look: only on its screen)
            if (e.type == SDL_MOUSEBUTTONDOWN && player.con.on) {
                int ww, wh, ow, oh; SDL_GetWindowSize(win, &ww, &wh); SDL_GetRendererOutputSize(ren, &ow, &oh);
                on_picture = player.console_hit(e.button.x * (float)ow / ww, e.button.y * (float)oh / wh).kind == GamePlayer::ConsoleHit::SCREEN;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && on_picture && !ImGui::GetIO().WantCaptureMouse) {
                if (e.button.clicks == 2 || SDL_GetTicks() - last_click < 350) player.want_fullscreen_toggle = true;
                last_click = SDL_GetTicks();
            }
            player.handle_event(e);
        }
        // the mouse pointer disappears while playing and comes back when moved
        bool playing = !player.menu_open && !player.start_screen && !player.ask_continue;
        bool hide = playing && SDL_GetTicks() - last_mouse > 2000 && player.cfg.getb("esconder_mouse", true);
        player.mouse_idle = SDL_GetTicks() - last_mouse > 2000;
        SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE);
        std::string want_title = player.paused && !player.menu_open ? title + " (pausado)" : title;
        if (want_title != cur_title) { SDL_SetWindowTitle(win, want_title.c_str()); cur_title = want_title; }
        bool on_top = player.cfg.getb("sempre_no_topo", false);
        if (on_top != cur_on_top) { SDL_SetWindowAlwaysOnTop(win, on_top ? SDL_TRUE : SDL_FALSE); cur_on_top = on_top; }
        if (player.want_quit) { NB_STEP("sair pelo jogo (menu ou console)"); player.want_quit = false; player.power_off_begin(); }
        if (player.power_off_done()) quit = true;
        if (player.want_minimize) { player.want_minimize = false; SDL_MinimizeWindow(win); }
#ifdef NB_MOBILE
        // (a phone has no windows to resize: the screen is the window)
        player.want_fullscreen_toggle = false; player.want_display_apply = false; player.want_resize = false;
        cur_console = player.console_on(); first_console_check = false;
#endif
        // console look on / off: window without borders, dragged by the body, shaped like the device
        if (player.console_on() != cur_console) {
            cur_console = player.console_on();
            SDL_SetWindowBordered(win, cur_console ? SDL_FALSE : SDL_TRUE);
            SDL_SetWindowHitTest(win, cur_console ? console_hit_test : nullptr, cur_console ? &player : nullptr);
            const bool keep_saved = first_console_check && pre.geti("janela_w", 0) > 0;   // (opening with it on: the saved size)
            if (cur_console && !is_fullscreen(win) && !keep_saved) {
                SDL_Rect usable{0, 0, 1280, 800};
                SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(win), &usable);
                int h = std::min(1100, (int)(usable.h * 0.92)), w = (int)(h * 1000.0 / 1640.0 / 0.97);
                SDL_SetWindowSize(win, w, h);
                SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            } else if (!cur_console && !first_console_check) player.want_resize = true;
        }
        first_console_check = false;
        if (player.want_fullscreen_toggle) {
            player.want_fullscreen_toggle = false;
            set_fullscreen(win, player, !is_fullscreen(win));
        }
        if (player.want_display_apply) {
            player.want_display_apply = false;
            if (is_fullscreen(win)) set_fullscreen(win, player, true);
        }
        int want_scale = std::max(2, std::min(8, player.cfg.geti("janela", 4)));
        if ((want_scale != cur_scale || player.want_resize) && !is_fullscreen(win) && !player.console_on()) {
            cur_scale = want_scale;
            player.want_resize = false;
            bool brd = player.m.sgb && player.cfg.getb("sgb_borda", true);
            SDL_SetWindowSize(win, (brd || player.m.widescreen ? 256 : 160) * cur_scale, (brd ? 224 : 144) * cur_scale);
            SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        bool want_vsync = player.cfg.getb("vsync", true);
        if (want_vsync != cur_vsync) { SDL_RenderSetVSync(ren, want_vsync ? 1 : 0); cur_vsync = want_vsync; }

        player.update();
        if (!player.paused && !player.menu_open) nb_user_frame(player.m);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        SDL_Rect area = {0, 0, ow, oh};
        player.draw_menu(area);
        player.draw_overlay(area);
        ImGui::Render();
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        player.render(ren, area);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), ren);
        SDL_RenderPresent(ren);
#ifdef __ANDROID__
        { static int shown = 0; if (shown < 3 && ++shown) NB_STEP("quadro %d desenhado", shown); }
#endif
    }
    save_window(win, player);
    player.stop();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    NB_STEP("jogo fechado");
#ifdef __ANDROID__
    exit(0);   // (the process ends with the game: the next start is a clean one)
#endif
    return 0;
}
