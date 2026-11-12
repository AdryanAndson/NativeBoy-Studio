#include "sgb.h"
#include <cstring>

namespace gb {

u32 rgb555(u16 c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return 0xFF000000u | ((u32)((r << 3) | (r >> 2)) << 16) | ((u32)((g << 3) | (g >> 2)) << 8) | (u32)((b << 3) | (b >> 2));
}
static u16 to555(u32 rgb) { return (u16)(((rgb >> 19) & 31) | (((rgb >> 11) & 31) << 5) | (((rgb >> 3) & 31) << 10)); }

void Sgb::reset() {
    memset(this, 0, sizeof(*this));
    prev_sel = 0x30;
    players = 1;
    // power-on palette (the SGB's "1-A" look)
    static const u32 def[4] = {0xF8E8C8, 0xD89048, 0xA82820, 0x301850};
    for (int p = 0; p < 4; p++) for (int i = 0; i < 4; i++) pal[p][i] = to555(def[i]);
}

void Sgb::p1_write(Machine& m, u8 v) {
    u8 sel = v & 0x30;
    if (sel == 0x00) {                       // reset pulse: start of a packet
        receiving = true; bit_index = 0; memset(packet, 0, sizeof packet);
    } else if (receiving && prev_sel == 0x30 && (sel == 0x10 || sel == 0x20)) {
        int bit = sel == 0x10 ? 1 : 0;
        if (bit_index < 128) {
            if (bit) packet[bit_index >> 3] |= (u8)(1 << (bit_index & 7));
            bit_index++;
        } else {                             // stop bit
            receiving = false;
            packet_done(m);
        }
    }
    // multiplayer: the joypad number advances when P15 goes from low to high
    if (!receiving && players > 1 && (sel & 0x20) && !(prev_sel & 0x20)) player = (player + 1) % players;
    prev_sel = sel;
}

void Sgb::packet_done(Machine& m) {
    if (cmd_packets == 0) {
        cmd_expected = packet[0] & 7;
        if (!cmd_expected) cmd_expected = 1;
    }
    if (cmd_packets < 7) memcpy(&cmd_buf[cmd_packets * 16], packet, 16);
    cmd_packets++;
    if (cmd_packets >= cmd_expected) { execute(m); cmd_packets = 0; }
}

void Sgb::set_attr_block(const u8* d) {
    u8 ctrl = d[0] & 7, pals = d[1];
    int x1 = d[2] & 31, y1 = d[3] & 31, x2 = d[4] & 31, y2 = d[5] & 31;
    int pin = pals & 3, pbo = (pals >> 2) & 3, pout = (pals >> 4) & 3;
    // "inside only" or "outside only" also colours the border with that palette
    if (ctrl == 1) { ctrl = 3; pbo = pin; }
    if (ctrl == 4) { ctrl = 6; pbo = pout; }
    for (int y = 0; y < 18; y++)
        for (int x = 0; x < 20; x++) {
            bool inside = x > x1 && x < x2 && y > y1 && y < y2;
            bool border = !inside && x >= x1 && x <= x2 && y >= y1 && y <= y2;
            bool outside = !inside && !border;
            if (inside && (ctrl & 1)) attr[y][x] = (u8)pin;
            if (border && (ctrl & 2)) attr[y][x] = (u8)pbo;
            if (outside && (ctrl & 4)) attr[y][x] = (u8)pout;
        }
}

static u16 rd16le(const u8* p) { return (u16)(p[0] | (p[1] << 8)); }

void Sgb::execute(Machine& m) {
    const u8* d = cmd_buf;
    int cmd = d[0] >> 3;
    switch (cmd) {
    case 0x00: case 0x01: case 0x02: case 0x03: {     // PAL01, PAL23, PAL03, PAL12
        static const int pa[4] = {0, 2, 0, 1}, pb[4] = {1, 3, 3, 2};
        u16 c0 = rd16le(d + 1);
        for (int p = 0; p < 4; p++) pal[p][0] = c0;
        for (int i = 0; i < 3; i++) { pal[pa[cmd]][i + 1] = rd16le(d + 3 + i * 2); pal[pb[cmd]][i + 1] = rd16le(d + 9 + i * 2); }
        break;
    }
    case 0x04: {                                       // ATTR_BLK
        int n = d[1] & 31;
        for (int k = 0; k < n && 2 + k * 6 + 6 <= 16 * 7; k++) set_attr_block(d + 2 + k * 6);
        break;
    }
    case 0x05: {                                       // ATTR_LIN
        int n = d[1];
        for (int k = 0; k < n && 2 + k < 16 * 7; k++) {
            u8 b = d[2 + k];
            int line = b & 31, p = (b >> 5) & 3;
            if (b & 0x80) { if (line < 18) for (int x = 0; x < 20; x++) attr[line][x] = (u8)p; }   // horizontal row
            else { if (line < 20) for (int y = 0; y < 18; y++) attr[y][line] = (u8)p; }            // vertical column
        }
        break;
    }
    case 0x06: {                                       // ATTR_DIV
        u8 b = d[1];
        int pr = b & 3, pl = (b >> 2) & 3, pon = (b >> 4) & 3;
        bool horiz = b & 0x40;
        int c = d[2] & 31;
        for (int y = 0; y < 18; y++)
            for (int x = 0; x < 20; x++) {
                int v = horiz ? y : x;
                attr[y][x] = (u8)(v < c ? pl : v == c ? pon : pr);
            }
        break;
    }
    case 0x07: {                                       // ATTR_CHR
        int x = d[1] % 20, y = d[2] % 18;
        int n = rd16le(d + 3);
        bool vert = d[5] & 1;
        for (int k = 0; k < n && k < 360; k++) {
            int byte = 6 + k / 4;
            if (byte >= 16 * 7) break;
            attr[y][x] = (u8)((d[byte] >> (6 - (k & 3) * 2)) & 3);
            if (vert) { if (++y >= 18) { y = 0; x = (x + 1) % 20; } }
            else { if (++x >= 20) { x = 0; y = (y + 1) % 18; } }
        }
        break;
    }
    case 0x0A: {                                       // PAL_SET
        for (int p = 0; p < 4; p++) {
            int idx = rd16le(d + 1 + p * 2) & 511;
            for (int i = 0; i < 4; i++) pal[p][i] = sys_pal[idx][i];
        }
        for (int p = 1; p < 4; p++) pal[p][0] = pal[0][0];
        if (d[9] & 0x80) {
            int f = d[9] & 0x3F;
            if (f < 45) for (int c = 0; c < 360; c++) attr[c / 20][c % 20] = (u8)((attr_files[f][c / 4] >> (6 - (c & 3) * 2)) & 3);
        }
        if (d[9] & 0x40) mask = 0;
        break;
    }
    case 0x0B: case 0x13: case 0x14: case 0x15:        // PAL_TRN, CHR_TRN, PCT_TRN, ATTR_TRN
        pending_trn = cmd; pending_arg = d[1];
        break;
    case 0x11:                                         // MLT_REQ
        players = (d[1] & 3) == 0 ? 1 : (d[1] & 3) == 1 ? 2 : 4;
        player = 0;
        break;
    case 0x16: {                                       // ATTR_SET
        int f = d[1] & 0x3F;
        if (f < 45) for (int c = 0; c < 360; c++) attr[c / 20][c % 20] = (u8)((attr_files[f][c / 4] >> (6 - (c & 3) * 2)) & 3);
        if (d[1] & 0x40) mask = 0;
        break;
    }
    case 0x17: mask = d[1] & 3; break;                 // MASK_EN
    default: break;                                    // sound / SNES program commands are not emulated
    }
    (void)m;
}

void Sgb::on_vblank(Machine& m) {
    if (!pending_trn) return;
    // the transferred 4 KB are the tiles shown on screen, in screen order
    u8 data[4096];
    u8 lcdc = m.io[0x40];
    int map = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    for (int i = 0; i < 256; i++) {
        u8 t = m.vram[0][map + (i / 20) * 32 + (i % 20)];
        int tno = (lcdc & 0x10) ? t : 256 + (s8)t;
        memcpy(&data[i * 16], &m.vram[0][tno * 16], 16);
    }
    switch (pending_trn) {
    case 0x0B: for (int i = 0; i < 512; i++) for (int k = 0; k < 4; k++) sys_pal[i][k] = rd16le(&data[(i * 4 + k) * 2]); break;
    case 0x13: { int base = (pending_arg & 1) ? 128 : 0; for (int i = 0; i < 128; i++) memcpy(border_tiles[base + i], &data[i * 32], 32); border_dirty = true; break; }
    case 0x14:
        for (int i = 0; i < 1024; i++) border_map[i] = rd16le(&data[i * 2]);
        for (int p = 0; p < 4; p++) for (int k = 0; k < 16; k++) border_pal[p][k] = rd16le(&data[0x800 + (p * 16 + k) * 2]);
        border_loaded = true; border_dirty = true;
        break;
    case 0x15: for (int f = 0; f < 45; f++) memcpy(attr_files[f], &data[f * 90], 90); break;
    }
    pending_trn = 0;
    if (border_dirty && border_loaded) build_border();
}

void Sgb::build_border() {
    border_dirty = false;
    for (int ty = 0; ty < 28; ty++)
        for (int tx = 0; tx < 32; tx++) {
            u16 e = border_map[ty * 32 + tx];
            int tile = e & 0xFF, p = (e >> 10) & 7;
            bool hf = e & 0x4000, vf = e & 0x8000;
            const u8* t = border_tiles[tile];
            for (int y = 0; y < 8; y++) {
                int r = vf ? 7 - y : y;
                for (int x = 0; x < 8; x++) {
                    int bit = hf ? x : 7 - x;
                    int idx = ((t[r * 2] >> bit) & 1) | (((t[r * 2 + 1] >> bit) & 1) << 1) |
                              (((t[16 + r * 2] >> bit) & 1) << 2) | (((t[16 + r * 2 + 1] >> bit) & 1) << 3);
                    u32 c = 0;
                    if (idx && p >= 4) c = rgb555(border_pal[p - 4][idx]);
                    border_img[(ty * 8 + y) * 256 + tx * 8 + x] = c;
                }
            }
        }
}

void Sgb::colorize_line(Machine& m, int y, const u8* shades) {
    // shades: 2-bit grey level per pixel (index x + Machine::WIDE_X0)
    if (mask == 1) return;    // freeze: keep the previous picture
    u32* line = m.line_buf;
    int cy = y / 8;
    int xs = m.widescreen ? -Machine::WIDE_X0 : 0, xe = m.widescreen ? 160 + Machine::WIDE_X0 : 160;
    u32 cols[4][4];
    for (int p = 0; p < 4; p++) for (int i = 0; i < 4; i++) cols[p][i] = rgb555(pal[p][i]);
    for (int x = xs; x < xe; x++) {
        int i = x + Machine::WIDE_X0;
        if (mask == 2) { line[i] = 0xFF000000u; continue; }
        if (mask == 3) { line[i] = cols[0][0]; continue; }
        int cx = x < 0 ? 0 : x > 159 ? 19 : x / 8;
        line[i] = cols[attr[cy][cx]][shades[i] & 3];
    }
}

void Sgb::compose(const Machine& m, u32* out) const {
    u32 bg = rgb555(pal[0][0]);
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < 256; x++) {
            u32 c = bg;
            if (x >= 48 && x < 208 && y >= 40 && y < 184) c = m.fb[(y - 40) * 160 + (x - 48)];
            if (border_loaded) { u32 b = border_img[y * 256 + x]; if (b) c = b; }
            out[y * 256 + x] = c;
        }
}

} // namespace gb
