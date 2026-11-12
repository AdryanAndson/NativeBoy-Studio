// GamePlayer: runs a game (native or emulated) with the modern layer:
// controllers/keyboard + remapping, scaling filters, V-Sync, audio options,
// battery saves, save states, screenshots, mods (IPS data patches, cheats,
// HD packs) and an in-game menu. Used by exported games and by the Studio.
#pragma once
#include "../core/gb.h"
#include "settings.h"
#include "video.h"
#include "link.h"
#include "music.h"
#include "recorder.h"
#include <map>
#include <memory>
#include <SDL.h>
#include <string>
#include <vector>

struct ModInfo {
    std::string id, name, desc, dir;
    bool has_ips = false, has_cheats = false, has_hd = false, has_palette = false;
    bool enabled = false;
};

struct GamePlayer {
    gb::Machine m;
    gb::NativeResolver* native = nullptr;     // null = emulation (interpreter)
    bool emulation = true;
    std::string game_name, game_id;
    std::string base_dir;      // folder with assets/ config/ saves/
    std::string user_dir;      // where saves/ config/ capturas/ are written (= base_dir unless the game is installed read-only)
    Settings cfg;
    std::vector<gb::u8> rom_original;

    // state
    bool running = false, paused = false, menu_open = false, fast = false;
    bool want_quit = false, want_fullscreen_toggle = false, want_resize = false;
    int remap_index = -1;
    int menu_tab = 0, want_tab = -1;     // controller: LB/RB change the menu tab      // waiting for a key (0-7) or pad button (8-15)
    std::string notice; Uint32 notice_until = 0;
    double fps = 0; Uint32 fps_t0 = 0; int fps_frames = 0;

    // video
    VideoStage vs;
    HdPack hd; std::string hd_error;
    std::vector<std::string> hd_packs;
    SDL_Texture* tex = nullptr; int tex_w = 0, tex_h = 0; bool tex_linear = false;
    bool frame_ready = true;
    // side fill (what appears around the game when the window is wider/taller than the picture)
    SDL_Texture* amb_a = nullptr; SDL_Texture* amb_b = nullptr;   // blurred "ambient" copies of the picture
    SDL_Texture* bg_img = nullptr; int bg_w = 0, bg_h = 0; std::string bg_path;
    bool amb_dirty = true;
    void draw_sides(SDL_Renderer* r, const SDL_Rect& area, const SDL_Rect& dst, const SDL_Rect& src, const std::string& mode);
    void free_side_textures();
    std::string background_image_path() const;
    TileDumper* dumper = nullptr;   // Studio: collect tiles for HD authoring

    // audio
    SDL_AudioDeviceID audio = 0;
    // input (0-7 = Game Boy buttons, 8 = turbo A, 9 = turbo B)
    SDL_Scancode keys[10];
    SDL_GameControllerButton padb[10];
    std::vector<SDL_GameController*> pads;
    gb::u8 kb_state = 0, pad_state = 0, axis_state = 0;
    // mods
    std::vector<ModInfo> mods;
    struct Cheat { gb::u16 addr; gb::u8 val; };
    std::vector<Cheat> cheats;
    std::string mods_log;
    // link cable, rumble, tilt
    NetLink netlink;
    char link_addr[128] = "";
    bool link_scanning = false;
    void draw_link_section(float uis);      // menu: host / connect by code / players on the same network / router port
    bool rumble_on = false;
    // stats
    Uint32 last_battery_save = 0;

    // ---- rewind / fast forward
    struct Snap {
        std::shared_ptr<std::vector<gb::u8>> key; std::vector<gb::u8> delta;   // delta empty = the key itself
        std::vector<int16_t> audio;          // sound played since the previous moment (stereo), heard backwards when rewinding
        std::vector<uint16_t> thumb;         // 160x144 picture (RGB565), once per second, for the timeline
        double when = 0;                     // play time of this moment, in seconds
    };
    std::vector<Snap> rewind_ring; size_t rw_head = 0, rw_count = 0;
    std::shared_ptr<std::vector<gb::u8>> rw_key; int rw_since_key = 0, rw_tick = 0, rw_since_thumb = 0;
    std::vector<int16_t> rw_pending;         // sound since the last moment, not yet stored
    size_t rw_out_mark = 0;                  // how much of the sound buffer is already in rw_pending
    bool rewinding = false, fast_shown = false;
    Uint32 rw_hold_t0 = 0;                   // when the rewind key was pressed (holding longer goes faster)
    double rw_back_seconds = 0;              // how far this rewind went, for the on-screen counter
    unsigned rw_serial = 0;                  // changes whenever the timeline changes
    void rewind_capture();
    bool rewind_step(int n = 1);
    bool rewind_jump(size_t back);           // go back to the moment `back` steps ago and drop the newer ones
    double rewind_seconds() const;           // how much time is stored
    void rewind_clear() { rewind_ring.clear(); rw_head = rw_count = 0; rw_key.reset(); rw_since_key = 0; rw_since_thumb = 0; rw_pending.clear(); rw_out_mark = 0; rw_serial++; }
    void rewind_audio_mark();                // called before the sound buffer is cleared
    void draw_timeline_section();            // pause menu: pictures of the last minutes to go back to
    std::map<size_t, SDL_Texture*> tl_tex;   // ring index -> texture of its picture
    unsigned tl_serial = ~0u; int tl_sel = -1;

    // ---- save state slots with thumbnails, "continue where you left off"
    SDL_Renderer* ren = nullptr;
    std::map<int, SDL_Texture*> thumbs;     // slot -> thumbnail (0 = automatic save on exit)
    bool ask_continue = false;
    std::string state_path(int slot) const;
    std::string slot_info(int slot) const;  // date/time text or ""
    SDL_Texture* thumb(int slot);
    void drop_thumb(int slot);
    void draw_continue_prompt(const SDL_Rect& area);

    // ---- accessibility
    std::vector<uint32_t> cvd_buf;          // colour-blind correction output
    std::vector<uint32_t> soft_buf;         // widescreen: picture with see-through areas the game has not drawn yet
    std::vector<uint8_t> soft_alpha;
    bool soft_active = false;
    gb::WideScout scout;                    // look-ahead copy of the game for the scenery ahead
    gb::Machine::RunAhead ahead;            // run-ahead: less input lag (the real state comes back before each frame)
    void ahead_off() { m.ahead_end(ahead); }
    void scenery_save(); void scenery_load();   // remembered places kept in the save folder
    float ext_top = 0, ext_bot = 0;         // extra rows currently shown above / below (animated)
    SDL_Texture* sharp_tex = nullptr; int sharp_w = 0, sharp_h = 0;   // "nítida" scaling: integer step, then smooth
    bool ambient_update(SDL_Renderer* r, const SDL_Rect& src);
    // ---- splash screen
    SDL_Texture* splash = nullptr; int splash_w = 0, splash_h = 0; Uint32 splash_t0 = 0; bool splash_on = false;

    // ---- soundtrack replacement and audio recording
    MusicPack music; MusicPlayer music_out; WavRecorder wav;
    bool in_studio = false;                 // Studio preview: no automatic state, no "continue" prompt
    int music_last = -1; bool music_active = false;
    void music_update();
    void apply_mutes();

    // ---- "PC game" layer: title screen, achievements, play time, display modes
    bool start_screen = false, start_load = false;
    int start_sel = 0, start_move = 0; bool start_go = false;   // keyboard / controller on the title screen
    SDL_Texture* title_bg = nullptr; int title_w = 0, title_h = 0;
    SDL_Texture* title_icon = nullptr; bool title_icon_tried = false;   // the game's icon on the title screen
    struct Ach { std::string id, name, desc; gb::u16 addr = 0; int op = 0; int val = 0; bool done = false, armed = false; std::string when; };
    std::vector<Ach> achs;
    struct Toast { std::string title, text; Uint32 until; };
    std::vector<Toast> toasts;
    std::string studio_mods_dir;            // Studio: the project's mods/ folder (soundtrack, achievements)
    double play_seconds = 0; Uint32 play_t0 = 0, play_saved = 0;
    bool want_display_apply = false;        // fullscreen type / resolution changed
    bool mouse_idle = true;                 // set by the window owner: pointer not moved for a while
    void draw_topbar(const SDL_Rect& area);
    SDL_Texture* bloom_tex = nullptr; int bloom_w = 0, bloom_h = 0;
    void load_achievements();
    void check_achievements();
    void save_achievements();
    void save_play_time();
    std::string play_time_text() const;
    void draw_start_screen(const SDL_Rect& area);
    void draw_achievements_tab();
    void render_bloom(SDL_Renderer* r, const SDL_Rect& dst);

    // ---- recording and gallery
    GifRecorder gif; VideoRecorder video;
    void toggle_gif();
    void toggle_video();
    void record_frame();                    // called once per emulated frame
    void draw_gallery_tab();
    std::map<std::string, SDL_Texture*> gallery_thumbs;
    std::string captures_dir() const;

    // ---- cheat finder (RAM search)
    struct Cand { gb::u16 addr; gb::u8 prev; };
    std::vector<Cand> cands; bool search_started = false; int search_value = 0;
    gb::u8 peek(gb::u16 a) const;
    void search_begin();
    void search_filter(int mode);           // 0 equal value, 1 changed, 2 unchanged, 3 increased, 4 decreased
    void add_user_cheat(gb::u16 addr, gb::u8 val);
    void draw_cheat_tab();
    void draw_states_section();

    bool start(const std::vector<gb::u8>& rom, const std::string& base_dir, const std::string& name,
               gb::NativeResolver* native, std::string& err);
    void stop();                         // saves battery RAM, closes audio
    void handle_event(const SDL_Event& e);
    void update();                       // runs the right amount of frames
    void render(SDL_Renderer* r, const SDL_Rect& area);
    void draw_menu(const SDL_Rect& area);
    void draw_overlay(const SDL_Rect& area);

    // ---- console look: the window becomes a handheld drawn in the classic (GB) or Color (GBC) style
    struct ConsoleLayout {
        bool on = false; int style = 0;          // 0 classic, 1 Color
        float u = 1, ox = 0, oy = 0;             // pixels per design unit, body origin
        SDL_FRect shell{}, bezel{};
        SDL_Rect screen{};                       // where the game picture goes
        SDL_FPoint led{}, dpad{}, a{}, b{}, start{}, select{}, logo{}, menu_btn{}, min_btn{}, close_btn{};
        float dpad_r = 0, ab_r = 0, pill_w = 0, pill_h = 0, icon_r = 0;
    } con;
    struct ConsoleHit { enum Kind { NONE, BODY, SCREEN, BUTTON, MENU, MINIMIZE, CLOSE } kind = NONE; uint8_t buttons = 0; };
    bool console_on() const;
    void console_layout(const SDL_Rect& area);
    void console_draw(SDL_Renderer* r, const SDL_Rect& area);
    void console_draw_front(SDL_Renderer* r);
    void console_labels();
    ConsoleHit console_hit(float px, float py) const;   // renderer coordinates
    uint8_t mouse_buttons = 0;               // Game Boy buttons held with the mouse / a touch on the drawn console
    bool want_minimize = false;
    // ---- power on / off animation
    int power_state = 0;                     // 1 turning on, 2 turning off, 3 off (window may close)
    Uint32 power_t0 = 0;
    bool power_chimed = false;
    void power_on_begin();
    void power_off_begin();
    bool power_off_done() const;
    float power_led() const;
    void power_chime();
    bool power_render(SDL_Renderer* r, const SDL_Rect& scr);
    void power_labels(const SDL_Rect& scr);
    SDL_Rect power_screen{};                 // where the animation was drawn (for its labels)
    void render_game(SDL_Renderer* r, const SDL_Rect& area);
    void reset();
    void notify(const std::string& s);
    bool fidelity() const { return cfg.getb("fidelity", false); }

    std::string saves_dir() const;
    std::string config_dir() const;
    bool save_state(int slot);
    bool load_state(int slot);
    void screenshot();
    void apply_settings();               // palette, color, volume, hd...
    void scan_mods();
    void load_mods_and_rom(std::vector<gb::u8>& rom);
    void write_diagnostics();
};

void daltonize(uint32_t* px, size_t n, const std::string& type);   // colour-blind correction (protanopia/deuteranopia/tritanopia)

bool apply_ips(std::vector<gb::u8>& data, const std::vector<gb::u8>& ips, std::string& err);
