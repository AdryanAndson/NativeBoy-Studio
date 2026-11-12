// NativeBoy core - shared Game Boy / Game Boy Color machine model.
// Used by: the Studio emulator (JOGAR ROM), the discovery tracer,
// and the runtime of statically recompiled games.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace gb {

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

enum { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };
enum { IRQ_VBLANK = 1, IRQ_STAT = 2, IRQ_TIMER = 4, IRQ_SERIAL = 8, IRQ_JOYPAD = 16 };
enum { BTN_RIGHT = 1, BTN_LEFT = 2, BTN_UP = 4, BTN_DOWN = 8, BTN_A = 16, BTN_B = 32, BTN_SELECT = 64, BTN_START = 128 };

struct RomInfo {
    std::string title;
    u8 cgb_flag = 0, sgb_flag = 0, cart_type = 0, rom_size_code = 0, ram_size_code = 0, version = 0;
    bool cgb_only = false, cgb_supported = false;
    int mbc = 0;            // 0,1,2,3,5
    bool has_ram = false, has_battery = false, has_rtc = false, has_rumble = false;
    int rom_banks = 2, ram_size = 0;
    u8 header_checksum = 0, header_checksum_calc = 0;
    u16 global_checksum = 0, global_checksum_calc = 0;
    bool logo_ok = false;
    std::string mbc_name;
    bool supported = true;
    std::string problem;     // human readable reason if not supported
};
bool parse_header(const std::vector<u8>& rom, RomInfo& out);

struct Cart {
    std::vector<u8> rom;
    std::vector<u8> ram;
    RomInfo info;
    int rom_bank = 1;        // currently mapped at 4000-7FFF
    int rom_bank0 = 0;       // mapped at 0000-3FFF (MBC1 mode 1)
    int ram_bank = 0;
    bool ram_enabled = false;
    int mbc1_mode = 0;
    u8 bank_lo = 1, bank_hi = 0;
    // MBC3 RTC
    u8 rtc[5] = {0, 0, 0, 0, 0}, rtc_latched[5] = {0, 0, 0, 0, 0};
    u8 rtc_latch_prev = 0xFF;
    u64 rtc_cycles = 0;
    bool ram_dirty = false;
    bool rumble = false;     // MBC5 rumble motor state
    // MBC7 (accelerometer + 93LC56 serial EEPROM, stored in ram[0..255])
    bool mbc7_en2 = false, accel_ready = true;
    u16 accel_x = 0x8000, accel_y = 0x8000;
    u8 ee_cs = 0, ee_clk = 0, ee_di = 0, ee_do = 1;
    int ee_state = 0, ee_bits = 0;
    u16 ee_sr = 0, ee_out = 0;
    bool ee_we = false;
    int ee_addr = 0;
    // HuC1 / HuC3
    u8 huc_mode = 0;
    u8 huc3_mem[256] = {};   // RTC nibbles (0-2 minutes of day, 3-5 day counter)
    u8 huc3_addr = 0, huc3_resp = 0;
    u64 huc3_base = 0;       // cycle count at which huc3 minutes were last advanced
};

struct PixelInfo {          // per output pixel, used for HD tile packs
    u16 tile;               // vram bank * 384 + tile index, 0xFFFF = none
    u8 pal;                 // 0-7 bg palettes, 8-15 obj palettes
    u8 sub;                 // x | y<<3 inside the tile (after flips)
};

struct FallbackStats {
    u64 rom_instr = 0;      // instructions executed by the fallback interpreter from ROM
    u64 ram_instr = 0;      // instructions executed from RAM (self-copied code)
    u64 native_blocks = 0;  // native chunk entries
    std::vector<u32> missing; // (bank<<16)|addr of ROM code not translated (unique)
};

struct Machine;
typedef bool (*NativeFn)(Machine&);

// Link cable partner (implemented by the front-end, e.g. over TCP)
struct SerialLink {
    virtual ~SerialLink() {}
    virtual u8 exchange_master(Machine& m, u8 out) = 0;   // we drive the clock: send a byte, get the partner's
    virtual void poll(Machine& m) = 0;                     // partner may be driving the clock
};

// Native code resolver, implemented by recompiled games.
struct NativeResolver {
    virtual ~NativeResolver() {}
    virtual NativeFn find(int bank, u16 pc) = 0;
};

struct Apu;
struct Sgb;

struct Machine {
    // ---------------- CPU ----------------
    u8 a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    u16 sp = 0, pc = 0;
    bool ime = false;
    int ei_delay = 0;
    bool halted = false;
    bool ev = false;            // "leave native code and return to the dispatcher"
    bool frame_done = false;
    bool locked = false;        // illegal opcode executed
    bool in_fallback = false;   // currently running untranslated ROM code
    bool halt_bug = false;      // HALT with IME=0 and a pending interrupt: next opcode byte is read twice

    // ---------------- memory ----------------
    bool cgb = false;
    bool double_speed = false;
    u8 vram[2][0x2000];
    u8 wram[8][0x1000];
    u8 oam[0xA0];
    u8 io[0x80];
    u8 hram[0x80];
    u8 ie = 0, iflag = 0;
    int vram_bank = 0, wram_bank = 1;
    Cart cart;
    const u8* rpage[16];
    u8* wpage[16];

    // ---------------- timer ----------------
    u16 div = 0;

    // ---------------- PPU ----------------
    int dot = 0;               // dot inside the current line
    int ly = 0;
    int mode = 2;
    bool stat_line = false;
    int win_line = 0;
    bool lcd_was_off = false;
    int lcd_off_dots = 0;
    u8 bgpal[64], objpal[64];  // CGB palette RAM
    bool hdma_active = false;
    u16 hdma_src = 0, hdma_dst = 0;
    int hdma_len = 0;
    u32 fb[160 * 144];         // ARGB8888 output (original 160x144 screen; used for verification)
    // Wide output: 256x144 (16:9). The original screen is columns 48..207.
    enum { WIDE_W = 256, WIDE_X0 = 48 };
    u32 fbw[WIDE_W * 144];
    PixelInfo pinfo[WIDE_W * 144];   // per pixel tile identity (HD packs), same layout as fbw
    bool track_pixels = false;
    // optional enhancements (never used by the verification)
    bool widescreen = false;     // render 48 extra columns on each side from the background map
    u8 wide_edge = 1;            // 0 = show everything, 1 = hide map columns the game left stale, 2 = 1 + fade the edges
    u32 map_col_frame[2][32] = {};   // frame of the last write to each tile-map column (stale-column detection)
    u8 map_col_behind[2][32] = {};   // last write happened while the column was just behind the camera (a clear, not new scenery)
    int wide_dir_x = 0;              // camera direction (-1 left, 1 right, 0 not moved yet)
    u32 lcd_off_frame = 0;           // last time the screen was switched off (columns older than that are leftovers)
    u32 line_fresh_min = 0;          // oldest write among the visible columns of the current line
    int line_cam_x = 0, line_cam_y = 0;   // world position of the scroll used by the current line
    // widescreen scenery memory: what scrolled out of view is remembered in "world" coordinates and
    // shown again at the sides instead of stale map columns (colour index + palette, so fades still apply)
    bool wide_memory = true, wide_hud = true, wide_parked = false;   // memory, extend HUD rows, show parked sprites
    u8 wflags[WIDE_W * 144];         // per pixel this frame: 1 sprite, 2 window, 4 stale map column
    u8 wbg[WIDE_W * 144];            // per pixel: bg colour index | palette << 2
    u8 wmc[WIDE_W * 144];            // per pixel: tile-map column | map select << 5
    u8 wline_scx[144], wline_scy[144];
    std::unordered_map<u64, std::vector<u32>> world;   // 32x32 chunks: bit 7 known, 0-1 colour, 2-4 palette, 8-31 frame seen
    int world_x = 0, world_y = 0, world_ref_scx = -1, world_ref_scy = -1;
    u32 frame_map_writes = 0;        // tile-map bytes written this frame (screen changes)
    void wide_memory_pass();
    // smart widescreen: each frame judges whether the extra columns show something real; when both sides
    // are mostly leftovers (menus, screens redrawn only in the visible area) or a full-screen window is up,
    // the picture smoothly narrows back to 160 columns and widens again once the scene is good
    bool wide_smart = false;
    float wide_vis = 1.0f;           // 0 = original width, 1 = full widescreen (animated per frame)
    bool wide_narrow = false;        // current decision
    int wide_bad_run = 0, wide_good_run = 0;
    float wide_bad_l = 0, wide_bad_r = 0;   // last frame: fraction of unusable pixels at each side
    bool wide_menu = false;          // last frame: window covering most of the screen
    void wide_judge(int rsx, int rsy);
    void world_clear() { wide_cuts++; world.clear(); world_ref_scx = world_ref_scy = -1; world_x = world_y = 0; }
    // places already visited: when the game leaves a scene (house, menu, other area) its memory is kept;
    // when a new screen matches one of them (same tiles at some offset), that memory comes back, so the
    // side the player walks towards shows what was there before
    struct WideScene {
        std::unordered_map<u64, std::vector<u32>> world;
        std::unordered_map<u64, std::vector<u64>> index;   // hash of an 8x8 block -> block positions
        bool indexed = false;
        u32 frame = 0;
        int cam_x = 0, cam_y = 0;    // where the camera was when the player left
        bool imported = false;       // loaded from the save folder (where the player left is not meaningful)
    };
    std::vector<WideScene> wide_scenes;
    bool wide_places = true;         // option: remember visited places
    int reloc_pending = 0;           // frames left to look for a known place after a scene change
    u32 reloc_count = 0;             // places recognised (statistics / tests)
    void wide_archive_scene();
    bool wide_relocate(int rsx, int rsy);
    void world_trim();
    // extended view (ultrawide + taller picture): 352x208, the original screen starts at (XX0, XY0).
    // Built once per frame from the wide picture, the tile map (rows above/below that the game keeps
    // fresh) and the scenery memory. xknown = 0 where nothing real is known (the player fades those).
    enum { XW = 352, XH = 208, XX0 = 96, XY0 = 32 };
    bool wide_extend = false;        // build fbx (ultrawide or vertical expansion in use)
    std::vector<u32> fbx;            // XW * XH
    std::vector<u8> xknown;          // XW * XH
    u32 map_row_frame[2][32] = {};   // frame of the last write to each tile-map row
    // per tile of the two maps: the world position (scenery memory coordinates) the game wrote it for and when.
    // A tile shown at the sides is real scenery only if it was written for the place where it now appears
    // (scrolling games reuse the map like a ring, and write whole rows or columns at a time)
    int map_tile_wx[2][1024] = {}, map_tile_wy[2][1024] = {};
    u32 map_tile_frame[2][1024] = {};
    u32 scene_start = 0;             // tiles written before this frame belong to an older screen
    void map_rebase();               // the map as it is now counts as matching the camera (new scene, loaded state)
    bool tile_here(int mp, int mi, int wx, int wy) const {     // tile mi was written for world position (wx, wy)
        return map_tile_frame[mp][mi] && map_tile_frame[mp][mi] >= scene_start && map_tile_wx[mp][mi] == wx && map_tile_wy[mp][mi] == wy;
    }
    // a map position written now (0..255 from the scroll) is meant for the side the camera goes to:
    // games write new columns / rows well ahead of the screen
    // (positions just left of / above the screen are taken as behind the camera: games clear or reuse those)
    int map_rel_x(int rel) const { return rel >= 160 + WIDE_X0 ? rel - 256 : rel; }
    int map_rel_y(int rel) const { return rel >= 200 ? rel - 256 : rel; }
    int cam_x() const { return world_x + (world_ref_scx < 0 ? 0 : (s8)(u8)(io[0x43] - world_ref_scx)); }   // world position of SCX now
    int cam_y() const { return world_y + (world_ref_scy < 0 ? 0 : (s8)(u8)(io[0x42] - world_ref_scy)); }
    u8 map_row_behind[2][32] = {};   // last write happened while the row was just behind the camera
    int wide_dir_y = 0;              // camera direction (-1 up, 1 down)
    bool wide_top_ok = false, wide_bot_ok = false;   // rows above / below the screen may be shown (no status bar there)
    // --- scenery memory, second generation
    // each 32x32 chunk holds 2048 values: [0,1024) the remembered pixel (bit 7 known, 0-1 colour, 2-4 palette,
    // bit 5 = predicted by the scout, 8-31 frame seen) and [1024,2048) the tile it came from ((ref + 1) << 8),
    // so animated tiles (water, torches) keep animating where the scenery is only remembered
    u16 wtile[WIDE_W * 144];         // per pixel: tile (bank * 384 + number) | x << 10 | y << 13 inside it, 0xFFFF none
    u8 tile_anim[768] = {};          // how often each tile's graphics were rewritten lately (animated tiles)
    u32 tile_wframe[768] = {};
    bool wide_animate = true;        // remembered scenery follows animated tiles
    bool wide_sprites = true;        // objects outside the original screen are drawn when they are really in play
    u8 spr_px[40] = {}, spr_py[40] = {}, spr_smooth[40] = {};
    bool spr_parked[40] = {};
    void wide_track_sprites();
    u32 wide_cuts = 0;               // scene changes so far
    u32 wide_moved_x = 0, wide_moved_y = 0;   // frame of the last camera movement in each axis
    std::vector<u8> wide_export() const;      // all remembered places (for the save folder)
    bool wide_import(const std::vector<u8>& d);
    void wide_merge_predicted(const Machine& scout);   // scenery seen by a look-ahead copy of the game
    float wide_top_score = 0, wide_bot_score = 0;    // smoothed share of known pixels in the extra rows
    void wide_extend_pass(int rsx, int rsy);
    bool no_sprite_limit = false;// draw every sprite on a line (original hardware: max 10, causes flicker)
    u32 dmg_palette[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};
    u32 dmg_obj0[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};
    u32 dmg_obj1[4] = {0xFFE0F8D0, 0xFF88C070, 0xFF346856, 0xFF081820};
    bool color_correct = true;
    bool pal_dirty = true;       // CGB palette cache must be rebuilt
    u32 bgcol[8][4], objcol[8][4];   // CGB palettes converted to ARGB
    u32 vram_write_gen = 0;    // increments on each VRAM write (for caches)
    u32 tile_gen[768];         // last generation each tile was written

    // ---------------- input / serial ----------------
    u8 buttons = 0;            // BTN_* bits, 1 = pressed
    std::string serial_out;
    SerialLink* link = nullptr;  // link cable (null = nothing connected)
    u64 link_poll_at = 0;
    u8 serial_slave_exchange(u8 in);   // byte arrives from a partner that drives the clock
    float tilt_x = 0, tilt_y = 0;      // MBC7 accelerometer input, -1..1 (1 = about 1 g)
    void huc3_command(u8 v);
    void mbc7_write(u16 addr, u8 v);

    // ---------------- Super Game Boy ----------------
    bool sgb_mode = false;       // emulate an SGB when the game supports it (set before load_rom)
    Sgb* sgb = nullptr;          // non-null while SGB features are active
    u8 line_shade[WIDE_W];       // 2-bit grey level of each pixel of the current line (SGB colouring)

    // ---------------- audio ----------------
    Apu* apu = nullptr;

    // ---------------- stats ----------------
    u64 cycles = 0;
    u64 frames = 0;
    FallbackStats fb_stats;
    std::vector<u8> missing_seen;  // bitmap (bank*0x4000+off) for missing log
    // optional execution trace (code discovery). Called with (bank, addr) for ROM addresses.
    std::vector<u8>* trace_bitmap = nullptr;   // size rom.size(), set to 1 for executed instruction starts
    // RAM code discovery: first-seen bytes of every instruction executed from RAM
    struct RamExec { u16 pc; u8 bytes[3]; };
    std::vector<RamExec>* ram_trace = nullptr;
    std::vector<u64> ram_seen;                 // hash set of (pc, bytes) already recorded

    Machine();
    ~Machine();
    bool load_rom(const std::vector<u8>& rom, std::string& err, bool force_dmg = false);
    void reset();
    void map_pages();

    u8 read_slow(u16 addr);
    void write_slow(u16 addr, u8 v);
    u8 io_read(u16 addr);
    void io_write(u16 addr, u8 v);
    void mbc_write(u16 addr, u8 v);

    // ---- timing -------------------------------------------------------------
    // The CPU (native code or interpreter) only advances 'cycles'. Video, sound and
    // timers are brought up to date lazily: when the program touches the hardware
    // (I/O registers) or when the next hardware event is due (end of a video mode,
    // timer overflow, end of frame). The observable behaviour is identical to
    // stepping the hardware after every instruction, but the hot path is a single
    // add + compare that the compiler inlines into the translated code.
    u64 synced = 0;            // cycles already applied to timer/PPU/APU
    u64 next_event = 0;        // when cycles >= next_event, slow_tick() runs
    u64 hw_syncs = 0;          // statistics
    inline void tick(int t) {
        cycles += (u64)t;
        if (__builtin_expect(cycles >= next_event, 0)) slow_tick();
    }
    void slow_tick();
    void sync();               // apply pending cycles to the hardware
    void schedule();           // compute next_event
    void force_event() { next_event = 0; }
    void timer_tick(int t);
    void ppu_tick(int t);
    // per-line renderer state (a line is drawn in segments so that register writes in
    // the middle of mode 3 affect only the pixels after them, like on the hardware)
    int line_x = 0;
    bool wy_triggered = false;
    bool line_win_active = false;  // window started on this line (stays until the end of the line)
    int line_win_x0 = 0;           // WX-7 when the window started
    int line_scx_low = 0;          // SCX & 7 latched at the start of mode 3
    bool ly153_zero = false;      // line 153 quirk already applied  // the window can only appear after LY == WY happened in this frame
    bool line_win_used = false;
    int line_sel[40], line_nsel = 0;
    u32 line_buf[WIDE_W];
    PixelInfo line_pi[WIDE_W];
    u8 line_bgidx[WIDE_W], line_bgattr[WIDE_W];
    int ppu_px_offset = 97;     // dot at which pixel 0 leaves the pixel pipeline (tuned on mealybug tests)
    void ppu_line_begin();
    void ppu_render_range(int x0, int x1);
    void ppu_catch_up();
    void ppu_line_end();
    void ppu_update_stat();
    void hdma_hblank();
    void service_irq();
    void run_frame(NativeResolver* native);   // runs until one video frame is complete
    // debugger (Studio): breakpoints are checked before each instruction while dbg_bp is set
    const std::vector<u8>* dbg_bp = nullptr;  // 65536 entries, 1 = stop at this PC
    bool dbg_hit = false, dbg_skip = false;   // dbg_hit: stopped mid-frame at a breakpoint
    void step_one();                          // executes exactly one instruction (or interrupt entry / halt wait)
    void note_missing(int bank, u16 pc);

    int cur_bank_for(u16 pc) const {
        return pc < 0x4000 ? cart.rom_bank0 : cart.rom_bank;
    }
    // battery saves
    std::vector<u8> save_data() const;
    void load_save_data(const std::vector<u8>& d);
    // save states
    std::vector<u8> save_state() const;
    bool load_state(const std::vector<u8>& s, bool keep_scene = false);
    // Run-ahead (less input lag): after each real frame the game is run a few frames further with the
    // same buttons, that future picture is shown, and the real state comes back before the next frame.
    // The scenery memory is not touched by the extra frames; in widescreen the future 160x144 picture is
    // pasted over the real wide picture, whose sides are moved by the scroll difference.
    struct RunAhead {
        std::vector<u8> state;
        u64 frames = 0;
        u32 mcf[2][32], mrf[2][32];
        u8 mcb[2][32], mrb[2][32];
        std::vector<u8> tiles;            // map_tile_wx / wy / frame and scene_start
        std::shared_ptr<Apu> apu;         // exact copy of the sound chip (its filters are not part of a saved state)
        bool active = false;
    };
    void ahead_keep_apu(RunAhead& ra);
    // runs n frames ahead (per_frame is called after each one, e.g. for cheats); false if nothing was done
    template <class F> bool ahead_begin(RunAhead& ra, int n, NativeResolver* native, F per_frame);
    void ahead_end(RunAhead& ra);
    void ahead_paste(int sx0, int sy0);
    u64 state_hash() const;
};

void interp_step(Machine& m);      // fallback / emulation interpreter (generated from the shared opcode table)

// fast memory access helpers -------------------------------------------------
static inline u8 rd(Machine& m, u16 a) {
    const u8* p = m.rpage[a >> 12];
    if (p) return p[a & 0xFFF];
    return m.read_slow(a);
}
static inline void wr(Machine& m, u16 a, u8 v) {
    u8* p = m.wpage[a >> 12];
    if (p) { p[a & 0xFFF] = v; return; }
    m.write_slow(a, v);
}


// Look-ahead for the widescreen memory: a private copy of the game runs a little into the future with
// the direction the camera is moving held down, and the scenery it sees is offered to the real game's
// memory as "predicted" (the player's own view always replaces it). Nothing in the real game changes.
struct WideScout {
    std::unique_ptr<Machine> s;
    int age = 0;            // look-ahead frames run since the last sync
    bool dead = true;       // the copy left the scene (door, death...): wait for the next sync
    u32 main_cuts = 0, own_cuts = 0;
    int wait = 0;
    int horizon = 900;      // frames before it is synced again with the real game
    int lead = 0;           // how many frames it is ahead of the real game
    int max_lead = 180;     // it runs two frames per real frame until it is this far ahead, then keeps pace
    int hold_x = 0, hold_y = 0;   // the direction it is holding
    void reset() { dead = true; age = 0; wait = 0; lead = 0; }
    void step(Machine& m, int frames_per_call = 2);
};
template <class F> bool Machine::ahead_begin(RunAhead& ra, int n, NativeResolver* native, F per_frame) {
    if (n <= 0 || ra.active || link) return false;
    ra.state = save_state();
    ra.frames = frames;
    ahead_keep_apu(ra);
    memcpy(ra.mcf, map_col_frame, sizeof ra.mcf); memcpy(ra.mrf, map_row_frame, sizeof ra.mrf);
    memcpy(ra.mcb, map_col_behind, sizeof ra.mcb); memcpy(ra.mrb, map_row_behind, sizeof ra.mrb);
    ra.tiles.resize(sizeof map_tile_wx + sizeof map_tile_wy + sizeof map_tile_frame + sizeof scene_start);
    {
        u8* t = ra.tiles.data();
        memcpy(t, map_tile_wx, sizeof map_tile_wx); t += sizeof map_tile_wx;
        memcpy(t, map_tile_wy, sizeof map_tile_wy); t += sizeof map_tile_wy;
        memcpy(t, map_tile_frame, sizeof map_tile_frame); t += sizeof map_tile_frame;
        memcpy(t, &scene_start, sizeof scene_start);
    }
    const int sx0 = io[0x43], sy0 = io[0x42];
    const bool wide = widescreen;
    widescreen = false;                   // the extra frames do not touch the wide picture nor the scenery memory
    for (int i = 0; i < n; i++) { run_frame(native); per_frame(); }
    widescreen = wide;
    apu->out.clear();                     // their sound is not played
    if (wide) ahead_paste(sx0, sy0);
    ra.active = true;
    return true;
}

} // namespace gb
