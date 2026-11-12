// Super Game Boy support: command packets sent through the joypad register, colour
// palettes, attribute maps, VRAM transfers (palettes, attribute files, border tiles
// and border map) and multiplayer detection (MLT_REQ).
#pragma once
#include "gb.h"

namespace gb {

struct Sgb {
    // packet reception
    bool receiving = false;
    int bit_index = 0;
    u8 prev_sel = 0x30;
    u8 packet[16];
    u8 cmd_buf[16 * 7];
    int cmd_packets = 0, cmd_expected = 0;
    // multiplayer
    int players = 1, player = 0;
    // colours
    u16 pal[4][4];             // active palettes (RGB555)
    u16 sys_pal[512][4];       // from PAL_TRN
    u8 attr[18][20];           // palette number per 8x8 cell
    u8 attr_files[45][90];     // from ATTR_TRN
    int mask = 0;              // MASK_EN: 0 none, 1 freeze, 2 black, 3 colour 0
    // VRAM transfers (performed on the next VBlank, from the screen contents)
    int pending_trn = 0;       // command code waiting for the next frame
    int pending_arg = 0;
    // border
    u8 border_tiles[256][32];  // SNES 4bpp tiles from CHR_TRN
    u16 border_map[32 * 32];
    u16 border_pal[4][16];     // palettes 4-7
    bool border_loaded = false, border_dirty = false;
    u32 border_img[256 * 224]; // ARGB, transparent (0) where the game screen shows

    void reset();
    void p1_write(Machine& m, u8 v);
    u8 joypad_id() const { return (u8)(0x0F - player); }
    void on_vblank(Machine& m);
    void colorize_line(Machine& m, int y, const u8* shades);
    void build_border();
    // 256x224 picture: border with the game screen at (48,40)
    void compose(const Machine& m, u32* out) const;
private:
    void packet_done(Machine& m);
    void execute(Machine& m);
    void set_attr_block(const u8* d);
};

u32 rgb555(u16 c);

} // namespace gb
