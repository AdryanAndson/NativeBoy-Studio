#include "player.h"
#include "platform.h"
#include "ui_common.h"
#include "../core/apu.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

using namespace gb;

// paths shown to the user use the system's own separator
static std::string natsep(std::string s) {
#ifdef _WIN32
    for (auto& c : s) if (c == '/') c = '\\';
#endif
    return s;
}

static const int NBTN = 10;     // 8 Game Boy buttons + turbo A + turbo B
static const char* BTN_IDS[NBTN] = {"right", "left", "up", "down", "a", "b", "select", "start", "turbo_a", "turbo_b"};
static const char* BTN_NAMES[NBTN] = {"Direita", "Esquerda", "Cima", "Baixo", "A", "B", "Select", "Start", "Turbo A", "Turbo B"};
static const SDL_Scancode DEF_KEYS[NBTN] = {SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
                                            SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN, SDL_SCANCODE_S, SDL_SCANCODE_A};
static const SDL_GameControllerButton DEF_PAD[NBTN] = {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP,
                                                       SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                                                       SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y};

bool apply_ips(std::vector<u8>& d, const std::vector<u8>& ips, std::string& err) {
    if (ips.size() < 8 || memcmp(ips.data(), "PATCH", 5) != 0) { err = "não é um arquivo IPS válido"; return false; }
    size_t p = 5;
    while (p + 3 <= ips.size()) {
        if (memcmp(&ips[p], "EOF", 3) == 0) return true;
        if (p + 5 > ips.size()) break;
        size_t off = ((size_t)ips[p] << 16) | (ips[p + 1] << 8) | ips[p + 2];
        size_t len = (ips[p + 3] << 8) | ips[p + 4];
        p += 5;
        if (len == 0) {
            if (p + 3 > ips.size()) break;
            size_t rl = (ips[p] << 8) | ips[p + 1];
            u8 v = ips[p + 2];
            p += 3;
            if (off + rl > d.size()) d.resize(off + rl, 0xFF);
            memset(&d[off], v, rl);
        } else {
            if (p + len > ips.size()) break;
            if (off + len > d.size()) d.resize(off + len, 0xFF);
            memcpy(&d[off], &ips[p], len);
            p += len;
        }
    }
    err = "arquivo IPS truncado";
    return false;
}

std::string GamePlayer::saves_dir() const { return plat::join(user_dir.empty() ? base_dir : user_dir, "saves"); }
std::string GamePlayer::config_dir() const { return plat::join(user_dir.empty() ? base_dir : user_dir, "config"); }

void GamePlayer::notify(const std::string& s) { notice = s; notice_until = SDL_GetTicks() + 2500; }

void GamePlayer::scan_mods() {
    mods.clear();
    std::string md = plat::join(base_dir, "assets/mods");
    std::string enabled = "," + cfg.get("mods", "") + ",";
    for (auto& d : plat::list_dir(md, true)) {
        ModInfo mi;
        mi.id = d; mi.dir = plat::join(md, d);
        Settings ini; ini.load(plat::join(mi.dir, "mod.ini"));
        mi.name = ini.get("nome", d);
        mi.desc = ini.get("descricao", "");
        mi.has_ips = plat::file_exists(plat::join(mi.dir, "patch.ips"));
        mi.has_cheats = plat::file_exists(plat::join(mi.dir, "cheats.txt"));
        mi.has_hd = plat::dir_exists(plat::join(mi.dir, "hd"));
        mi.has_palette = plat::file_exists(plat::join(mi.dir, "paleta.ini"));
        mi.enabled = enabled.find("," + d + ",") != std::string::npos;
        mods.push_back(mi);
    }
    hd_packs = plat::list_dir(plat::join(base_dir, "assets/hd"), true);
}

void GamePlayer::load_mods_and_rom(std::vector<u8>& rom) {
    mods_log.clear();
    cheats.clear();
    if (fidelity()) { mods_log = "Modo fidelidade ativo: mods desativados."; return; }
    for (auto& mi : mods) {
        if (!mi.enabled) continue;
        if (mi.has_ips) {
            std::vector<u8> ips; std::string err;
            plat::read_file(plat::join(mi.dir, "patch.ips"), ips);
            if (apply_ips(rom, ips, err)) mods_log += "[" + mi.name + "] patch de dados aplicado.\n";
            else mods_log += "[" + mi.name + "] patch ignorado: " + err + "\n";
        }
        if (mi.has_cheats) {
            std::string t; plat::read_text(plat::join(mi.dir, "cheats.txt"), t);
            int n = 0;
            for (auto& line : split_lines(t)) {
                std::string l = trim(line);
                unsigned code;
                if (l.size() >= 8 && l[0] != '#' && sscanf(l.c_str(), "%8x", &code) == 1 && (code >> 24) == 0x01) {
                    u8 val = (code >> 16) & 0xFF;
                    u16 addr = (u16)(((code & 0xFF) << 8) | ((code >> 8) & 0xFF));
                    cheats.push_back({addr, val}); n++;
                }
            }
            mods_log += "[" + mi.name + "] " + std::to_string(n) + " código(s) GameShark ativos.\n";
        }
    }
    std::string user_cheats;
    if (plat::read_text(plat::join(config_dir(), "cheats.txt"), user_cheats)) {
        for (auto& line : split_lines(user_cheats)) {
            unsigned code; std::string l = trim(line);
            if (l.size() >= 8 && l[0] != '#' && sscanf(l.c_str(), "%8x", &code) == 1 && (code >> 24) == 0x01)
                cheats.push_back({(u16)(((code & 0xFF) << 8) | ((code >> 8) & 0xFF)), (u8)((code >> 16) & 0xFF)});
        }
    }
}

void GamePlayer::apply_settings() {
    bool fid = fidelity();
    apply_dmg_palette(m, fid ? "original" : cfg.get("paleta", "classico"), cfg.get("cores_personalizadas", ""));
    m.widescreen = !fid && cfg.getb("widescreen", false) && !console_on();   // (the console look has its own 10:9 screen)
    m.wide_edge = (u8)std::max(0, std::min(2, cfg.geti("bordas_wide", 1)));
    m.wide_memory = cfg.getb("memoria_cenario", true);
    m.wide_hud = cfg.getb("hud_largo", true);
    m.wide_parked = cfg.getb("sprites_estacionados", false);
    m.wide_smart = cfg.getb("wide_inteligente", true);
    m.wide_places = cfg.getb("wide_lugares", true);
    m.wide_animate = cfg.getb("cenario_animado", true);
    m.wide_sprites = cfg.getb("sprites_extras", true);
    scout.reset();
    m.wide_extend = m.widescreen && (cfg.getb("ultrawide", false) || cfg.geti("linhas_extras", 0) > 0);
    m.wide_vis = 1.0f; m.wide_narrow = false; m.wide_bad_run = m.wide_good_run = 0;
    m.world_clear();
    m.no_sprite_limit = !fid && cfg.getb("sem_limite_sprites", false);
    frame_ready = true;
    m.color_correct = fid ? true : cfg.getb("correcao_cor", true);
    m.pal_dirty = true;
    float vol = cfg.geti("volume", 80) / 100.0f;
    m.apu->set_volume(vol);
    apply_mutes();
    for (int i = 0; i < NBTN; i++) {
        SDL_Scancode sc = ui::key_from_name(cfg.get(std::string("tecla_") + BTN_IDS[i], ""));
        keys[i] = sc == SDL_SCANCODE_UNKNOWN ? DEF_KEYS[i] : sc;
        SDL_GameControllerButton pb = SDL_GameControllerGetButtonFromString(cfg.get(std::string("controle_") + BTN_IDS[i], "").c_str());
        padb[i] = pb == SDL_CONTROLLER_BUTTON_INVALID ? DEF_PAD[i] : pb;
    }
    // HD pack
    hd = HdPack(); hd_error.clear();
    std::string pack = cfg.get("pacote_hd", "");
    std::string pack_dir;
    if (!pack.empty()) pack_dir = plat::join(plat::join(base_dir, "assets/hd"), pack);
    for (auto& mi : mods) if (mi.enabled && mi.has_hd && pack_dir.empty()) pack_dir = plat::join(mi.dir, "hd");
    if (!fid && !pack_dir.empty() && plat::dir_exists(pack_dir)) hd.load(pack_dir, hd_error);
    vs.fx.shadow = !fid && cfg.getb("sombra_sprites", false);
    vs.fx.vivid = fid ? 0 : std::max(0, std::min(100, cfg.geti("cores_vivas", 0)));
    vs.fx.vignette = !fid && cfg.getb("vinheta", false);
    m.track_pixels = !hd.empty() || dumper != nullptr || vs.fx.shadow;
    // soundtrack replacement
    music = MusicPack(); music_out.stop(); music_last = -1; music_active = false;
    if (!fid && cfg.getb("trilha_nova", true)) {
        std::string e;
        if (!music.load(plat::join(base_dir, "assets/musica"), e) && !studio_mods_dir.empty()) music.load(plat::join(studio_mods_dir, "musica"), e);
    }
    apply_mutes();
}

void GamePlayer::apply_mutes() {
    bool fid = fidelity();
    for (int i = 0; i < 4; i++)
        m.apu->set_mute(i, (!fid && cfg.getb("mudo" + std::to_string(i + 1), false)) || (music_active && (music.mute_mask >> i & 1)));
}

bool GamePlayer::start(const std::vector<u8>& rom, const std::string& bd, const std::string& name, NativeResolver* nat, std::string& err) {
    base_dir = bd; game_name = name; native = nat; emulation = nat == nullptr;
    game_id.clear();
    for (char c : name) game_id += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    if (game_id.empty()) game_id = "jogo";
    plat::make_dirs(saves_dir()); plat::make_dirs(config_dir());
    if (user_dir.empty() || user_dir == base_dir) {           // (an installed game never writes into its own folder)
        plat::make_dirs(plat::join(base_dir, "assets/mods")); plat::make_dirs(plat::join(base_dir, "assets/hd"));
    }
    cfg = Settings();
    cfg.load(plat::join(config_dir(), "settings.ini"));
    if (cfg.kv.empty()) {
        cfg.path = plat::join(config_dir(), "settings.ini");
        cfg.set("fidelity", "0"); cfg.set("vsync", "1"); cfg.set("fullscreen", "0"); cfg.set("escala", "inteira");
        cfg.set("filtro", "nitido"); cfg.set("paleta", "classico"); cfg.set("volume", "80"); cfg.set("correcao_cor", "1");
        cfg.set("janela", "4");
        cfg.save();
    }
#ifdef NB_MOBILE
    // phones: the drawn console when standing up, bigger menus, run-ahead off (battery)
    if (!cfg.kv.count("visual_console")) cfg.set("visual_console", "auto");
    // menu size from the screen density (Android's "dp": 160 dots per inch = 100%), a bit smaller than
    // the system text because the menu font is already large
    if (!cfg.kv.count("escala_interface")) {
        float ddpi = 0;
        int ui = 170;
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 60) ui = (int)(ddpi / 160.f * 80.f + 0.5f);
        cfg.seti("escala_interface", std::max(120, std::min(NB_UI_MAX, ui)));
    }
    if (!cfg.kv.count("run_ahead")) cfg.seti("run_ahead", 0);
    if (!haptic && SDL_InitSubSystem(SDL_INIT_HAPTIC) == 0 && SDL_NumHaptics() > 0) {
        haptic = SDL_HapticOpen(0);
        if (haptic && SDL_HapticRumbleInit(haptic) != 0) { SDL_HapticClose(haptic); haptic = nullptr; }
    }
#endif
    rom_original = rom;
    {   // the link cable tells the partner which game this is (CRC of the original ROM)
        uint32_t c = 0xFFFFFFFFu;
        for (u8 b : rom) { c ^= b; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
        netlink.set_game(c ^ 0xFFFFFFFFu, name);
    }
    scan_mods();
    std::vector<u8> r = rom;
    load_mods_and_rom(r);
    m.sgb_mode = !fidelity() && cfg.getb("sgb", true);
    if (!m.load_rom(r, err)) return false;
    apply_settings();
    std::vector<u8> sav;
    if (m.cart.info.has_battery && plat::read_file(plat::join(saves_dir(), game_id + ".sav"), sav)) m.load_save_data(sav);
    scenery_load();
    if (audio == 0) {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio) { m.apu->set_rate(have.freq); SDL_PauseAudioDevice(audio, 0); }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { SDL_GameController* c = SDL_GameControllerOpen(i); if (c) pads.push_back(c); }
    running = true; paused = false; menu_open = false;
    rewind_clear();
    music_out.rate = m.apu->sample_rate;
    // title screen like a PC game (it also offers "continue"); without it, a simple "continue?" prompt
    start_screen = !in_studio && cfg.getb("tela_inicial", true);
    ask_continue = !start_screen && !fidelity() && cfg.getb("continuar", true) && plat::file_exists(state_path(0)) && !in_studio;
    {
        std::string t;
        play_seconds = plat::read_text(plat::join(saves_dir(), game_id + ".tempo"), t) ? atof(t.c_str()) : 0;
        play_t0 = play_saved = SDL_GetTicks();
    }
    load_achievements();
    // opening image chosen by the game's creator
    splash_on = !fidelity() && cfg.getb("abertura", true) && plat::file_exists(plat::join(base_dir, "assets/abertura.png"));
    power_on_begin();
    splash_t0 = 0;
    last_battery_save = SDL_GetTicks();
    fps_t0 = SDL_GetTicks();
    if (!mods_log.empty()) notify("Mods carregados");
    return true;
}

void GamePlayer::write_diagnostics() {
    if (emulation) return;
    std::string s = "# Código que precisou do interpretador de compatibilidade nesta sessão\n"
                    "# Importe este arquivo no NativeBoy Studio (Converter > Corrigir problemas) e recompile.\n";
    for (u32 v : m.fb_stats.missing) { char b[16]; snprintf(b, sizeof b, "%02X:%04X\n", v >> 16, v & 0xFFFF); s += b; }
    if (!m.fb_stats.missing.empty()) plat::write_text(plat::join(config_dir(), "missing_code.txt"), s);
    char b[256];
    snprintf(b, sizeof b, "entradas_nativas=%llu\ninstrucoes_fallback_rom=%llu\ninstrucoes_fallback_ram=%llu\nquadros=%llu\n",
             (unsigned long long)m.fb_stats.native_blocks, (unsigned long long)m.fb_stats.rom_instr,
             (unsigned long long)m.fb_stats.ram_instr, (unsigned long long)m.frames);
    plat::write_text(plat::join(config_dir(), "estatisticas_nativas.txt"), b);
}

void GamePlayer::stop() {
    ahead_off();
    if (!running) return;
    if (m.cart.info.has_battery) {
        auto d = m.save_data();
        plat::write_file(plat::join(saves_dir(), game_id + ".sav"), d.data(), d.size());
    }
    write_diagnostics();
    // automatic state for "continue where you left off" (not while still asking, so it is not overwritten)
    if (!ask_continue && !start_screen && !fidelity() && cfg.getb("continuar", true) && !in_studio) {
        auto st = m.save_state();
        plat::write_file(state_path(0), st.data(), st.size());
        save_png(state_path(0) + ".png", m.fb, 160, 144);
        plat::write_text(state_path(0) + ".txt", plat::now_stamp());
    }
    wav.stop();
    gif.stop(); video.stop();
    for (auto& t : gallery_thumbs) if (t.second) SDL_DestroyTexture(t.second);
    gallery_thumbs.clear();
    music_out.stop();
    save_play_time();
    scenery_save();
    save_achievements();
    if (title_bg) { SDL_DestroyTexture(title_bg); title_bg = nullptr; }
    if (title_icon) { SDL_DestroyTexture(title_icon); title_icon = nullptr; } title_icon_tried = false;
    if (bloom_tex) { SDL_DestroyTexture(bloom_tex); bloom_tex = nullptr; }
    for (auto& t : thumbs) if (t.second) SDL_DestroyTexture(t.second);
    thumbs.clear();
    if (splash) { SDL_DestroyTexture(splash); splash = nullptr; }
    cfg.save();
    if (audio) { SDL_CloseAudioDevice(audio); audio = 0; }
    m.link = nullptr; netlink.close();
    for (auto* p : pads) { if (rumble_on) SDL_GameControllerRumble(p, 0, 0, 0); SDL_GameControllerClose(p); }
    rumble_on = false;
    pads.clear();
    if (tex) { SDL_DestroyTexture(tex); tex = nullptr; }
    if (sharp_tex) { SDL_DestroyTexture(sharp_tex); sharp_tex = nullptr; }
    free_side_textures();
    running = false;
}

void GamePlayer::reset() {
    ahead_off();
    std::string err;
    std::vector<u8> r = rom_original;
    load_mods_and_rom(r);
    std::vector<u8> sav = m.cart.info.has_battery ? m.save_data() : std::vector<u8>();
    m.sgb_mode = !fidelity() && cfg.getb("sgb", true);
    m.load_rom(r, err);
    rewind_clear();
    if (!sav.empty()) m.load_save_data(sav);
    apply_settings();
    notify("Jogo reiniciado");
}

bool GamePlayer::save_state(int slot) {
    ahead_off();
    auto s = m.save_state();
    bool ok = plat::write_file(state_path(slot), s.data(), s.size());
    if (ok) { save_png(state_path(slot) + ".png", m.fb, 160, 144); plat::write_text(state_path(slot) + ".txt", plat::now_stamp()); drop_thumb(slot); }
    notify(ok ? "Estado salvo no espaço " + std::to_string(slot) : "Falha ao salvar o estado");
    return ok;
}
bool GamePlayer::load_state(int slot) {
    ahead_off();
    std::vector<u8> s;
    bool ok = plat::read_file(state_path(slot), s) && m.load_state(s);
    if (ok) { rewind_clear(); frame_ready = true; music_last = -1; }
    std::string nm = slot == 0 ? std::string("salvamento automático") : "espaço " + std::to_string(slot);
    notify(ok ? "Estado carregado: " + nm : (plat::file_exists(state_path(slot)) ? "Este estado é de outra versão do jogo e não pôde ser carregado" : "Nada salvo no " + nm));
    return ok;
}
void GamePlayer::screenshot() {
    std::string dir = captures_dir();
    plat::make_dirs(dir);
    std::string path = plat::join(dir, game_id + "_" + plat::now_stamp() + ".png");
    bool ok;
    if (fidelity()) ok = save_png(path, m.fb, 160, 144);
    else if (vs.buf.empty() && m.widescreen) ok = save_png(path, m.fbw, Machine::WIDE_W, 144);
    else ok = save_png(path, vs.buf.empty() || vs.w == 160 ? m.fb : vs.buf.data(), vs.buf.empty() ? 160 : vs.w, vs.buf.empty() ? 144 : vs.h);
    notify(ok ? natsep("Captura salva em capturas/") + plat::file_name(path) : "Não foi possível salvar a captura");
}

// menu sections in the order they are listed (LB/RB and PageUp/PageDown follow it)
static const int MENU_ORDER[10] = {0, 1, 2, 3, 5, 4, 6, 8, 9, 7};
static bool g_menu_was_open = false;      // to put the keyboard focus on the current section when the menu opens
static int menu_step(int tab, int d) {
    int k = 0; while (k < 10 && MENU_ORDER[k] != tab) k++;
    return MENU_ORDER[((k + d) % 10 + 10) % 10];
}

void GamePlayer::handle_event(const SDL_Event& e) {
    ahead_off();                                        // keys and menus always act on the real game
    touch_event(e);                                     // fingers: touch controls and the drawn console's buttons
    // phone "back" button: opens / closes the menu
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat) {
        if (start_screen && !menu_open) return;
        menu_open = !menu_open; return;
    }
    const bool from_touch = (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) ? e.button.which == SDL_TOUCH_MOUSEID
                          : e.type == SDL_MOUSEMOTION ? e.motion.which == SDL_TOUCH_MOUSEID : false;
    if (con.on && running && ren && !from_touch && (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEMOTION)) {
        // the drawn buttons work with the mouse (fingers are followed one by one in touch_event)
        SDL_Window* w = SDL_GetWindowFromID(e.type == SDL_MOUSEMOTION ? e.motion.windowID : e.button.windowID);
        int ww = 1, wh = 1, ow = 1, oh = 1;
        if (w) SDL_GetWindowSize(w, &ww, &wh);
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const float sx = (float)ow / std::max(1, ww), sy = (float)oh / std::max(1, wh);
        const bool free_ui = !menu_open && !start_screen && !ask_continue && !ImGui::GetIO().WantCaptureMouse;
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && free_ui) {
            ConsoleHit h = console_hit(e.button.x * sx, e.button.y * sy);
            if (h.kind == ConsoleHit::BUTTON) mouse_buttons = h.buttons;
            else if (h.kind == ConsoleHit::MENU) menu_open = true;
            else if (h.kind == ConsoleHit::MINIMIZE) want_minimize = true;
            else if (h.kind == ConsoleHit::CLOSE) want_quit = true;
        }
        if (e.type == SDL_MOUSEMOTION && mouse_buttons && (e.motion.state & SDL_BUTTON_LMASK)) {   // sliding over the pad
            ConsoleHit h = console_hit(e.motion.x * sx, e.motion.y * sy);
            mouse_buttons = h.kind == ConsoleHit::BUTTON ? h.buttons : 0;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) mouse_buttons = 0;
    }
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
        if (c) { pads.push_back(c); notify(std::string("Controle conectado: ") + (SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "")); }
    }
    if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        for (size_t i = 0; i < pads.size(); i++)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[i])) == e.cdevice.which) { SDL_GameControllerClose(pads[i]); pads.erase(pads.begin() + i); notify("Controle desconectado"); break; }
    }
    if (splash_on && splash_t0 && SDL_GetTicks() - splash_t0 > 300 &&
        (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_MOUSEBUTTONDOWN)) { splash_on = false; return; }
    if (start_screen && !menu_open && !start_load && !ask_continue) {
        if (e.type == SDL_KEYDOWN) {
            SDL_Scancode sc = e.key.keysym.scancode;
            if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) start_move = -1;
            if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) start_move = 1;
            if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER || sc == SDL_SCANCODE_SPACE || sc == keys[4] || sc == keys[7]) start_go = true;
            if (sc != SDL_SCANCODE_ESCAPE && sc != SDL_SCANCODE_F1 && sc != SDL_SCANCODE_F11) return;
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) start_move = -1;
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) start_move = 1;
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A || e.cbutton.button == SDL_CONTROLLER_BUTTON_START) start_go = true;
            if (e.cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE) return;
        }
    }
    if (remap_index >= 0) {
        if (remap_index < NBTN && e.type == SDL_KEYDOWN) {
            if (e.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
                keys[remap_index] = e.key.keysym.scancode;
                cfg.set(std::string("tecla_") + BTN_IDS[remap_index], ui::key_name(e.key.keysym.scancode));
                cfg.save();
            }
            remap_index = -1;
        } else if (remap_index >= NBTN && e.type == SDL_CONTROLLERBUTTONDOWN) {
            int i = remap_index - NBTN;
            padb[i] = (SDL_GameControllerButton)e.cbutton.button;
            cfg.set(std::string("controle_") + BTN_IDS[i], SDL_GameControllerGetStringForButton(padb[i]));
            cfg.save();
            remap_index = -1;
        }
        return;
    }
    if (e.type == SDL_KEYDOWN && !e.key.repeat) {
        SDL_Scancode sc = e.key.keysym.scancode;
        if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F1) { menu_open = !menu_open; return; }
        if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT))) { want_fullscreen_toggle = true; return; }
        if (sc == SDL_SCANCODE_F12) { screenshot(); return; }
        if (sc == SDL_SCANCODE_F6) { toggle_gif(); return; }
        if (sc == SDL_SCANCODE_F7) { toggle_video(); return; }
        if (sc == SDL_SCANCODE_F5) { save_state(1); return; }
        if (sc == SDL_SCANCODE_F8) { load_state(1); return; }
        if (sc == SDL_SCANCODE_P || sc == SDL_SCANCODE_PAUSE) { paused = !paused; notify(paused ? "Pausado" : "Continuando"); return; }
        if (sc == SDL_SCANCODE_TAB) fast = true;
    }
    if (e.type == SDL_KEYUP && e.key.keysym.scancode == SDL_SCANCODE_TAB) fast = false;
    if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) menu_open = !menu_open;
    // with the menu open, Start closes it (the menu itself is navigated with the d-pad, A confirms, B goes back)
    if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START && menu_open && !ask_continue) menu_open = false;
    if (e.type == SDL_CONTROLLERBUTTONDOWN && menu_open) {     // LB/RB switch tabs
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) want_tab = menu_step(menu_tab, -1);
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) want_tab = menu_step(menu_tab, 1);
    }
    if (e.type == SDL_KEYDOWN && menu_open) {                 // PageUp / PageDown switch sections too
        if (e.key.keysym.scancode == SDL_SCANCODE_PAGEUP) want_tab = menu_step(menu_tab, -1);
        if (e.key.keysym.scancode == SDL_SCANCODE_PAGEDOWN) want_tab = menu_step(menu_tab, 1);
    }
}

void GamePlayer::update() {
    if (!running) return;
    ahead_off();                                        // back to the real state before anything else
    // input
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    kb_state = 0; pad_state = 0; axis_state = 0;
    bool block = menu_open || ImGui::GetIO().WantCaptureKeyboard;
    // turbo: the button alternates pressed/released several times per second
    int thz = std::max(5, std::min(30, cfg.geti("turbo_hz", 15)));
    bool tphase = ((m.frames * thz * 2 / 60) & 1) == 0;
    bool fast_pad = false, rewind_in = false;
    if (!block) {
        for (int i = 0; i < 8; i++) if (ks[keys[i]]) kb_state |= (u8)(1 << i);
        if (tphase && ks[keys[8]]) kb_state |= BTN_A;
        if (tphase && ks[keys[9]]) kb_state |= BTN_B;
        bool bs_used = false;
        for (int i = 0; i < NBTN; i++) if (keys[i] == SDL_SCANCODE_BACKSPACE) bs_used = true;
        if (ks[SDL_SCANCODE_BACKSPACE] && !bs_used) rewind_in = true;
    }
    for (auto* p : pads) {
        for (int i = 0; i < 8; i++) if (SDL_GameControllerGetButton(p, padb[i])) pad_state |= (u8)(1 << i);
        if (tphase && SDL_GameControllerGetButton(p, padb[8])) pad_state |= BTN_A;
        if (tphase && SDL_GameControllerGetButton(p, padb[9])) pad_state |= BTN_B;
        if (SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) rewind_in = true;
        if (SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) fast_pad = true;
        int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax > 16000) axis_state |= BTN_RIGHT; if (ax < -16000) axis_state |= BTN_LEFT;
        if (ay > 16000) axis_state |= BTN_DOWN; if (ay < -16000) axis_state |= BTN_UP;
        if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK) && SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_START)) { menu_open = true; }
    }
    if (menu_open) pad_state = axis_state = 0;
    if (!menu_open && rewind_touch_hold) rewind_in = true;
    if (!menu_open && fast_touch_hold) fast_pad = true;
    u8 b = kb_state | pad_state | axis_state | (con.on && !menu_open ? mouse_buttons : 0) | (!menu_open ? touch_bits : 0);
    if ((b & BTN_LEFT) && (b & BTN_RIGHT)) b &= (u8)~(BTN_LEFT | BTN_RIGHT);
    if ((b & BTN_UP) && (b & BTN_DOWN)) b &= (u8)~(BTN_UP | BTN_DOWN);
    if ((b & ~m.buttons) && !menu_open) { m.iflag |= IRQ_JOYPAD; m.force_event(); }
    m.buttons = b;
    // link cable
    netlink.update();
    m.link = netlink.status() == 2 ? &netlink : nullptr;
    // MBC7 tilt: right stick, or I/J/K/L keys
    if (m.cart.info.mbc == 7) {
        float tx = 0, ty = 0;
        if (!block) {
            if (ks[SDL_SCANCODE_J]) tx -= 1; if (ks[SDL_SCANCODE_L]) tx += 1;
            if (ks[SDL_SCANCODE_I]) ty -= 1; if (ks[SDL_SCANCODE_K]) ty += 1;
        }
        for (auto* p : pads) {
            float ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f, ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            if (std::fabs(ax) > 0.15f) tx += ax;
            if (std::fabs(ay) > 0.15f) ty += ay;
        }
        if (menu_open) tx = ty = 0;
        // smooth like a real tilt instead of jumping
        m.tilt_x += (std::max(-1.f, std::min(1.f, tx)) - m.tilt_x) * 0.25f;
        m.tilt_y += (std::max(-1.f, std::min(1.f, ty)) - m.tilt_y) * 0.25f;
    }
    // rumble cartridges
    bool want_rumble = m.cart.info.has_rumble && m.cart.rumble && !menu_open && !paused && cfg.getb("vibracao", true);
    if (want_rumble || rumble_on) {
        for (auto* p : pads) SDL_GameControllerRumble(p, want_rumble ? 0xB000 : 0, want_rumble ? 0xB000 : 0, want_rumble ? 120 : 0);
        rumble_on = want_rumble;
    }

    if (power_state == 1 || power_state == 2 || splash_on || ask_continue || start_screen) { SDL_Delay(5); return; }
    if (paused || menu_open) { SDL_Delay(5); return; }
    // rewind: one saved moment back per displayed frame (2 game frames each)
    rewinding = rewind_in && !fidelity() && cfg.getb("rebobinar", true);
    if (rewinding) {
        static Uint32 last = 0;
        Uint32 now = SDL_GetTicks();
        if (!rw_hold_t0) { rw_hold_t0 = now; rw_back_seconds = 0; }
        // holding the key longer goes faster: 2x, then 4x after 2 s, then 8x after 5 s
        const Uint32 held = now - rw_hold_t0;
        const int n = held > 5000 ? 4 : held > 2000 ? 2 : 1;
        if (now - last >= 16) { last = now; if (rewind_step(n)) frame_ready = true; }
        m.apu->out.clear(); rw_out_mark = 0;
        SDL_Delay(1);
        return;
    }
    rw_hold_t0 = 0;
    int frames = 1;
    if (audio) {
        Uint32 q = SDL_GetQueuedAudioSize(audio) / (sizeof(float) * 2);
        int per_frame = m.apu->sample_rate / 60;
        if (q > (Uint32)per_frame * 5) frames = 0;
        else if (q < (Uint32)per_frame * 2) frames = 2;
    } else {
        static Uint64 last = SDL_GetPerformanceCounter();
        static double acc = 0;
        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / SDL_GetPerformanceFrequency();
        last = now;
        frames = 0;
        const double step = (1.0 / 59.7275) * 100.0 / std::max(50, std::min(200, fidelity() ? 100 : cfg.geti("velocidade_jogo", 100)));   // game speed mod
        while (acc >= step && frames < 3) { acc -= step; frames++; }
        if (acc > 0.2) acc = 0;
    }
    bool going_fast = fast || fast_pad;
    fast_shown = going_fast;
    if (going_fast) frames = std::max(2, std::min(10, cfg.geti("velocidade_rapida", 4)));
    if (frames == 0) { SDL_Delay(1); return; }
    for (int i = 0; i < frames; i++) {
        if (m.dbg_bp && m.dbg_hit) break;                 // debugger stopped on a breakpoint
        m.run_frame(native);
        if (m.dbg_hit) { paused = true; notify("Parado no ponto de parada"); break; }
        if (!fidelity()) for (auto& c : cheats) wr(m, c.addr, c.val);
        if (m.widescreen && !going_fast && !fidelity() && cfg.getb("cenario_frente", true)) scout.step(m, 2);
        if (dumper) dumper->collect(m);
        if (!fidelity() && cfg.getb("rebobinar", true) && (++rw_tick & 1) == 0) rewind_capture();
        music_update();
        check_achievements();
        if (gif.on() || video.on()) record_frame();
        if (!going_fast) play_seconds += 1.0 / 59.7275;
        fps_frames++;
    }
    if (SDL_GetTicks() - play_saved > 60000) {
        save_play_time();
        static Uint32 scenery_saved = SDL_GetTicks();                 // the remembered scenery, every 3 minutes
        if (SDL_GetTicks() - scenery_saved > 180000) { scenery_saved = SDL_GetTicks(); scenery_save(); }   // (so a crash or power cut keeps it)
    }
    frame_ready = true;
    if (music_out.playing()) music_out.mix(m.apu->out);
    if (wav.on() && !going_fast) wav.add(m.apu->out.data(), m.apu->out.size());
    if (video.on()) video.add_audio(m.apu->out.data(), m.apu->out.size());
    rewind_audio_mark();       // sound not yet stored with a rewind moment
    if (audio) {
        if (!going_fast && !m.apu->out.empty()) SDL_QueueAudio(audio, m.apu->out.data(), (Uint32)(m.apu->out.size() * sizeof(float)));
        m.apu->out.clear();
    } else m.apu->out.clear();
    // run-ahead: show the picture 1 or 2 frames in the future (what the buttons pressed now will cause)
    {
        const int n = std::max(0, std::min(2, cfg.geti("run_ahead", 1)));
        if (n && !fidelity() && !going_fast && !m.dbg_bp && !m.link && !in_studio)
            m.ahead_begin(ahead, n, native, [&] { for (auto& c : cheats) wr(m, c.addr, c.val); });
    }
    Uint32 now = SDL_GetTicks();
    if (now - fps_t0 >= 1000) { fps = fps_frames * 1000.0 / (now - fps_t0); fps_frames = 0; fps_t0 = now; }
    if (m.cart.info.has_battery && m.cart.ram_dirty && now - last_battery_save > 3000) {
        auto d = m.save_data();
        plat::write_file(plat::join(saves_dir(), game_id + ".sav"), d.data(), d.size());
        m.cart.ram_dirty = false;
        last_battery_save = now;
    }
}

static const double NB_HALF_PI = 1.57079632679489661923;
// ---------------------------------------------------------------- side fill helpers
static void grad_rect(SDL_Renderer* r, const SDL_Rect& q, SDL_Color c0, SDL_Color c1, bool horizontal) {
    if (q.w <= 0 || q.h <= 0) return;
    SDL_Vertex v[4];
    float x0 = (float)q.x, y0 = (float)q.y, x1 = (float)(q.x + q.w), y1 = (float)(q.y + q.h);
    v[0] = {{x0, y0}, c0, {0, 0}};
    v[1] = {{x1, y0}, horizontal ? c1 : c0, {0, 0}};
    v[2] = {{x1, y1}, c1, {0, 0}};
    v[3] = {{x0, y1}, horizontal ? c0 : c1, {0, 0}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}
static void round_rect(SDL_Renderer* r, float x, float y, float w, float h, float rad, SDL_Color c) {
    rad = std::min(rad, std::min(w, h) / 2);
    std::vector<SDL_Vertex> v;
    v.push_back({{x + w / 2, y + h / 2}, c, {0, 0}});
    const float cx[4] = {x + w - rad, x + rad, x + rad, x + w - rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= 8; i++) {
            float a = (float)(k * NB_HALF_PI + i * NB_HALF_PI / 8);
            v.push_back({{cx[k] + rad * std::cos(a), cy[k] - rad * std::sin(a)}, c, {0, 0}});
        }
    std::vector<int> idx;
    int n = (int)v.size() - 1;
    for (int i = 1; i <= n; i++) { idx.push_back(0); idx.push_back(i); idx.push_back(i % n + 1); }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}
static void circle(SDL_Renderer* r, float x, float y, float rad, SDL_Color c, SDL_Color edge) {
    std::vector<SDL_Vertex> v; std::vector<int> idx;
    v.push_back({{x, y}, c, {0, 0}});
    for (int i = 0; i < 24; i++) { float a = (float)(i * 2 * NB_HALF_PI * 2 / 24); v.push_back({{x + rad * std::cos(a), y + rad * std::sin(a)}, edge, {0, 0}}); }
    for (int i = 1; i <= 24; i++) { idx.push_back(0); idx.push_back(i); idx.push_back(i % 24 + 1); }
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void GamePlayer::free_side_textures() {
    if (amb_a) SDL_DestroyTexture(amb_a);
    if (amb_b) SDL_DestroyTexture(amb_b);
    if (bg_img) SDL_DestroyTexture(bg_img);
    amb_a = amb_b = bg_img = nullptr; bg_path.clear();
}

std::string GamePlayer::background_image_path() const {
    std::string c = plat::join(config_dir(), "fundo.png");          // chosen by the player
    if (plat::file_exists(c)) return c;
    std::string a = plat::join(base_dir, "assets/fundo.png");       // shipped by the game's creator
    return plat::file_exists(a) ? a : "";
}

// ambient light: the picture shrunk to a few pixels and stretched back with smooth filtering
bool GamePlayer::ambient_update(SDL_Renderer* r, const SDL_Rect& src) {
    if (!amb_a) {
        amb_a = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 40, 24);
        amb_b = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 10, 6);
        if (amb_a) { SDL_SetTextureScaleMode(amb_a, SDL_ScaleModeLinear); SDL_SetTextureBlendMode(amb_a, SDL_BLENDMODE_NONE); }
        if (amb_b) { SDL_SetTextureScaleMode(amb_b, SDL_ScaleModeLinear); SDL_SetTextureBlendMode(amb_b, SDL_BLENDMODE_NONE); }
        amb_dirty = true;
    }
    if (!amb_a || !amb_b) return false;
    if (amb_dirty) {
        SDL_Texture* old = SDL_GetRenderTarget(r);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);     // see-through parts keep their colour here
        SDL_SetRenderTarget(r, amb_a); SDL_RenderCopy(r, tex, &src, nullptr);
        SDL_SetRenderTarget(r, amb_b); SDL_RenderCopy(r, amb_a, nullptr, nullptr);
        SDL_SetRenderTarget(r, old);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        amb_dirty = false;
    }
    return true;
}

void GamePlayer::draw_sides(SDL_Renderer* r, const SDL_Rect& area, const SDL_Rect& dst, const SDL_Rect& src, const std::string& mode) {
    if (dst.x <= area.x && dst.y <= area.y && dst.w >= area.w && dst.h >= area.h) return;   // nothing visible around the game
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    std::string md = mode;
    if (md == "imagem") {
        std::string pth = background_image_path();
        if (pth.empty()) md = "desfocado";
        else {
            if (pth != bg_path) {
                if (bg_img) SDL_DestroyTexture(bg_img);
                bg_img = nullptr; bg_path = pth;
                std::vector<uint32_t> px;
                if (load_png(pth, px, bg_w, bg_h)) {
                    bg_img = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, bg_w, bg_h);
                    SDL_UpdateTexture(bg_img, nullptr, px.data(), bg_w * 4);
                    SDL_SetTextureScaleMode(bg_img, SDL_ScaleModeLinear);
                }
            }
            if (!bg_img) md = "desfocado";
            else {   // "cover": fills the whole window keeping the image's proportions
                double sc = std::max((double)area.w / bg_w, (double)area.h / bg_h);
                SDL_Rect q{0, 0, (int)(bg_w * sc + 1), (int)(bg_h * sc + 1)};
                q.x = area.x + (area.w - q.w) / 2; q.y = area.y + (area.h - q.h) / 2;
                SDL_SetTextureColorMod(bg_img, 200, 200, 200);
                SDL_RenderCopy(r, bg_img, nullptr, &q);
            }
        }
    }
    if (md == "desfocado") {
        if (ambient_update(r, src)) {
            // extend a little past the window so the blurred edges do not show
            SDL_Rect q{area.x - area.w / 8, area.y - area.h / 8, area.w + area.w / 4, area.h + area.h / 4};
            SDL_SetTextureColorMod(amb_b, 120, 120, 120);
            SDL_RenderCopy(r, amb_b, nullptr, &q);
        } else md = "bordas";
    }
    if (md == "bordas") {
        // the outermost row/column of the picture continues to the window edge, fading out
        const float fx = (float)src.w / dst.w, fy = (float)src.h / dst.h;
        (void)fx;
        if (dst.x > area.x) {
            SDL_Rect sl{src.x, src.y, std::max(1, (int)(src.w / (float)dst.w + 0.5f)), src.h};
            SDL_Rect sr{src.x + src.w - sl.w, src.y, sl.w, src.h};
            SDL_Rect dl{area.x, dst.y, dst.x - area.x, dst.h}, dr{dst.x + dst.w, dst.y, area.x + area.w - dst.x - dst.w, dst.h};
            SDL_RenderCopy(r, tex, &sl, &dl); SDL_RenderCopy(r, tex, &sr, &dr);
            grad_rect(r, dl, {0, 0, 0, 220}, {0, 0, 0, 40}, true);
            grad_rect(r, dr, {0, 0, 0, 40}, {0, 0, 0, 220}, true);
        }
        if (dst.y > area.y) {
            SDL_Rect st{src.x, src.y, src.w, std::max(1, (int)(fy + 0.5f))};
            SDL_Rect sb{src.x, src.y + src.h - st.h, src.w, st.h};
            SDL_Rect dt{dst.x, area.y, dst.w, dst.y - area.y}, db{dst.x, dst.y + dst.h, dst.w, area.y + area.h - dst.y - dst.h};
            SDL_RenderCopy(r, tex, &st, &dt); SDL_RenderCopy(r, tex, &sb, &db);
            grad_rect(r, dt, {0, 0, 0, 220}, {0, 0, 0, 40}, false);
            grad_rect(r, db, {0, 0, 0, 40}, {0, 0, 0, 220}, false);
            // corners
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        }
    }
    if (md == "moldura") {
        // a console shell around the screen, drawn procedurally
        struct Theme { const char* id; SDL_Color body, body2, bezel; };
        static const Theme themes[] = {
            {"cinza", {205, 202, 196, 255}, {172, 168, 162, 255}, {88, 88, 104, 255}},
            {"roxo", {108, 72, 176, 255}, {74, 46, 128, 255}, {36, 30, 52, 255}},
            {"preto", {52, 54, 60, 255}, {28, 29, 33, 255}, {14, 14, 18, 255}},
            {"verde", {56, 128, 96, 255}, {34, 88, 64, 255}, {22, 40, 34, 255}},
            {"vermelho", {196, 52, 60, 255}, {140, 30, 38, 255}, {44, 24, 28, 255}},
            {"azul", {52, 104, 196, 255}, {30, 64, 140, 255}, {22, 28, 48, 255}},
            {"translucido", {132, 98, 208, 255}, {86, 58, 158, 255}, {30, 24, 46, 255}},
            {"indigo", {84, 80, 168, 255}, {54, 50, 122, 255}, {24, 24, 44, 255}}};
        std::string tid = cfg.get("moldura_cor", "cinza");
        const Theme* t = &themes[0];
        for (auto& th : themes) if (tid == th.id) t = &th;
        grad_rect(r, area, t->body, t->body2, false);
        float pad = std::max(8.f, dst.h * 0.07f);
        float bx = dst.x - pad, by = dst.y - pad * 1.25f, bw = dst.w + pad * 2, bh = dst.h + pad * 2.25f;
        round_rect(r, bx + 3, by + 5, bw, bh, pad * 0.6f, {0, 0, 0, 70});                       // drop shadow
        round_rect(r, bx, by, bw, bh, pad * 0.6f, t->bezel);
        float lh = std::max(2.f, pad * 0.07f), ly = by + pad * 0.45f;                           // two decorative stripes
        SDL_FRect s1{bx + pad * 0.5f, ly, bw - pad, lh}, s2{bx + pad * 0.5f, ly + lh * 2.2f, bw - pad, lh};
        SDL_SetRenderDrawColor(r, 140, 40, 90, 255); SDL_RenderFillRectF(r, &s1);
        SDL_SetRenderDrawColor(r, 40, 60, 140, 255); SDL_RenderFillRectF(r, &s2);
        if (pad >= 14) {                                                                         // power LED
            float lx = bx + pad * 0.5f, lyy = dst.y + dst.h * 0.32f, lr = std::max(3.f, pad * 0.13f);
            circle(r, lx, lyy, lr * 2.4f, {255, 40, 40, 70}, {255, 40, 40, 0});
            circle(r, lx, lyy, lr, {255, 90, 80, 255}, {200, 20, 20, 255});
        }
        if (tid == "translucido") {
            // see-through plastic: faint outlines of what would be inside the shell
            SDL_SetRenderDrawColor(r, 255, 255, 255, 16);
            for (int k = 0; k < 6; k++) {
                SDL_Rect ln{area.x + area.w * (k * 2 + 1) / 13, area.y + area.h / 9 + (k % 3) * area.h / 7, area.w / 16, 2};
                SDL_RenderFillRect(r, &ln);
                SDL_Rect lv{ln.x + ln.w, ln.y, 2, area.h / 6};
                SDL_RenderFillRect(r, &lv);
            }
        }
        const float room = (float)(dst.x - area.x) - pad;                                        // free width at each side
        if (cfg.get("moldura_formato", "portatil") == "portatil" && room > dst.h * 0.30f) {
            // handheld look: direction pad on the left, two round buttons on the right, two small pills below them
            const float u = std::min(room, dst.h * 0.55f), cy = dst.y + dst.h * 0.52f;
            const float lx = area.x + ((dst.x - pad) - area.x) / 2.f, rx = area.x + area.w - ((dst.x - pad) - area.x) / 2.f;
            const SDL_Color dark{30, 30, 36, 255}, dark2{52, 52, 60, 255}, sh{0, 0, 0, 60};
            const float arm = u * 0.44f, th = u * 0.16f;
            round_rect(r, lx - arm / 2 + 2, cy - th / 2 + 3, arm, th, th * 0.2f, sh);
            round_rect(r, lx - th / 2 + 2, cy - arm / 2 + 3, th, arm, th * 0.2f, sh);
            round_rect(r, lx - arm / 2, cy - th / 2, arm, th, th * 0.2f, dark);
            round_rect(r, lx - th / 2, cy - arm / 2, th, arm, th * 0.2f, dark);
            circle(r, lx, cy, th * 0.28f, dark2, dark2);
            const float br = u * 0.105f;
            const SDL_Color b1 = tid == "cinza" ? SDL_Color{150, 40, 96, 255} : SDL_Color{44, 44, 52, 255};
            const SDL_Color b2 = tid == "cinza" ? SDL_Color{110, 24, 66, 255} : SDL_Color{24, 24, 30, 255};
            circle(r, rx + br * 1.25f + 2, cy - br * 0.7f + 3, br, sh, sh); circle(r, rx - br * 1.25f + 2, cy + br * 0.7f + 3, br, sh, sh);
            circle(r, rx + br * 1.25f, cy - br * 0.7f, br, b1, b2);
            circle(r, rx - br * 1.25f, cy + br * 0.7f, br, b1, b2);
            const float pw = u * 0.15f, ph = u * 0.045f, py = cy + u * 0.36f;
            round_rect(r, rx - pw * 1.15f, py, pw, ph, ph / 2, dark2);
            round_rect(r, rx + pw * 0.15f, py, pw, ph, ph / 2, dark2);
            for (int k = 0; k < 5; k++) {                                                        // speaker slots
                SDL_FRect sl{lx - u * 0.16f + k * u * 0.08f, cy + u * 0.38f, u * 0.03f, u * 0.14f};
                SDL_SetRenderDrawColor(r, 0, 0, 0, 70); SDL_RenderFillRectF(r, &sl);
            }
        }
        SDL_Rect in{dst.x - 2, dst.y - 2, dst.w + 4, dst.h + 4};                                // inner shadow line
        SDL_SetRenderDrawColor(r, 0, 0, 0, 160); SDL_RenderDrawRect(r, &in);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// the window: the console drawing (if on), the power animation, then the game in its screen
void GamePlayer::render(SDL_Renderer* r, const SDL_Rect& win) {
    if (!running) return;
    ren = r;
    SDL_Rect area = win;
    console_layout(win);
    touch_layout(win);
    if (con.on) { console_draw(r, win); area = con.screen; }
    else area = touch_game_area(win);
    if (power_state == 1 || power_state == 2) {
        SDL_Rect scr = area;
        if (!con.on && power_state == 1) {               // without the console: a screen-shaped box in the middle
            const double k = std::min(win.w / 160.0, win.h / 144.0) * 0.9;
            scr = {win.x + (int)(win.w - 160 * k) / 2, win.y + (int)(win.h - 144 * k) / 2, (int)(160 * k), (int)(144 * k)};
        }
        if (power_state == 2) render_game(r, area);    // turning off: over the last picture
        power_screen = scr;
        if (power_render(r, scr)) { if (con.on) console_draw_front(r); return; }
    }
    render_game(r, area);
    if (con.on) console_draw_front(r);
}

void GamePlayer::render_game(SDL_Renderer* r, const SDL_Rect& area) {
    if (splash_on) {     // opening image: fades in, holds, fades out (any key skips)
        if (!splash) {
            std::vector<uint32_t> px;
            if (load_png(plat::join(base_dir, "assets/abertura.png"), px, splash_w, splash_h)) {
                splash = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, splash_w, splash_h);
                SDL_UpdateTexture(splash, nullptr, px.data(), splash_w * 4);
                SDL_SetTextureScaleMode(splash, SDL_ScaleModeLinear);
                SDL_SetTextureBlendMode(splash, SDL_BLENDMODE_BLEND);
            }
            if (!splash) { splash_on = false; return; }
            splash_t0 = SDL_GetTicks();
        }
        Uint32 t = SDL_GetTicks() - splash_t0;
        const Uint32 total = 2600;
        if (t >= total) { splash_on = false; SDL_DestroyTexture(splash); splash = nullptr; }
        else {
            float a = t < 500 ? t / 500.f : t > total - 500 ? (total - t) / 500.f : 1.f;
            double sc = std::min((double)area.w / splash_w, (double)area.h / splash_h);
            SDL_Rect q{0, 0, (int)(splash_w * sc), (int)(splash_h * sc)};
            q.x = area.x + (area.w - q.w) / 2; q.y = area.y + (area.h - q.h) / 2;
            SDL_SetTextureAlphaMod(splash, (Uint8)(a * 255));
            SDL_RenderCopy(r, splash, nullptr, &q);
            return;
        }
    }
    if (start_screen && !splash_on) {    // title screen background: opening image, wallpaper or the game icon
        if (!title_bg) {
            std::vector<uint32_t> px;
            for (const char* f : {"assets/abertura.png", "assets/fundo.png", "assets/icone.png"})
                if (load_png(plat::join(base_dir, f), px, title_w, title_h)) {
                    title_bg = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, title_w, title_h);
                    SDL_UpdateTexture(title_bg, nullptr, px.data(), title_w * 4);
                    SDL_SetTextureScaleMode(title_bg, SDL_ScaleModeLinear);
                    break;
                }
        }
        if (title_bg) {
            double sc = std::max((double)area.w / title_w, (double)area.h / title_h);
            SDL_Rect q{0, 0, (int)(title_w * sc), (int)(title_h * sc)};
            q.x = area.x + (area.w - q.w) / 2; q.y = area.y + (area.h - q.h) / 2;
            SDL_SetTextureColorMod(title_bg, 90, 90, 90);
            SDL_RenderCopy(r, title_bg, nullptr, &q);
        }
        return;
    }
    bool fid = fidelity();
    int filter = fid ? FILTER_NEAREST : filter_from_id(cfg.get("filtro", "nitido"));
    if (frame_ready || !tex) {
        const uint32_t* px = vs.process(m, filter, hd.empty() ? nullptr : &hd, !fid && cfg.getb("rastro_lcd", false), cfg.getb("sgb_borda", true));
        if (!tex || tex_w != vs.w || tex_h != vs.h || tex_linear != vs.linear) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vs.w, vs.h);
            tex_w = vs.w; tex_h = vs.h; tex_linear = vs.linear;
            SDL_SetTextureScaleMode(tex, vs.linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        }
        std::string cvd = fid ? "nenhum" : cfg.get("daltonismo", "nenhum");
        if (cvd != "nenhum") { cvd_buf.assign(px, px + (size_t)vs.w * vs.h); daltonize(cvd_buf.data(), cvd_buf.size(), cvd); px = cvd_buf.data(); }
        // widescreen: the parts of the sides the game has not drawn yet become see-through, with a soft edge,
        // so the background (blurred picture, image...) shows there instead of a flat strip
        soft_active = false;
        if (!fid && m.widescreen && m.wide_edge >= 1 && cfg.getb("wide_suave", true) && vs.base_w == Machine::WIDE_W && vs.base_h == 144) {
            const int W = Machine::WIDE_W, X0 = Machine::WIDE_X0, R = 16;
            soft_alpha.assign((size_t)W * 144, 255);
            int unknown = 0;
            for (int y = 0; y < 144; y++) {
                const u8* fl = &m.wflags[y * W];
                uint8_t* a = &soft_alpha[(size_t)y * W];
                auto unk = [&](int x) { u8 f = fl[x]; return !(f & 3) && (f & 4) && !(f & 8); };
                // left side: distance from the nearest drawn pixel towards the outer edge
                int d = 0;
                for (int x = X0 - 1; x >= 0; x--) { if (unk(x)) { d++; a[x] = (uint8_t)std::max(0, 255 - d * 255 / R); unknown++; } else d = 0; }
                d = 0;
                for (int x = X0 + 160; x < W; x++) { if (unk(x)) { d++; a[x] = (uint8_t)std::max(0, 255 - d * 255 / R); unknown++; } else d = 0; }
            }
            if (unknown) {
                // soften the steps between lines
                std::vector<uint8_t> v(soft_alpha);
                for (int y = 1; y < 143; y++) for (int x = 0; x < W; x++) {
                    if (x == X0) { x = X0 + 159; continue; }
                    size_t i = (size_t)y * W + x;
                    soft_alpha[i] = (uint8_t)std::min<int>(v[i], (v[i - W] + 2 * v[i] + v[i + W]) / 4);
                }
                soft_buf.assign(px, px + (size_t)vs.w * vs.h);
                const int kx = vs.w / W, ky = vs.h / 144;
                for (int y = 0; y < vs.h; y++) {
                    const uint8_t* a = &soft_alpha[(size_t)(y / ky) * W];
                    uint32_t* o = &soft_buf[(size_t)y * vs.w];
                    for (int x = 0; x < vs.w; x++) { uint8_t al = a[x / kx]; if (al != 255) o[x] = (o[x] & 0x00FFFFFFu) | ((uint32_t)al << 24); }
                }
                px = soft_buf.data();
                soft_active = true;
            }
        }
        if (!fid && vs.extended) {
            // extended view: everything that is not known (not drawn by the game nor remembered) fades out
            const int W = Machine::XW, H = Machine::XH, R = 16;
            static std::vector<uint16_t> dist;
            dist.assign((size_t)W * H, 0);
            bool any = false;
            for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {       // distance to the nearest known pixel
                size_t i = (size_t)y * W + x;
                if (m.xknown[i]) continue;
                any = true;
                int d = 4 * R;
                if (x > 0) d = std::min(d, dist[i - 1] + 2);
                if (y > 0) { d = std::min(d, dist[i - W] + 2); if (x > 0) d = std::min(d, dist[i - W - 1] + 3); if (x < W - 1) d = std::min(d, dist[i - W + 1] + 3); }
                dist[i] = (uint16_t)d;
            }
            if (any) {
                for (int y = H - 1; y >= 0; y--) for (int x = W - 1; x >= 0; x--) {
                    size_t i = (size_t)y * W + x;
                    if (m.xknown[i]) continue;
                    int d = dist[i];
                    if (x < W - 1) d = std::min(d, dist[i + 1] + 2);
                    if (y < H - 1) { d = std::min(d, dist[i + W] + 2); if (x < W - 1) d = std::min(d, dist[i + W + 1] + 3); if (x > 0) d = std::min(d, dist[i + W - 1] + 3); }
                    dist[i] = (uint16_t)d;
                }
                const bool soft = cfg.getb("wide_suave", true);
                soft_buf.assign(px, px + (size_t)vs.w * vs.h);
                const int kx = vs.w / W, ky = vs.h / H;
                for (int y = 0; y < vs.h; y++) {
                    const uint16_t* d = &dist[(size_t)(y / ky) * W];
                    uint32_t* o = &soft_buf[(size_t)y * vs.w];
                    for (int x = 0; x < vs.w; x++) {
                        int dd = d[x / kx];
                        if (!dd) continue;
                        int al = soft ? std::max(0, 255 - dd * 255 / (2 * R)) : 0;
                        o[x] = (o[x] & 0x00FFFFFFu) | ((uint32_t)al << 24);
                    }
                }
                px = soft_buf.data();
                soft_active = true;
            }
        }
        SDL_UpdateTexture(tex, nullptr, px, vs.w * 4);
        frame_ready = false;
        amb_dirty = true;
    }
    std::string mode = fid ? "inteira" : cfg.get("escala", "inteira");
    const std::string sides = fid || con.on ? "preto" : cfg.get("fundo_laterais", "desfocado");   // (the console is the frame)
    const double shrink = sides == "moldura" ? 0.84 : 1.0;   // the console shell needs room around the screen
    SDL_Rect dst, src{0, 0, tex_w, tex_h};
    const bool sharp = mode == "nitida";
    if (sharp) mode = "ajustar";
    if (mode == "esticar") dst = area;
    else if (!fid && vs.extended) {
        // extended view: as wide as the window allows (up to 352 with ultrawide) and, with extra rows,
        // taller too; status bars at the top / bottom keep that side closed; menus bring it back to 160x144
        const int rows = std::max(0, std::min(32, cfg.geti("linhas_extras", 0)));
        const double maxw = cfg.getb("ultrawide", false) ? Machine::XW : Machine::WIDE_W;
        const float tt = m.wide_top_ok ? (float)rows : 0.f, tb = m.wide_bot_ok ? (float)rows : 0.f;
        ext_top += (tt - ext_top) * 0.15f; ext_bot += (tb - ext_bot) * 0.15f;
        if (std::fabs(tt - ext_top) < 0.3f) ext_top = tt;
        if (std::fabs(tb - ext_bot) < 0.3f) ext_bot = tb;
        double t = m.wide_smart ? m.wide_vis : 1.0, e = t * t * (3 - 2 * t);
        double top = ext_top * e, bot = ext_bot * e, hv = 144.0 + top + bot;
        double sc = std::min(area.h / hv, area.w / 160.0) * shrink;
        if (mode == "inteira" && sc >= 1) sc = std::floor(sc);
        double vw = cfg.getb("wide_ajustar", true) ? std::max(160.0, std::min(maxw, std::floor(area.w * shrink / sc))) : maxw;
        vw = 160.0 + (vw - 160.0) * e;
        const double kx = (double)tex_w / Machine::XW, ky = (double)tex_h / Machine::XH;
        src.x = (int)std::lround((Machine::XX0 - (vw - 160.0) / 2) * kx); src.w = (int)std::lround(vw * kx);
        src.y = (int)std::lround((Machine::XY0 - top) * ky); src.h = (int)std::lround(hv * ky);
        dst.w = (int)(vw * sc); dst.h = (int)(hv * sc);
        dst.x = area.x + (area.w - dst.w) / 2; dst.y = area.y + (area.h - dst.h) / 2;
    } else {
        double bw = vs.base_w, bh = vs.base_h;
        // adaptive widescreen: shows as much of the 256-pixel-wide picture as the window's shape allows
        bool adapt = !fid && m.widescreen && vs.base_w == Machine::WIDE_W && vs.base_h == 144 && cfg.getb("wide_ajustar", true);
        double s;
        if (adapt) {
            s = std::min(area.h / bh, area.w / 160.0) * shrink;
            if (mode == "inteira" && s >= 1) s = std::floor(s);
            double vw = std::max(160.0, std::min((double)Machine::WIDE_W, std::floor(area.w * shrink / s)));
            int cut = (int)((Machine::WIDE_W - vw) / 2);
            double k = (double)tex_w / Machine::WIDE_W;
            src.x = (int)(cut * k); src.w = tex_w - 2 * src.x;
            bw = vw;
        } else {
            s = std::min(area.w / bw, area.h / bh) * shrink;
            if (mode == "inteira" && s >= 1) s = std::floor(s);
        }
        // smart widescreen: menus and screens with leftovers at the sides narrow smoothly to the original width
        if (!fid && m.widescreen && m.wide_smart && vs.base_w == Machine::WIDE_W && vs.base_h == 144 && m.wide_vis < 1.0f) {
            double t = m.wide_vis, e = t * t * (3 - 2 * t);          // smoothstep
            double vw = 160.0 + (bw - 160.0) * e;
            double k = (double)tex_w / Machine::WIDE_W;
            int cut = (int)std::lround((Machine::WIDE_W - vw) / 2 * k);
            src.x = cut; src.w = tex_w - 2 * cut;
            bw = vw;
        }
        dst.w = (int)(bw * s); dst.h = (int)(bh * s);
        dst.x = area.x + (area.w - dst.w) / 2; dst.y = area.y + (area.h - dst.h) / 2;
    }
    if (!fid) draw_sides(r, area, dst, src, sides);
    if (soft_active) {
        // something must be behind the see-through parts, even when the picture fills the window
        if (sides == "preto") { SDL_SetRenderDrawColor(r, 0, 0, 0, 255); SDL_RenderFillRect(r, &dst); }
        else if (sides != "moldura" && ambient_update(r, src)) { SDL_SetTextureColorMod(amb_b, 120, 120, 120); SDL_RenderCopy(r, amb_b, nullptr, &dst); }
    }
    bool drawn = false;
    if (sharp && !fid && !vs.linear && dst.w > 0 && src.w > 0) {
        // sharp scaling: pixels are enlarged by a whole number first and only the last step is smooth,
        // so any window size (and 4K screens) gets crisp pixels without uneven columns
        int n = std::max(1, (int)std::ceil((double)dst.w / src.w));
        while (n > 1 && ((long long)src.w * n > 4096 || (long long)src.h * n > 4096)) n--;
        const int sw = src.w * n, sh = src.h * n;
        if (!sharp_tex || sharp_w != sw || sharp_h != sh) {
            if (sharp_tex) SDL_DestroyTexture(sharp_tex);
            sharp_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, sw, sh);
            sharp_w = sw; sharp_h = sh;
            if (sharp_tex) { SDL_SetTextureScaleMode(sharp_tex, SDL_ScaleModeLinear); SDL_SetTextureBlendMode(sharp_tex, SDL_BLENDMODE_BLEND); }
        }
        if (sharp_tex) {
            SDL_Texture* old = SDL_GetRenderTarget(r);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
            SDL_SetRenderTarget(r, sharp_tex); SDL_RenderCopy(r, tex, &src, nullptr);
            SDL_SetRenderTarget(r, old);
            SDL_RenderCopy(r, sharp_tex, nullptr, &dst);
            drawn = true;
        }
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    if (!drawn) SDL_RenderCopy(r, tex, &src, &dst);
    if (!fid && cfg.geti("brilho", 0) > 0 && vs.base_px && !vs.extended) render_bloom(r, dst);
}

void GamePlayer::draw_overlay(const SDL_Rect& area) {
    console_layout(area);
    touch_layout(area);
    console_labels();
    power_labels(power_screen);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    draw_touch(ImGui::GetBackgroundDrawList());
    // achievement toasts (top right, stacked)
    Uint32 now_t = SDL_GetTicks();
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return now_t > t.until; }), toasts.end());
    float ty = (float)area.y + 16;
    for (auto& t : toasts) {
        ImFont* fnt = ImGui::GetFont();
        float fs = ImGui::GetFontSize();
        ImVec2 s1 = fnt->CalcTextSizeA(fs * 1.1f, FLT_MAX, 0, t.title.c_str()), s2 = fnt->CalcTextSizeA(fs, FLT_MAX, 0, t.text.c_str());
        float wbox = std::max(s1.x, s2.x) + 90, hbox = s1.y + s2.y + 26;
        float x0 = area.x + area.w - wbox - 16;
        dl->AddRectFilled(ImVec2(x0, ty), ImVec2(x0 + wbox, ty + hbox), IM_COL32(20, 22, 28, 235), 10);
        dl->AddRect(ImVec2(x0, ty), ImVec2(x0 + wbox, ty + hbox), IM_COL32(230, 190, 60, 255), 10.0f, 0, 2.0f);
        ImVec2 c(x0 + 36, ty + hbox / 2);     // trophy-like medal
        dl->AddCircleFilled(c, 20, IM_COL32(230, 180, 40, 255));
        dl->AddCircleFilled(c, 13, IM_COL32(255, 220, 110, 255));
        dl->AddTriangleFilled(ImVec2(c.x - 9, c.y + 16), ImVec2(c.x - 2, c.y + 30), ImVec2(c.x - 14, c.y + 28), IM_COL32(200, 60, 60, 255));
        dl->AddText(fnt, fs * 1.1f, ImVec2(x0 + 70, ty + 8), IM_COL32(255, 215, 90, 255), t.title.c_str());
        dl->AddText(fnt, fs, ImVec2(x0 + 70, ty + 12 + s1.y), IM_COL32(230, 230, 230, 255), t.text.c_str());
        ty += hbox + 10;
    }
    if (!notice.empty() && SDL_GetTicks() < notice_until) {
        ImVec2 ts = ImGui::CalcTextSize(notice.c_str());
        ImVec2 p((float)area.x + 16, (float)(area.y + area.h) - ts.y - 28);
        dl->AddRectFilled(p, ImVec2(p.x + ts.x + 20, p.y + ts.y + 12), IM_COL32(0, 0, 0, 190), 6);
        dl->AddText(ImVec2(p.x + 10, p.y + 6), IM_COL32(255, 255, 255, 255), notice.c_str());
    }
    if (cfg.getb("mostrar_fps", false)) {
        char b[64]; snprintf(b, sizeof b, "%.1f FPS", fps);
        dl->AddRectFilled(ImVec2((float)area.x + 8, (float)area.y + 8), ImVec2((float)area.x + 100, (float)area.y + 34), IM_COL32(0, 0, 0, 160), 4);
        dl->AddText(ImVec2((float)area.x + 14, (float)area.y + 12), IM_COL32(180, 255, 200, 255), b);
    }
    if (rewinding) {
        // counter and a bar with what is left to go back
        char t[96];
        const Uint32 held = rw_hold_t0 ? SDL_GetTicks() - rw_hold_t0 : 0;
        const int speed = held > 5000 ? 8 : held > 2000 ? 4 : 2;
        if (rw_count == 0) snprintf(t, sizeof t, "<< INÍCIO DO QUE FOI GUARDADO");
        else snprintf(t, sizeof t, "<< VOLTANDO  -%.0f s   %dx", rw_back_seconds, speed);
        ImVec2 ts = ImGui::CalcTextSize(t);
        float w = std::max(ts.x + 24, 240.f), x1 = (float)area.x + area.w - 8, x0 = x1 - w, y0 = (float)area.y + 8;
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + ts.y + 22), IM_COL32(0, 0, 0, 180), 6);
        dl->AddText(ImVec2(x0 + 12, y0 + 5), IM_COL32(120, 200, 255, 255), t);
        const double total = rw_back_seconds + rewind_seconds();
        const float left = total > 0 ? (float)(rewind_seconds() / total) : 0;
        dl->AddRectFilled(ImVec2(x0 + 12, y0 + ts.y + 10), ImVec2(x1 - 12, y0 + ts.y + 14), IM_COL32(255, 255, 255, 50), 2);
        dl->AddRectFilled(ImVec2(x0 + 12, y0 + ts.y + 10), ImVec2(x0 + 12 + (w - 24) * left, y0 + ts.y + 14), IM_COL32(120, 200, 255, 255), 2);
    } else if (fast_shown) {
        const char* t = ">> ACELERADO";
        dl->AddRectFilled(ImVec2((float)area.x + area.w - 230, (float)area.y + 8), ImVec2((float)area.x + area.w - 8, (float)area.y + 34), IM_COL32(0, 0, 0, 170), 4);
        dl->AddText(ImVec2((float)area.x + area.w - 222, (float)area.y + 12), IM_COL32(255, 220, 120, 255), t);
    }
    if (gif.on() || video.on()) {
        char b[96];
        if (video.on()) snprintf(b, sizeof b, "GRAVANDO VÍDEO %d:%02d (F7 para parar)", (int)video.seconds() / 60, (int)video.seconds() % 60);
        else snprintf(b, sizeof b, "GRAVANDO GIF %.0f s (F6 para parar)", gif.seconds());
        bool blink = (SDL_GetTicks() / 500) & 1;
        dl->AddRectFilled(ImVec2((float)area.x + 8, (float)area.y + area.h - 40), ImVec2((float)area.x + 330, (float)area.y + area.h - 10), IM_COL32(0, 0, 0, 170), 6);
        if (blink) dl->AddCircleFilled(ImVec2((float)area.x + 24, (float)area.y + area.h - 25), 7, IM_COL32(230, 40, 40, 255));
        dl->AddText(ImVec2((float)area.x + 38, (float)area.y + area.h - 34), IM_COL32(255, 255, 255, 255), b);
    }
    if (wav.on()) {
        char b[64]; snprintf(b, sizeof b, "GRAVANDO ÁUDIO %.0f s", wav.seconds());
        dl->AddCircleFilled(ImVec2((float)area.x + 18, (float)area.y + area.h - 18), 6, IM_COL32(230, 40, 40, 255));
        dl->AddText(ImVec2((float)area.x + 30, (float)area.y + area.h - 26), IM_COL32(255, 255, 255, 255), b);
    }
    if (paused && !menu_open) {
        const char* t = "PAUSADO (P para continuar)";
        ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(area.x + (area.w - ts.x) / 2, (float)area.y + 20), IM_COL32(255, 255, 255, 255), t);
    }
}

static bool combo_setting(Settings& cfg, const char* label, const char* key, const char* const* ids, const char* const* names, int n, const char* def) {
    std::string cur = cfg.get(key, def);
    int idx = 0;
    for (int i = 0; i < n; i++) if (cur == ids[i]) idx = i;
    bool ch = ImGui::Combo(label, &idx, names, n);
    if (ch) { cfg.set(key, ids[idx]); cfg.save(); }
    return ch;
}

// phones: dragging a finger scrolls the menu (ImGui only scrolls with the wheel or the scrollbar).
// A drag that starts on a button cancels the button; a slider moved sideways keeps working.
static void touch_scroll(bool horizontal = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseSource != ImGuiMouseSource_TouchScreen || !ImGui::IsMouseDown(0)) return;
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) return;
    const ImVec2 d = ImGui::GetMouseDragDelta(0, 0.f);
    const float along = horizontal ? std::fabs(d.x) : std::fabs(d.y), across = horizontal ? std::fabs(d.y) : std::fabs(d.x);
    if (along < 12.f || along < across) return;
    ImGuiContext& g = *GImGui;
    if (g.ActiveId) ImGui::ClearActiveID();
    if (horizontal) ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
    else ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
}

// a button that goes on the same line as the previous one only if it fits (menus in small windows)
static bool flow_button(const char* label) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + st.FramePadding.x * 2;
    const float next = ImGui::GetItemRectMax().x + st.ItemSpacing.x;
    if (next + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
    return ImGui::Button(label);
}

void GamePlayer::draw_menu(const SDL_Rect& area) {
    if (splash_on || power_state == 1 || power_state == 2) return;
    if (ask_continue) { draw_continue_prompt(area); return; }
    if (start_screen && !menu_open) { draw_start_screen(area); return; }
    if (!menu_open) { draw_topbar(area); g_menu_was_open = false; return; }
    float uis = std::max(100, std::min(NB_UI_MAX, cfg.geti("escala_interface", 100))) / 100.f;
    // pause screen: the whole window, the game dimmed behind it, sections listed on the left
    static const char* sections[10] = {"Jogo", "Vídeo", "Áudio", "Controles", "Trapaças", "Acessibilidade", "Mods", "Sobre", "Conquistas", "Capturas"};
    const int* order = MENU_ORDER;
    const bool opening = !g_menu_was_open;
    g_menu_was_open = true;
    if (want_tab >= 0 && want_tab < 10) menu_tab = want_tab;
    ImGui::SetNextWindowPos(ImVec2((float)area.x, (float)area.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)area.w, (float)area.h), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.04f, 0.13f, 0.86f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    // menu size: the font size itself, so the inner panels (sections, buttons) grow too
    ImGui::PushFont(nullptr, ui::base_size * uis);
    ImGui::Begin("##pausa", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();
    {   // colour stripe along the top, like the Studio
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float sw = area.w / 5.f;
        for (int i = 0; i < 5; i++) dl->AddRectFilled(ImVec2(area.x + i * sw, (float)area.y), ImVec2(area.x + (i + 1) * sw, area.y + 4.f), ui::accent(i));
    }
    const float pw = std::min((float)area.w - 24, 1060.f * uis);
    ImGui::SetCursorPos(ImVec2((area.w - pw) / 2, 14));
    ImGui::BeginGroup();
    const bool narrow = area.w < area.h || pw < 620.f * uis;     // a phone standing up, or a small window
    {
        const char* back = narrow ? "Voltar" : "Voltar ao jogo";
        const float bwid = ImGui::CalcTextSize(back).x + 34;
        ImGui::PushFont(nullptr, (narrow ? 22.f : 30.f) * uis);
        std::string nm = game_name;
        while (narrow && nm.size() > 4 && ImGui::CalcTextSize(nm.c_str()).x > pw - bwid - 20) nm = nm.substr(0, nm.size() - 4) + "...";
        ImGui::TextUnformatted(nm.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 10, (area.w + pw) / 2 - bwid));
        if (ImGui::Button(back)) menu_open = false;
    }
    if (emulation) ui::badge(narrow ? "EMULAÇÃO" : "MODO EMULAÇÃO (interpretador SM83)", IM_COL32(170, 110, 20, 255));
    else ui::badge(narrow ? "NATIVO" : "EXECUTÁVEL NATIVO RECOMPILADO", IM_COL32(30, 130, 90, 255));
    ImGui::SameLine();
    bool fid = fidelity();
    if (ImGui::Checkbox("Fidelidade ao original", &fid)) {
        cfg.set("fidelity", fid ? "1" : "0"); cfg.save(); reset(); want_resize = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Joga exatamente como no aparelho original: desliga widescreen, filtros, trapaças e todas as melhorias.");
    ImGui::Spacing();
    // the list of sections: on the left, or (narrow screens: a phone standing up) a row on top
    float navw = std::min(220.f * uis, pw * 0.3f), ch = area.h - ImGui::GetCursorPosY() - 16;
    if (narrow) {
        const float rowh = ImGui::GetTextLineHeight() + 18 + ImGui::GetStyle().ScrollbarSize;
        ImGui::BeginChild("secoes", ImVec2(pw, rowh), 0, ImGuiWindowFlags_HorizontalScrollbar);
        touch_scroll(true);
        for (int k = 0; k < 10; k++) {
            const int i = order[k];
            const bool sel = menu_tab == i;
            if (k) ImGui::SameLine(0, 6);
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImVec4(0.18f, 0.16f, 0.32f, 1));
            if (sel && (opening || want_tab >= 0)) { ImGui::SetKeyboardFocusHere(); ImGui::SetScrollHereX(0.5f); }
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float bw = ImGui::CalcTextSize(sections[i]).x + 34;
            if (ImGui::Button("##s", ImVec2(bw, ImGui::GetTextLineHeight() + 14))) menu_tab = i;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p0.x + 12, p0.y + (ImGui::GetTextLineHeight() + 14) / 2), 5.f * uis, ui::accent(k));
            ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 24, p0.y + 7), ImGui::GetColorU32(ImGuiCol_Text), sections[i]);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndChild();
        ch -= rowh + ImGui::GetStyle().ItemSpacing.y;
        navw = 0;
    } else {
    ImGui::BeginChild("secoes", ImVec2(navw, ch));
    for (int k = 0; k < 10; k++) {
        const int i = order[k];
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool sel = menu_tab == i;
        ImGui::PushID(i);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (sel && (opening || want_tab >= 0)) ImGui::SetKeyboardFocusHere();   // the keyboard / controller focus follows the current section
        if (ImGui::Selectable("##s", sel, 0, ImVec2(0, ImGui::GetTextLineHeight() + 14))) menu_tab = i;
        if (sel) ImGui::PopStyleColor();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p0.x + 12, p0.y + (ImGui::GetTextLineHeight() + 14) / 2), 5.f * uis, ui::accent(k));
        ImGui::GetWindowDrawList()->AddText(ImVec2(p0.x + 26, p0.y + 7), ImGui::GetColorU32(ImGuiCol_Text), sections[i]);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
#ifdef NB_MOBILE
    ImGui::TextWrapped("Voltar do celular: fecha o menu");
#else
    ImGui::TextWrapped("Esc / B: voltar\nLB / RB ou PgUp / PgDn: seção");
#endif
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::SameLine(0, 14);
    }
    ImGui::BeginChild("conteudo", ImVec2(narrow ? pw : pw - navw - 14, ch), ImGuiChildFlags_Borders);
    touch_scroll();
    ImGui::PushFont(nullptr, 22.f * uis);
    ImGui::TextUnformatted(sections[menu_tab]);
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);       // leaves room for the labels on the right
    auto tab = [&](const char*, int idx) { return menu_tab == idx; };
    {
        if (tab("Jogo", 0)) {
            const int nb = in_studio ? 2 : 3;
            const float bwd = std::min(200.f * uis, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (nb - 1)) / nb);
            if (ImGui::Button("Continuar", ImVec2(bwd, 0))) menu_open = false;
            ImGui::SameLine();
            if (ImGui::Button("Reiniciar", ImVec2(bwd, 0))) { reset(); menu_open = false; }
            if (!in_studio) { ImGui::SameLine(); if (ImGui::Button("Tela inicial", ImVec2(bwd, 0))) { save_state(0); start_screen = true; menu_open = false; } }
            draw_timeline_section();
            draw_states_section();
            ImGui::SeparatorText("Outros");
            if (ImGui::Button("Capturar tela (F12)")) screenshot();
            if (flow_button(gif.on() ? "Parar GIF (F6)" : "Gravar GIF (F6)")) { toggle_gif(); menu_open = false; }
            if (flow_button(video.on() ? "Parar vídeo (F7)" : "Gravar vídeo com som (F7)")) { toggle_video(); menu_open = false; }
            if (flow_button("Abrir pasta de capturas")) { plat::make_dirs(captures_dir()); plat::open_in_explorer(captures_dir()); }
            if (flow_button("Abrir pasta de saves")) plat::open_in_explorer(saves_dir());
            if (m.cart.info.has_battery) ImGui::TextDisabled("%s", natsep("O progresso do cartucho é salvo automaticamente em saves/" + game_id + ".sav").c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("Atalhos: Esc/F1 menu, F5 salvar estado, F8 carregar, F6 gravar GIF, F7 gravar vídeo, F11 tela cheia, F12 captura, P pausa, Tab acelerar, Backspace voltar no tempo");
            ImGui::PopStyleColor();
            ImGui::SeparatorText("Cabo link (2 jogadores pela rede ou pela internet)");
            draw_link_section(uis);
            if (m.cart.info.has_rumble) {
                bool vb = cfg.getb("vibracao", true);
                if (ImGui::Checkbox("Vibração no controle (cartucho com motor de vibração)", &vb)) { cfg.set("vibracao", vb ? "1" : "0"); cfg.save(); }
            }
            if (m.cart.info.mbc == 7) ImGui::TextDisabled("Cartucho com sensor de inclinação: incline com o analógico direito ou as teclas I, J, K, L.");
            ImGui::Spacing();
            if (ImGui::Button("Sair do jogo", ImVec2(200, 0))) want_quit = true;
            
        }
        if (tab("Vídeo", 1)) {
            ImGui::SeparatorText("Visual do aparelho");
            {
                static const char* vc_ids[] = {"desligado", "gb", "gbc", "auto"};
                static const char* vc_names[] = {"Desligado (janela normal)", "Clássico (estilo Game Boy)", "Color (estilo Game Boy Color)", "Automático: em pé vira o aparelho"};
                std::string before = cfg.get("visual_console", "desligado");
#ifdef NB_MOBILE
                const int vc_n = 4;
#else
                const int vc_n = cfg.get("visual_console", "") == "auto" ? 4 : 3;
#endif
                combo_setting(cfg, "Janela com visual de console", "visual_console", vc_ids, vc_names, vc_n, "desligado");
                if (cfg.get("visual_console", "") == "auto") {
                    static const char* st_ids[] = {"gbc", "gb"};
                    static const char* st_names[] = {"Color (estilo Game Boy Color)", "Clássico (estilo Game Boy)"};
                    combo_setting(cfg, "Aparelho quando em pé", "estilo_console_auto", st_ids, st_names, 2, "gbc");
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A janela vira o aparelho: o jogo na tela dele, botões que afundam quando você aperta\n(e que dá para clicar ou tocar), luz de ligado e janela sem bordas que você arrasta pelo corpo.");
                if (cfg.get("visual_console", "desligado") != before) { apply_settings(); want_resize = true; frame_ready = true; }
                if (console_on() && cfg.getb("widescreen", false)) ImGui::TextDisabled("O widescreen fica desligado enquanto a janela tem o visual de console.");
                if (console_on()) {
                    static const char* cc_ids[] = {"cinza", "roxo_translucido", "roxo", "amarelo", "azul_petroleo", "vermelho", "verde", "preto"};
                    static const char* cc_names[] = {"Cinza clássico", "Roxo translúcido", "Roxo", "Amarelo", "Azul-petróleo", "Vermelho", "Verde", "Preto"};
                    combo_setting(cfg, "Cor do aparelho", "console_cor", cc_ids, cc_names, 8, console_gbc() ? "roxo_translucido" : "cinza");
                    bool refl = cfg.getb("console_reflexo", true);
                    if (ImGui::Checkbox("Reflexo no vidro da tela", &refl)) { cfg.set("console_reflexo", refl ? "1" : "0"); cfg.save(); }
                }
                bool anim = cfg.getb("animacao_ligar", true);
                if (ImGui::Checkbox("Ligar e desligar como o aparelho", &anim)) { cfg.set("animacao_ligar", anim ? "1" : "0"); cfg.save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ao abrir: a tela acende, o nome NativeBoy desce e toca um som curto. Ao fechar: a tela apaga.");
                if (anim) { ImGui::SameLine(); bool snd = cfg.getb("animacao_som", true); if (ImGui::Checkbox("com som", &snd)) { cfg.set("animacao_som", snd ? "1" : "0"); cfg.save(); } }
            }
            ImGui::SeparatorText("Imagem");
            if (fid) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Modo fidelidade ativo: imagem original em escala inteira.");
            ImGui::BeginDisabled(fid);
            bool fs = cfg.getb("fullscreen", false);
            if (ImGui::Checkbox("Tela cheia (F11)", &fs)) want_fullscreen_toggle = true;
            if (!in_studio) {
                static const char* ft_ids[] = {"sem_bordas", "exclusiva"};
                static const char* ft_names[] = {"Sem bordas (troca de janela rápida)", "Exclusiva (escolher a resolução)"};
                ImGui::SetNextItemWidth(std::min(320.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (combo_setting(cfg, "Tipo de tela cheia", "tipo_tela_cheia", ft_ids, ft_names, 2, "sem_bordas")) want_display_apply = true;
                if (cfg.get("tipo_tela_cheia", "sem_bordas") == "exclusiva") {
                    std::vector<std::string> modes;
                    int n = SDL_GetNumDisplayModes(0);
                    for (int i = 0; i < n; i++) {
                        SDL_DisplayMode dm;
                        if (SDL_GetDisplayMode(0, i, &dm) == 0) {
                            std::string md = std::to_string(dm.w) + "x" + std::to_string(dm.h) + " @ " + std::to_string(dm.refresh_rate) + " Hz";
                            if (std::find(modes.begin(), modes.end(), md) == modes.end()) modes.push_back(md);
                        }
                    }
                    std::string cur = cfg.get("resolucao", modes.empty() ? "" : modes[0]);
                    ImGui::SetNextItemWidth(std::min(320.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                    if (ImGui::BeginCombo("Resolução", cur.c_str())) {
                        for (auto& md : modes) if (ImGui::Selectable(md.c_str(), md == cur)) { cfg.set("resolucao", md); cfg.save(); want_display_apply = true; }
                        ImGui::EndCombo();
                    }
                }
            }
            bool vsync = cfg.getb("vsync", true);
            if (ImGui::Checkbox("V-Sync (sincronizar com o monitor)", &vsync)) { cfg.set("vsync", vsync ? "1" : "0"); cfg.save(); notify("V-Sync aplicado"); }
            static const char* esc_ids[] = {"inteira", "nitida", "ajustar", "esticar"};
            static const char* esc_names[] = {"Inteira (pixels sem distorção)", "Nítida (preenche a tela, pixels definidos)", "Ajustar à tela (mantém proporção)", "Esticar (preenche tudo)"};
            combo_setting(cfg, "Escala", "escala", esc_ids, esc_names, 4, "inteira");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("\"Nítida\" é a melhor para telas grandes e 4K: usa a tela inteira e mantém os pixels definidos.");
            combo_setting(cfg, "Filtro", "filtro", FILTER_IDS, FILTER_NAMES, FILTER_COUNT, "nitido");
            static const char* win_ids[] = {"2", "3", "4", "5", "6", "8"};
            static const char* win_names[] = {"320x288 (2x)", "480x432 (3x)", "640x576 (4x)", "800x720 (5x)", "960x864 (6x)", "1280x1152 (8x)"};
            if (combo_setting(cfg, "Tamanho da janela", "janela", win_ids, win_names, 6, "4")) want_fullscreen_toggle = false;
            if (!in_studio) {
                ImGui::SeparatorText("Janela");
                bool ps = cfg.getb("pausar_sem_foco", true);
                if (ImGui::Checkbox("Pausar quando a janela fica em segundo plano", &ps)) { cfg.set("pausar_sem_foco", ps ? "1" : "0"); cfg.save(); }
                bool hm = cfg.getb("esconder_mouse", true);
                if (ImGui::Checkbox("Esconder o mouse enquanto joga", &hm)) { cfg.set("esconder_mouse", hm ? "1" : "0"); cfg.save(); }
                bool top = cfg.getb("sempre_no_topo", false);
                if (ImGui::Checkbox("Janela sempre por cima das outras", &top)) { cfg.set("sempre_no_topo", top ? "1" : "0"); cfg.save(); }
                bool bar = cfg.getb("barra_superior", true);
                if (ImGui::Checkbox("Barra de atalhos ao passar o mouse no topo", &bar)) { cfg.set("barra_superior", bar ? "1" : "0"); cfg.save(); }
                ImGui::TextDisabled("Dois cliques na imagem: tela cheia. A janela lembra o tamanho e a posição.");
            }
            ImGui::SeparatorText("Laterais da tela (janela maior que o jogo)");
            static const char* side_ids[] = {"desfocado", "bordas", "moldura", "imagem", "preto"};
            static const char* side_names[] = {"Luz ambiente (imagem do jogo desfocada)", "Estender as bordas do jogo", "Moldura de console",
                                               "Imagem de fundo (capa)", "Preto"};
            combo_setting(cfg, "Laterais", "fundo_laterais", side_ids, side_names, 5, "desfocado");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("O que aparece nas sobras da janela quando ela é maior ou mais larga que a imagem do jogo.");
            std::string sm = cfg.get("fundo_laterais", "desfocado");
            if (sm == "moldura") {
                static const char* th_ids[] = {"cinza", "translucido", "indigo", "roxo", "preto", "verde", "vermelho", "azul"};
                static const char* th_names[] = {"Cinza clássico", "Roxo translúcido", "Índigo", "Roxo", "Preto", "Verde", "Vermelho", "Azul"};
                combo_setting(cfg, "Cor da moldura", "moldura_cor", th_ids, th_names, 8, "cinza");
                static const char* fm_ids[] = {"portatil", "simples"};
                static const char* fm_names[] = {"Portátil (direcional e botões nas laterais)", "Só a moldura da tela"};
                combo_setting(cfg, "Formato", "moldura_formato", fm_ids, fm_names, 2, "portatil");
            }
            if (sm == "imagem") {
                std::string cur = background_image_path();
                ImGui::TextDisabled("%s", cur.empty() ? "Nenhuma imagem escolhida (usa a luz ambiente)." : natsep(cur).c_str());
                if (ImGui::Button("Escolher imagem (PNG)...")) {
                    std::string f = plat::open_file_dialog("Imagem de fundo das laterais", "Imagem PNG", "*.png");
                    std::vector<uint32_t> px; int w, h;
                    if (!f.empty() && load_png(f, px, w, h)) { plat::make_dirs(config_dir()); plat::copy_file(f, plat::join(config_dir(), "fundo.png")); bg_path.clear(); notify("Imagem de fundo aplicada"); }
                    else if (!f.empty()) notify("Não foi possível ler a imagem (use PNG).");
                }
                if (plat::file_exists(plat::join(config_dir(), "fundo.png"))) {
                    ImGui::SameLine();
                    if (ImGui::Button("Remover imagem")) { plat::remove_file(plat::join(config_dir(), "fundo.png")); bg_path.clear(); }
                }
            }
            if (m.cart.info.sgb_flag == 0x03 && !m.cart.info.cgb_supported) {
                ImGui::SeparatorText("Super Game Boy");
                bool sg = cfg.getb("sgb", true);
                if (ImGui::Checkbox("Recursos de Super Game Boy (cores e moldura feitas pelo jogo)", &sg)) { cfg.set("sgb", sg ? "1" : "0"); cfg.save(); reset(); want_resize = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Este jogo foi feito com suporte ao Super Game Boy: ele mesmo define paletas para cada tela e uma moldura.\nMudar esta opção reinicia o jogo.");
                bool sb = cfg.getb("sgb_borda", true);
                if (ImGui::Checkbox("Mostrar a moldura do Super Game Boy", &sb)) { cfg.set("sgb_borda", sb ? "1" : "0"); cfg.save(); want_resize = true; }
            }
            ImGui::SeparatorText("Efeitos modernos");
            {
                bool sh = cfg.getb("sombra_sprites", false);
                if (ImGui::Checkbox("Sombra sob os personagens", &sh)) { cfg.set("sombra_sprites", sh ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Personagens e objetos (sprites) projetam uma sombra suave no cenário, dando profundidade.");
                ImGui::SameLine(0, 30);
                bool vg = cfg.getb("vinheta", false);
                if (ImGui::Checkbox("Vinheta", &vg)) { cfg.set("vinheta", vg ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Escurece de leve os cantos da imagem.");
                int cv = cfg.geti("cores_vivas", 0);
                ImGui::SetNextItemWidth(std::min(240.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::SliderInt("Cores vivas", &cv, 0, 100, cv ? "%d%%" : "desligado")) { cfg.seti("cores_vivas", cv); apply_settings(); }
                if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
                int br = cfg.geti("brilho", 0);
                ImGui::SetNextItemWidth(std::min(240.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::SliderInt("Brilho das luzes (bloom)", &br, 0, 100, br ? "%d%%" : "desligado")) cfg.seti("brilho", br);
                if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("As partes mais claras da imagem ganham um brilho suave em volta, como em jogos modernos.");
                ImGui::TextDisabled("Para bordas arredondadas em alta definição, escolha o filtro xBR 4x acima.");
            }
            ImGui::SeparatorText("Melhorias");
            bool ws = cfg.getb("widescreen", false);
            if (ImGui::Checkbox("Tela widescreen 16:9 (experimental)", &ws)) { cfg.set("widescreen", ws ? "1" : "0"); cfg.save(); apply_settings(); want_resize = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mostra 48 pixels a mais de cada lado, lidos do mapa de fundo do jogo.\nEm jogos com rolagem, as bordas podem mostrar trechos ainda não carregados.");
            if (ws) {
                bool adj = cfg.getb("wide_ajustar", true);
                if (ImGui::Checkbox("Ajustar a largura à janela", &adj)) { cfg.set("wide_ajustar", adj ? "1" : "0"); cfg.save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A imagem fica mais larga ou mais estreita conforme o formato da janela (de 160 até 256 pixels),\npreenchendo a tela sem faixas laterais. Desligado: sempre 16:9.");
                static const char* edges[] = {"Mostrar tudo", "Esconder trechos antigos (recomendado)", "Esconder trechos antigos e esmaecer as bordas"};
                int ev = std::max(0, std::min(2, cfg.geti("bordas_wide", 1)));
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::Combo("Bordas do widescreen", &ev, edges, 3)) { cfg.seti("bordas_wide", ev); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Partes do mapa que o jogo não atualizou junto com a tela visível costumam conter restos de outras telas.\n\"Esconder\" pinta essas colunas com a cor de fundo.");
                bool uw = cfg.getb("ultrawide", false);
                if (ImGui::Checkbox("Ultrawide (até 352 pixels: 21:9 e além)", &uw)) { cfg.set("ultrawide", uw ? "1" : "0"); cfg.save(); apply_settings(); want_resize = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Passa dos 256 pixels que o mapa do jogo guarda. O que fica além disso vem da memória do cenário\n(lugares por onde você já passou); o que ainda não é conhecido se desfaz no fundo escolhido.\nNeste modo o pacote HD e a sombra dos personagens ficam desligados.");
                static const char* row_names[] = {"Desligada", "16 linhas acima e abaixo", "32 linhas acima e abaixo"};
                int rv = cfg.geti("linhas_extras", 0) >= 32 ? 2 : cfg.geti("linhas_extras", 0) >= 16 ? 1 : 0;
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::Combo("Expansão vertical", &rv, row_names, 3)) { cfg.seti("linhas_extras", rv * 16); cfg.save(); apply_settings(); want_resize = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mostra mais cenário acima e abaixo da tela original (campo de visão maior).\nO lado que tem barra de status fica fechado, e em menus a imagem volta ao tamanho original.");
                {
                    bool v = cfg.getb("cenario_frente", true);
                    if (ImGui::Checkbox("Cenário à frente (o jogo olha adiante por você)", &v)) { cfg.set("cenario_frente", v ? "1" : "0"); cfg.save(); scout.reset(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Uma cópia invisível do jogo roda um pouco adiante, andando na direção em que a tela está rolando,\ne o cenário que ela vê já aparece nas laterais antes de você chegar. Nada muda no seu jogo.\nUsa mais processador; desligue em computadores fracos.");
                    v = cfg.getb("sprites_extras", true);
                    if (ImGui::Checkbox("Personagens e objetos fora da tela original", &v)) { cfg.set("sprites_extras", v ? "1" : "0"); cfg.save(); apply_settings(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inimigos e itens que saíram da tela original andando (ou porque a tela rolou) continuam sendo\ndesenhados nas áreas extras. Sprites que o jogo esconde com um salto de posição ficam escondidos.");
                    v = cfg.getb("cenario_animado", true);
                    if (ImGui::Checkbox("Cenário lembrado continua animado (água, tochas...)", &v)) { cfg.set("cenario_animado", v ? "1" : "0"); cfg.save(); apply_settings(); }
                    v = cfg.getb("cenario_salvo", true);
                    if (ImGui::Checkbox("Guardar o cenário visto junto do save", &v)) { cfg.set("cenario_salvo", v ? "1" : "0"); cfg.save(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Os lugares por onde você passou ficam gravados na pasta de saves. Ao abrir o jogo de novo,\nas laterais já aparecem preenchidas nesses lugares.");
                }
                bool mem = cfg.getb("memoria_cenario", true);
                if (ImGui::Checkbox("Memória do cenário", &mem)) { cfg.set("memoria_cenario", mem ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("O que já passou pela tela é lembrado e aparece de novo nas laterais quando o jogo\nnão tem mais aquela parte do mapa (comum ao rolar a tela). As cores acompanham fades e paletas.");
                bool hudw = cfg.getb("hud_largo", true);
                if (ImGui::Checkbox("Estender barras de status (HUD) na largura toda", &hudw)) { cfg.set("hud_largo", hudw ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Barras de vida/pontos (na janela ou com rolagem própria) continuam até as bordas,\nem vez de mostrar pedaços do cenário ao lado.");
                bool soft = cfg.getb("wide_suave", true);
                if (ImGui::Checkbox("Esconder suavemente o que o jogo ainda não desenhou", &soft)) { cfg.set("wide_suave", soft ? "1" : "0"); cfg.save(); frame_ready = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ao andar, o lado para onde você vai ainda não foi desenhado pelo jogo. Em vez de uma faixa lisa,\nessa parte se desfaz suavemente no fundo escolhido (desfocado, imagem...).");
                bool places = cfg.getb("wide_lugares", true);
                if (ImGui::Checkbox("Lembrar lugares já visitados", &places)) { cfg.set("wide_lugares", places ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ao voltar para um lugar (sair de uma casa, fechar um menu), as laterais mostram de novo\no cenário que você já tinha visto ali.");
                bool smart = cfg.getb("wide_inteligente", true);
                if (ImGui::Checkbox("Widescreen inteligente", &smart)) { cfg.set("wide_inteligente", smart ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Em menus, telas de pausa e cenas em que as laterais só teriam restos de outras telas,\na imagem volta suavemente para a largura original e se abre de novo quando o jogo continua.");
                bool park = cfg.getb("sprites_estacionados", false);
                if (ImGui::Checkbox("Mostrar sprites guardados fora da tela", &park)) { cfg.set("sprites_estacionados", park ? "1" : "0"); cfg.save(); apply_settings(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Muitos jogos escondem sprites colocando-os na coluna 0 ou depois da 168. Normalmente\neles ficam escondidos; ligue só se algum objeto de verdade sumir nas bordas.");
            }
            bool nsl = cfg.getb("sem_limite_sprites", false);
            if (ImGui::Checkbox("Sem limite de sprites (menos piscadas)", &nsl)) { cfg.set("sem_limite_sprites", nsl ? "1" : "0"); cfg.save(); apply_settings(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("O Game Boy só desenha 10 sprites por linha; acima disso eles somem ou piscam.\nEsta opção desenha todos.");
            bool gh = cfg.getb("rastro_lcd", false);
            if (ImGui::Checkbox("Rastro do LCD original (ghosting)", &gh)) { cfg.set("rastro_lcd", gh ? "1" : "0"); cfg.save(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Imita a tela lenta do Game Boy: objetos que piscam ficam semitransparentes, como no aparelho.");
            if (!m.cgb) {
                std::vector<const char*> ids, names;
                for (int i = 0; i < DMG_PALETTE_COUNT; i++) { ids.push_back(DMG_PALETTES[i].id); names.push_back(DMG_PALETTES[i].name); }
                bool has_custom = !cfg.get("cores_personalizadas", "").empty();
                if (has_custom) { ids.push_back("personalizada"); names.push_back("Personalizada (do criador do jogo)"); }
                if (combo_setting(cfg, "Paleta de cores", "paleta", ids.data(), names.data(), (int)ids.size(), "classico")) apply_settings();
            } else {
                bool cc = cfg.getb("correcao_cor", true);
                if (ImGui::Checkbox("Correção de cor do LCD do GBC", &cc)) { cfg.set("correcao_cor", cc ? "1" : "0"); cfg.save(); apply_settings(); }
            }
            ImGui::SeparatorText("Gráficos em alta definição");
            std::vector<std::string> opts = {"(nenhum)"};
            for (auto& p : hd_packs) opts.push_back(p);
            std::string cur = cfg.get("pacote_hd", "");
            if (ImGui::BeginCombo("Pacote HD", cur.empty() ? "(nenhum)" : cur.c_str())) {
                for (size_t i = 0; i < opts.size(); i++) {
                    bool sel = (i == 0 && cur.empty()) || opts[i] == cur;
                    if (ImGui::Selectable(opts[i].c_str(), sel)) { cfg.set("pacote_hd", i == 0 ? "" : opts[i]); cfg.save(); apply_settings(); }
                }
                ImGui::EndCombo();
            }
            if (!hd.empty()) ImGui::Text("Pacote ativo: %s (%zu tiles, escala %dx). Tiles substituídos neste quadro: %d", hd.name.c_str(), hd.images.size(), hd.scale, vs.hd_hits / (hd.scale > 0 ? 64 : 1));
            if (!hd_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", hd_error.c_str());
            ImGui::TextDisabled("%s", natsep("Coloque pacotes em assets/hd/<nome>/ (crie-os no NativeBoy Studio > Modificar jogo).").c_str());
            bool sf = cfg.getb("mostrar_fps", false);
            if (ImGui::Checkbox("Mostrar FPS", &sf)) { cfg.set("mostrar_fps", sf ? "1" : "0"); cfg.save(); }
            ImGui::EndDisabled();
            
        }
        if (tab("Áudio", 2)) {
            int vol = cfg.geti("volume", 80);
            if (ImGui::SliderInt("Volume", &vol, 0, 100, "%d%%")) { cfg.seti("volume", vol); m.apu->set_volume(vol / 100.0f); }
            if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
            ImGui::BeginDisabled(fid);
            static const char* ch[4] = {"Canal 1 (pulso com sweep)", "Canal 2 (pulso)", "Canal 3 (onda)", "Canal 4 (ruído)"};
            for (int i = 0; i < 4; i++) {
                bool on = !cfg.getb("mudo" + std::to_string(i + 1), false);
                if (ImGui::Checkbox(ch[i], &on)) { cfg.set("mudo" + std::to_string(i + 1), on ? "0" : "1"); cfg.save(); apply_settings(); }
            }
            ImGui::EndDisabled();
            ImGui::Text("Saída: %d Hz, estéreo%s", m.apu->sample_rate, audio ? "" : " (nenhum dispositivo de áudio encontrado)");
            ImGui::SeparatorText("Trilha remasterizada");
            if (music.empty()) ImGui::TextDisabled("Este jogo não tem trilha remasterizada (o criador pode adicionar no NativeBoy Studio).");
            else {
                bool tn = cfg.getb("trilha_nova", true);
                if (ImGui::Checkbox("Tocar a trilha remasterizada no lugar das músicas originais", &tn)) { cfg.set("trilha_nova", tn ? "1" : "0"); cfg.save(); apply_settings(); }
                ImGui::TextDisabled("%zu música(s) substituída(s). Tocando agora: %s", music.files.size(),
                                    music_active ? music.files[music_last].c_str() : "música original");
                if (!music_out.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", music_out.error.c_str());
            }
            ImGui::SeparatorText("Gravar o áudio");
            if (!wav.on()) {
                if (ImGui::Button("Gravar áudio em WAV")) {
                    std::string dir = captures_dir(); plat::make_dirs(dir);
                    if (wav.start(plat::join(dir, game_id + "_" + plat::now_stamp() + ".wav"), m.apu->sample_rate)) { notify("Gravando o áudio..."); menu_open = false; }
                    else notify("Não foi possível criar o arquivo WAV");
                }
                ImGui::SameLine(); ImGui::TextDisabled("Grave cada música do jogo para ouvir ou remasterizar.");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1));
                if (ImGui::Button("Parar gravação")) { wav.stop(); notify(natsep("Áudio salvo em capturas/") + plat::file_name(wav.path)); }
                ImGui::PopStyleColor();
                ImGui::SameLine(); ImGui::Text("%.1f s gravados", wav.seconds());
            }
            
        }
        if (tab("Controles", 3)) {
            {   // touch screens (phones, tablets, PCs with a touch screen)
                ImGui::SeparatorText("Controles de toque");
                static const char* tq_ids[] = {"auto", "sempre", "nunca"};
                static const char* tq_names[] = {"Automático (aparecem ao tocar na tela)", "Sempre visíveis", "Nunca"};
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                combo_setting(cfg, "Botões na tela", "controles_toque", tq_ids, tq_names, 3, "auto");
                int ts = cfg.geti("toque_tamanho", 100), to = cfg.geti("toque_opacidade", 55);
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::SliderInt("Tamanho dos botões", &ts, 60, 150, "%d%%")) cfg.seti("toque_tamanho", ts);
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::SliderInt("Transparência", &to, 15, 100, "%d%%")) cfg.seti("toque_opacidade", to);
                bool vib = cfg.getb("vibrar_toque", true);
                if (ImGui::Checkbox("Vibrar ao apertar", &vib)) { cfg.set("vibrar_toque", vib ? "1" : "0"); cfg.save(); }
                ImGui::TextDisabled("Dá para apertar vários botões ao mesmo tempo e deslizar o dedo pelo direcional.\n\"<<\" segurado volta no tempo; \">>\" acelera; \"≡\" abre este menu.");
            }
            {
                static const char* ra_names[] = {"Desligado (como o aparelho)", "1 quadro (recomendado)", "2 quadros (mínimo atraso)"};
                int ra = std::max(0, std::min(2, cfg.geti("run_ahead", 1)));
                ImGui::SetNextItemWidth(std::min(330.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
                if (ImGui::Combo("Menos atraso nos controles", &ra, ra_names, 3)) { cfg.seti("run_ahead", ra); cfg.save(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Run-ahead: o jogo calcula o próximo quadro antes de mostrar a imagem, então o que você aperta\naparece 1 ou 2 quadros mais cedo do que no aparelho original. O jogo em si não muda.\nUsa mais processador; desligue se o jogo ficar lento.");
                ImGui::Separator();
            }
            ImGui::Text("Clique em um botão e pressione a nova tecla / botão do controle (Esc cancela).");
            if (ImGui::BeginTable("map", 3, ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Botão do Game Boy"); ImGui::TableSetupColumn("Teclado"); ImGui::TableSetupColumn("Controle");
                ImGui::TableHeadersRow();
                for (int i = 0; i < NBTN; i++) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%s", BTN_NAMES[i]);
                    ImGui::TableNextColumn();
                    std::string kl = remap_index == i ? "...pressione uma tecla..." : ui::key_name(keys[i]);
                    if (ImGui::Button((kl + "##k").c_str(), ImVec2(200, 0))) remap_index = i;
                    ImGui::TableNextColumn();
                    std::string pl = remap_index == NBTN + i ? "...pressione um botão..." : SDL_GameControllerGetStringForButton(padb[i]);
                    if (ImGui::Button((pl + "##p").c_str(), ImVec2(200, 0))) remap_index = NBTN + i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Restaurar padrão")) {
                for (int i = 0; i < NBTN; i++) { cfg.kv.erase(std::string("tecla_") + BTN_IDS[i]); cfg.kv.erase(std::string("controle_") + BTN_IDS[i]); }
                cfg.save(); apply_settings();
            }
            int thz = cfg.geti("turbo_hz", 15);
            ImGui::SetNextItemWidth(std::min(220.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
            if (ImGui::SliderInt("Velocidade do turbo", &thz, 5, 30, "%d toques/s")) cfg.seti("turbo_hz", thz);
            if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
            ImGui::SeparatorText("Voltar no tempo e acelerar");
            bool rw = cfg.getb("rebobinar", true);
            if (ImGui::Checkbox("Permitir voltar no tempo", &rw)) { cfg.set("rebobinar", rw ? "1" : "0"); cfg.save(); rewind_clear(); }
            ImGui::SameLine(); ImGui::TextDisabled("(segure Backspace ou o gatilho esquerdo LT/L2)");
            {
                static const char* ids[] = {"30", "60", "120", "300"};
                static const char* names[] = {"30 segundos (pouca memória)", "1 minuto", "2 minutos", "5 minutos (cerca de 60 MB)"};
                ImGui::BeginDisabled(!rw);
                ImGui::SetNextItemWidth(std::min(260.f * uis, ImGui::GetContentRegionAvail().x * 0.6f));
                if (combo_setting(cfg, "Quanto tempo guardar", "segundos_rebobinar", ids, names, 4, "60")) rewind_clear();
                bool rs = cfg.getb("rebobinar_som", true);
                if (ImGui::Checkbox("Som ao contrário enquanto volta", &rs)) { cfg.set("rebobinar_som", rs ? "1" : "0"); cfg.save(); }
                ImGui::TextDisabled("Segurando mais tempo, volta mais rápido (2x, 4x, 8x). No menu, aba Jogo, há a linha do tempo com imagens.");
                ImGui::EndDisabled();
            }
            int sp = cfg.geti("velocidade_rapida", 4);
            ImGui::SetNextItemWidth(std::min(220.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
            if (ImGui::SliderInt("Velocidade acelerada", &sp, 2, 10, "%dx")) cfg.seti("velocidade_rapida", sp);
            if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
            ImGui::SameLine(); ImGui::TextDisabled("(segure Tab ou o gatilho direito RT/R2)");
            ImGui::SeparatorText("Menu pelo controle");
            ImGui::TextDisabled("Abrir: botão Guia/Home ou Select+Start. Navegar: direcional. Confirmar: A. Voltar: B. Fechar: Start.\nTrocar de aba: LB/RB (L1/R1).");
            ImGui::Text("Controles conectados: %d", (int)pads.size());
            for (auto* p : pads) ImGui::BulletText("%s", SDL_GameControllerName(p) ? SDL_GameControllerName(p) : "Controle");
            
        }
        if (tab("Trapaças", 4)) { draw_cheat_tab();  }
        if (tab("Acessibilidade", 5)) {
            static const char* cvd_ids[] = {"nenhum", "protanopia", "deuteranopia", "tritanopia"};
            static const char* cvd_names[] = {"Nenhuma", "Protanopia (vermelho)", "Deuteranopia (verde)", "Tritanopia (azul)"};
            ImGui::BeginDisabled(fid);
            if (combo_setting(cfg, "Correção para daltonismo", "daltonismo", cvd_ids, cvd_names, 4, "nenhum")) frame_ready = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Desloca as cores que se confundem para tons que a pessoa distingue melhor.\nFunciona em jogos coloridos (GBC/SGB) e nas paletas de cor.");
            ImGui::TextDisabled("Para jogos de Game Boy clássico, as paletas \"Acessível p/ daltonismo\" e \"Alto contraste\" (aba Vídeo) também ajudam.");
            ImGui::EndDisabled();
            int ui = cfg.geti("escala_interface", 100);
            ImGui::SetNextItemWidth(std::min(220.f * uis, ImGui::GetContentRegionAvail().x * 0.55f));
            if (ImGui::SliderInt("Tamanho do menu", &ui, 100, NB_UI_MAX, "%d%%")) cfg.seti("escala_interface", ui);
            if (ImGui::IsItemDeactivatedAfterEdit()) cfg.save();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Aumenta o texto deste menu (bom para jogar na TV).");
            bool ab = cfg.getb("abertura", true);
            if (ImGui::Checkbox("Mostrar a tela de abertura ao iniciar", &ab)) { cfg.set("abertura", ab ? "1" : "0"); cfg.save(); }
            bool ct = cfg.getb("continuar", true);
            if (ImGui::Checkbox("Perguntar se quer continuar de onde parou", &ct)) { cfg.set("continuar", ct ? "1" : "0"); cfg.save(); }
            
        }
        if (tab("Mods", 6)) {
            if (fid) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Modo fidelidade ativo: mods desativados.");
            ImGui::TextWrapped("%s", natsep("Mods ficam em assets/mods/<nome>/ e podem conter: patch.ips (dados: gráficos, textos, tabelas), "
                               "cheats.txt (códigos GameShark 01VVAAAA), hd/ (pacote de gráficos HD). "
                               "Alterações de código exigem reconstruir o jogo no NativeBoy Studio.").c_str());
            bool changed = false;
            for (auto& mi : mods) {
                ImGui::PushID(mi.id.c_str());
                if (ImGui::Checkbox(mi.name.c_str(), &mi.enabled)) changed = true;
                if (!mi.desc.empty()) { ImGui::SameLine(); ImGui::TextDisabled("- %s", mi.desc.c_str()); }
                ImGui::PopID();
            }
            if (mods.empty()) ImGui::TextDisabled("Nenhum mod instalado.");
            if (changed) {
                std::string en;
                for (auto& mi : mods) if (mi.enabled) en += (en.empty() ? "" : ",") + mi.id;
                cfg.set("mods", en); cfg.save();
                notify("Mods alterados: reinicie o jogo para aplicar patches");
            }
            if (ImGui::Button("Reiniciar com os mods selecionados")) { reset(); menu_open = false; }
            ImGui::SameLine();
            if (ImGui::Button("Abrir pasta de mods")) plat::open_in_explorer(plat::join(base_dir, "assets/mods"));
            if (!mods_log.empty()) { ImGui::SeparatorText("Registro"); ImGui::TextUnformatted(mods_log.c_str()); }
            
        }
        if (tab("Sobre", 7)) {
            const RomInfo& ri = m.cart.info;
            ImGui::Text("%s", game_name.c_str());
            {   // the game's official name, when the Studio recognised it (config/jogo.ini)
                Settings gi; gi.load(plat::join(base_dir, "config/jogo.ini"));
                std::string on = gi.get("nome_oficial", "");
                if (!on.empty()) ImGui::TextDisabled("Jogo reconhecido: %s", on.c_str());
                std::string line;
                auto add = [&](const char* label, const std::string& v) { if (!v.empty()) line += (line.empty() ? "" : "   ·   ") + std::string(label) + v; };
                add("Gênero: ", gi.get("genero", "")); add("Ano: ", gi.get("ano", ""));
                std::string pl = gi.get("jogadores", ""); add("Jogadores: ", pl == "1" || pl.empty() ? pl : "1 a " + pl);
                if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
                line.clear();
                add("Editora: ", gi.get("editora", ""));
                if (gi.get("desenvolvedora", "") != gi.get("editora", "")) add("Desenvolvedora: ", gi.get("desenvolvedora", ""));
                add("Série: ", gi.get("serie", ""));
                if (!line.empty()) ImGui::TextDisabled("%s", line.c_str());
            }
            ImGui::Text("Cartucho: %s | %s | %d bancos de ROM", ri.mbc_name.c_str(), m.cgb ? "Game Boy Color" : "Game Boy", ri.rom_banks);
            if (emulation) {
                ImGui::TextWrapped("Este jogo está rodando por EMULAÇÃO: cada instrução SM83 é interpretada em tempo de execução.");
            } else {
                u64 total = m.fb_stats.native_blocks + m.fb_stats.rom_instr + m.fb_stats.ram_instr;
                ImGui::TextWrapped("Este jogo é um EXECUTÁVEL NATIVO: o código SM83 foi traduzido para C++ e compilado para x64.");
                ImGui::Text("Entradas em código nativo: %llu", (unsigned long long)m.fb_stats.native_blocks);
                ImGui::Text("Instruções via interpretador de compatibilidade: ROM %llu, RAM %llu",
                            (unsigned long long)m.fb_stats.rom_instr, (unsigned long long)m.fb_stats.ram_instr);
                if (m.fb_stats.rom_instr + m.fb_stats.ram_instr == 0) ImGui::TextColored(ImVec4(0.4f, 1, 0.6f, 1), "100%% nativo nesta sessão.");
                else ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", natsep("Trechos não traduzidos foram executados (" + std::to_string(m.fb_stats.missing.size()) +
                                                                    " pontos). Eles serão listados em config/missing_code.txt\npara você importar no NativeBoy Studio e recompilar.").c_str());
                (void)total;
            }
            ImGui::Separator();
            ImGui::TextDisabled("Gerado pelo NativeBoy Studio. Núcleo de compatibilidade e runtime: NativeBoy.");
            
        }
        if (tab("Conquistas", 8)) { draw_achievements_tab();  }
        if (tab("Capturas", 9)) { draw_gallery_tab();  }
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::EndGroup();
    want_tab = -1;
    ImGui::End();
    ImGui::PopFont();
}
