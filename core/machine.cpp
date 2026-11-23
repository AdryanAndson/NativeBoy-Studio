#include "gb.h"
#include "apu.h"
#include "sgb.h"
#include "cpu_ops.h"
#include <algorithm>

namespace gb {

// LY as seen by the CPU: on line 153 the register already reads 0 after the first dots
static inline int ly_reg(const Machine& m) { return (m.ly == 153 && m.dot >= 4) ? 0 : m.ly; }

// ============================================================ header
static const u8 NINTENDO_LOGO_HASH_REF[] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B};

bool parse_header(const std::vector<u8>& rom, RomInfo& r) {
    r = RomInfo();
    if (rom.size() < 0x8000) { r.supported = false; r.problem = "Arquivo pequeno demais para ser uma ROM de Game Boy (menos de 32 KB)."; return false; }
    if (rom.size() > 8 * 1024 * 1024) { r.supported = false; r.problem = "Arquivo grande demais para uma ROM de Game Boy (mais de 8 MB)."; return false; }
    r.logo_ok = memcmp(&rom[0x104], NINTENDO_LOGO_HASH_REF, sizeof NINTENDO_LOGO_HASH_REF) == 0;
    r.cgb_flag = rom[0x143];
    int tlen = (r.cgb_flag & 0x80) ? 15 : 16;
    for (int i = 0; i < tlen; i++) {
        char ch = (char)rom[0x134 + i];
        if (ch == 0) break;
        r.title += (ch >= 32 && ch < 127) ? ch : '?';
    }
    while (!r.title.empty() && r.title.back() == ' ') r.title.pop_back();
    r.cgb_supported = (r.cgb_flag & 0x80) != 0;
    r.cgb_only = r.cgb_flag == 0xC0;
    r.sgb_flag = rom[0x146];
    r.cart_type = rom[0x147];
    r.rom_size_code = rom[0x148];
    r.ram_size_code = rom[0x149];
    r.version = rom[0x14C];
    r.header_checksum = rom[0x14D];
    u8 x = 0;
    for (int i = 0x134; i <= 0x14C; i++) x = (u8)(x - rom[i] - 1);
    r.header_checksum_calc = x;
    r.global_checksum = (u16)((rom[0x14E] << 8) | rom[0x14F]);
    u16 g = 0;
    for (size_t i = 0; i < rom.size(); i++) if (i != 0x14E && i != 0x14F) g = (u16)(g + rom[i]);
    r.global_checksum_calc = g;
    switch (r.cart_type) {
    case 0x00: r.mbc = 0; r.mbc_name = "ROM"; break;
    case 0x08: r.mbc = 0; r.has_ram = true; r.mbc_name = "ROM+RAM"; break;
    case 0x09: r.mbc = 0; r.has_ram = r.has_battery = true; r.mbc_name = "ROM+RAM+BATERIA"; break;
    case 0x01: r.mbc = 1; r.mbc_name = "MBC1"; break;
    case 0x02: r.mbc = 1; r.has_ram = true; r.mbc_name = "MBC1+RAM"; break;
    case 0x03: r.mbc = 1; r.has_ram = r.has_battery = true; r.mbc_name = "MBC1+RAM+BATERIA"; break;
    case 0x05: r.mbc = 2; r.has_ram = true; r.mbc_name = "MBC2"; break;
    case 0x06: r.mbc = 2; r.has_ram = r.has_battery = true; r.mbc_name = "MBC2+BATERIA"; break;
    case 0x0F: r.mbc = 3; r.has_rtc = r.has_battery = true; r.mbc_name = "MBC3+RTC+BATERIA"; break;
    case 0x10: r.mbc = 3; r.has_rtc = r.has_ram = r.has_battery = true; r.mbc_name = "MBC3+RTC+RAM+BATERIA"; break;
    case 0x11: r.mbc = 3; r.mbc_name = "MBC3"; break;
    case 0x12: r.mbc = 3; r.has_ram = true; r.mbc_name = "MBC3+RAM"; break;
    case 0x13: r.mbc = 3; r.has_ram = r.has_battery = true; r.mbc_name = "MBC3+RAM+BATERIA"; break;
    case 0x19: r.mbc = 5; r.mbc_name = "MBC5"; break;
    case 0x1A: r.mbc = 5; r.has_ram = true; r.mbc_name = "MBC5+RAM"; break;
    case 0x1B: r.mbc = 5; r.has_ram = r.has_battery = true; r.mbc_name = "MBC5+RAM+BATERIA"; break;
    case 0x1C: r.mbc = 5; r.has_rumble = true; r.mbc_name = "MBC5+VIBRAÇÃO"; break;
    case 0x1D: r.mbc = 5; r.has_rumble = r.has_ram = true; r.mbc_name = "MBC5+VIBRAÇÃO+RAM"; break;
    case 0x1E: r.mbc = 5; r.has_rumble = r.has_ram = r.has_battery = true; r.mbc_name = "MBC5+VIBRAÇÃO+RAM+BATERIA"; break;
    case 0x22: r.mbc = 7; r.has_ram = r.has_battery = true; r.mbc_name = "MBC7 (ACELERÔMETRO+EEPROM)"; break;
    case 0xFE: r.mbc = 0xFE; r.has_ram = r.has_battery = r.has_rtc = true; r.mbc_name = "HuC3 (RELÓGIO+INFRAVERMELHO)"; break;
    case 0xFF: r.mbc = 0xFF; r.has_ram = r.has_battery = true; r.mbc_name = "HuC1 (INFRAVERMELHO)"; break;
    default: {
        char buf[160];
        snprintf(buf, sizeof buf, "Tipo de cartucho 0x%02X ainda não é suportado (ex.: MBC6, Game Boy Camera, TAMA5 ou MMM01).", r.cart_type);
        r.supported = false; r.problem = buf; r.mbc_name = "DESCONHECIDO";
    }
    }
    r.rom_banks = (int)((rom.size() + 0x3FFF) / 0x4000);
    if (r.rom_banks < 2) r.rom_banks = 2;
    static const int RAMS[6] = {0, 2048, 8192, 32768, 131072, 65536};
    r.ram_size = r.ram_size_code < 6 ? RAMS[r.ram_size_code] : 0;
    if (r.mbc == 2) r.ram_size = 512;
    if (r.mbc == 7) r.ram_size = 256;     // 93LC56 EEPROM
    if (r.has_ram && r.ram_size == 0 && r.mbc != 2) r.ram_size = 8192;
    return r.supported;
}

// ============================================================ machine
Machine::Machine() {
    apu = new Apu();
    memset(vram, 0, sizeof vram); memset(wram, 0, sizeof wram); memset(oam, 0, sizeof oam);
    memset(io, 0, sizeof io); memset(hram, 0, sizeof hram); memset(fb, 0xFF, sizeof fb);
    memset(pinfo, 0xFF, sizeof pinfo); memset(fbw, 0xFF, sizeof fbw); memset(bgpal, 0xFF, sizeof bgpal); memset(objpal, 0, sizeof objpal);
    memset(tile_gen, 0, sizeof tile_gen);
    for (int i = 0; i < 16; i++) { rpage[i] = nullptr; wpage[i] = nullptr; }
}
Machine::~Machine() { delete apu; delete sgb; }

bool Machine::load_rom(const std::vector<u8>& rom, std::string& err, bool force_dmg) {
    RomInfo info;
    if (!parse_header(rom, info)) { err = info.problem; return false; }
    cart = Cart();
    cart.info = info;
    cart.rom = rom;
    cart.rom.resize((size_t)info.rom_banks * 0x4000, 0xFF);
    cart.ram.assign(info.ram_size ? info.ram_size : 0, 0xFF);
    cgb = info.cgb_supported && !(force_dmg && !info.cgb_only);
    delete sgb; sgb = nullptr;
    if (sgb_mode && !cgb && info.sgb_flag == 0x03) sgb = new Sgb();
    reset();
    return true;
}

void Machine::reset() {
    double_speed = false;
    memset(vram, 0, sizeof vram); memset(wram, 0, sizeof wram); memset(oam, 0, sizeof oam);
    memset(io, 0, sizeof io); memset(hram, 0, sizeof hram);
    memset(bgpal, 0xFF, sizeof bgpal); memset(objpal, 0, sizeof objpal);
    pal_dirty = true;
    vram_bank = 0; wram_bank = 1;
    cart.rom_bank = 1; cart.rom_bank0 = 0; cart.ram_bank = 0; cart.ram_enabled = false; cart.mbc1_mode = 0;
    cart.bank_lo = 1; cart.bank_hi = 0;
    if (cgb) { a = 0x11; f = 0x80; b = 0x00; c = 0x00; d = 0xFF; e = 0x56; h = 0x00; l = 0x0D; }
    else if (sgb) { a = 0x01; f = 0x00; b = 0x00; c = 0x14; d = 0x00; e = 0x00; h = 0xC0; l = 0x60; sgb->reset(); }
    else { a = 0x01; f = 0xB0; b = 0x00; c = 0x13; d = 0x00; e = 0xD8; h = 0x01; l = 0x4D; }
    sp = 0xFFFE; pc = 0x0100;
    ime = false; ei_delay = 0; halted = false; ev = false; locked = false; halt_bug = false;
    ie = 0; iflag = 0x01;
    div = 0xABCC;
    dot = 0; ly = 0; mode = 2; stat_line = false; win_line = 0; lcd_off_dots = 0;
    hdma_active = false; hdma_len = 0;
    apu->reset();
    static const u8 apu_init[] = {0x80, 0xBF, 0xF3, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF, 0xFF, 0x00, 0x00, 0xBF, 0x77, 0xF3, 0xF1};
    apu->write(0xFF26, 0x80);
    for (int i = 0; i < 0x16; i++) apu->write((u16)(0xFF10 + i), apu_init[i]);
    // silence channels that the boot ROM leaves configured
    apu->sq[0].en = false;
    io[0x00] = 0x30; io[0x02] = 0x7E; io[0x07] = 0xF8;
    io[0x40] = 0x91; io[0x41] = 0x85; io[0x47] = 0xFC; io[0x48] = 0xFF; io[0x49] = 0xFF;
    cycles = 0; frames = 0; synced = 0; next_event = 0;
    serial_out.clear();
    fb_stats = FallbackStats();
    missing_seen.assign(cart.rom.size(), 0);
    memset(fb, 0xFF, sizeof fb);
    map_pages();
}

void Machine::map_pages() {
    size_t nb = cart.rom.size() / 0x4000;
    int b0 = (int)(cart.rom_bank0 % nb), b1 = (int)(cart.rom_bank % nb);
    cart.rom_bank0 = b0; cart.rom_bank = b1;
    for (int i = 0; i < 4; i++) rpage[i] = &cart.rom[(size_t)b0 * 0x4000 + i * 0x1000];
    for (int i = 0; i < 4; i++) rpage[4 + i] = &cart.rom[(size_t)b1 * 0x4000 + i * 0x1000];
    for (int i = 0; i < 8; i++) wpage[i] = nullptr;
    rpage[8] = vram[vram_bank]; rpage[9] = vram[vram_bank] + 0x1000;
    wpage[8] = wpage[9] = nullptr;
    rpage[0xA] = rpage[0xB] = nullptr; wpage[0xA] = wpage[0xB] = nullptr;
    if (cart.ram_enabled && cart.info.mbc != 2 && cart.info.mbc != 7 && !cart.ram.empty() &&
        !(cart.info.mbc == 0xFE && cart.huc_mode != 0x0A)) {
        bool rtc_sel = cart.info.mbc == 3 && cart.ram_bank >= 8;
        if (!rtc_sel) {
            size_t nrb = std::max<size_t>(1, cart.ram.size() / 0x2000);
            size_t base = (size_t)(cart.ram_bank % nrb) * 0x2000;
            if (cart.ram.size() >= 0x2000) {
                rpage[0xA] = wpage[0xA] = &cart.ram[base];
                rpage[0xB] = wpage[0xB] = &cart.ram[base + 0x1000];
            }
        }
    }
    rpage[0xC] = wpage[0xC] = wram[0];
    rpage[0xD] = wpage[0xD] = wram[wram_bank];
    rpage[0xE] = wpage[0xE] = wram[0];
    rpage[0xF] = wpage[0xF] = nullptr;
}

void Machine::mbc_write(u16 addr, u8 v) {
    Cart& k = cart;
    switch (k.info.mbc) {
    case 0: break;
    case 1:
        if (addr < 0x2000) k.ram_enabled = (v & 0xF) == 0xA;
        else if (addr < 0x4000) { k.bank_lo = v & 0x1F; if (!k.bank_lo) k.bank_lo = 1; }
        else if (addr < 0x6000) k.bank_hi = v & 3;
        else k.mbc1_mode = v & 1;
        k.rom_bank = (k.bank_hi << 5) | k.bank_lo;
        k.rom_bank0 = k.mbc1_mode ? (k.bank_hi << 5) : 0;
        k.ram_bank = k.mbc1_mode ? k.bank_hi : 0;
        break;
    case 2:
        if (addr < 0x4000) {
            if (addr & 0x100) { k.rom_bank = v & 0xF; if (!k.rom_bank) k.rom_bank = 1; }
            else k.ram_enabled = (v & 0xF) == 0xA;
        }
        break;
    case 3:
        if (addr < 0x2000) k.ram_enabled = (v & 0xF) == 0xA;
        else if (addr < 0x4000) { k.rom_bank = v & 0x7F; if (!k.rom_bank) k.rom_bank = 1; }
        else if (addr < 0x6000) k.ram_bank = v & 0x0F;
        else {
            if (k.rtc_latch_prev == 0 && v == 1) memcpy(k.rtc_latched, k.rtc, 5);
            k.rtc_latch_prev = v;
        }
        break;
    case 5:
        if (addr < 0x2000) k.ram_enabled = (v & 0xF) == 0xA;
        else if (addr < 0x3000) k.rom_bank = (k.rom_bank & 0x100) | v;
        else if (addr < 0x4000) k.rom_bank = (k.rom_bank & 0xFF) | ((v & 1) << 8);
        else if (addr < 0x6000) { k.ram_bank = k.info.has_rumble ? (v & 0x7) : (v & 0xF); if (k.info.has_rumble) k.rumble = (v & 8) != 0; }
        break;
    case 7:     // MBC7: two enable registers, ROM banking like MBC5 (7 bits)
        if (addr < 0x2000) k.ram_enabled = v == 0x0A;
        else if (addr < 0x4000) k.rom_bank = v & 0x7F;
        else if (addr < 0x6000) k.mbc7_en2 = v == 0x40;
        break;
    case 0xFF:  // HuC1: like MBC1 without the upper bits; 0x0E selects the infrared port
        if (addr < 0x2000) { k.huc_mode = v; k.ram_enabled = v != 0x0E; }
        else if (addr < 0x4000) { k.rom_bank = v & 0x3F; if (!k.rom_bank) k.rom_bank = 1; }
        else if (addr < 0x6000) k.ram_bank = v & 3;
        break;
    case 0xFE:  // HuC3: mode register selects RAM / RTC command / RTC response / semaphore / infrared
        if (addr < 0x2000) { k.huc_mode = v & 0x0F; k.ram_enabled = true; }
        else if (addr < 0x4000) { k.rom_bank = v & 0x7F; if (!k.rom_bank) k.rom_bank = 1; }
        else if (addr < 0x6000) k.ram_bank = v & 3;
        break;
    }
    map_pages();
    ev = true;    // code mapping may have changed: leave native code
}

u8 Machine::read_slow(u16 addr) {
    if (addr < 0x8000) return rpage[addr >> 12][addr & 0xFFF];
    if (addr < 0xA000) return vram[vram_bank][addr - 0x8000];
    if (addr < 0xC000) {
        if (!cart.ram_enabled) return 0xFF;
        if (cart.info.mbc == 7) {
            Cart& k = cart;
            if (!k.mbc7_en2 || addr >= 0xB000) return 0xFF;
            switch ((addr >> 4) & 0xF) {
            case 2: return (u8)k.accel_x; case 3: return (u8)(k.accel_x >> 8);
            case 4: return (u8)k.accel_y; case 5: return (u8)(k.accel_y >> 8);
            case 6: return 0x00;
            case 8: return (u8)((k.ee_cs ? 0x80 : 0) | (k.ee_clk ? 0x40 : 0) | (k.ee_di ? 2 : 0) | (k.ee_do ? 1 : 0));
            default: return 0xFF;
            }
        }
        if (cart.info.mbc == 0xFF && cart.huc_mode == 0x0E) return 0xC0;      // HuC1 infrared: no light seen
        if (cart.info.mbc == 0xFE && cart.huc_mode != 0x0A && cart.huc_mode != 0x00) {
            if (cart.huc_mode == 0x0C) return cart.huc3_resp;
            if (cart.huc_mode == 0x0D) return 0x01;                            // RTC ready
            if (cart.huc_mode == 0x0E) return 0xC0;
            return 0xFF;
        }
        if (cart.info.mbc == 2) return (u8)(0xF0 | cart.ram[(addr - 0xA000) & 0x1FF]);
        if (cart.info.mbc == 3 && cart.ram_bank >= 8) return cart.ram_bank <= 0xC ? cart.rtc_latched[cart.ram_bank - 8] : 0xFF;
        if (cart.ram.empty()) return 0xFF;
        size_t nrb = std::max<size_t>(1, cart.ram.size() / 0x2000);
        size_t off = (size_t)(cart.ram_bank % nrb) * 0x2000 + (addr - 0xA000);
        return off < cart.ram.size() ? cart.ram[off] : 0xFF;
    }
    if (addr < 0xD000) return wram[0][addr - 0xC000];
    if (addr < 0xE000) return wram[wram_bank][addr - 0xD000];
    if (addr < 0xF000) return wram[0][addr - 0xE000];
    if (addr < 0xFE00) return wram[wram_bank][addr - 0xF000];
    if (addr < 0xFEA0) return oam[addr - 0xFE00];
    if (addr < 0xFF00) return 0x00;
    if (addr < 0xFF80) return io_read(addr);
    if (addr < 0xFFFF) return hram[addr - 0xFF80];
    return ie;
}

void Machine::write_slow(u16 addr, u8 v) {
    if (addr < 0x8000) { mbc_write(addr, v); return; }
    if (addr < 0xA000) {
        vram[vram_bank][addr - 0x8000] = v;
        if (addr < 0x9800) {
            const int ti = vram_bank * 384 + ((addr - 0x8000) >> 4);
            tile_gen[ti] = ++vram_write_gen;
            if (tile_wframe[ti] != (u32)frames + 1) { tile_wframe[ti] = (u32)frames + 1; if (tile_anim[ti] < 250) tile_anim[ti]++; }
        }
        else {
            const int mp = addr >= 0x9C00, col = addr & 31;
            map_col_frame[mp][col] = (u32)frames + 1; frame_map_writes++;
            const int row = (addr >> 5) & 31, rrel = (row * 8 - io[0x42]) & 255;
            map_row_frame[mp][row] = (u32)frames + 1;
            map_row_behind[mp][row] = wide_dir_y > 0 ? rrel >= 200 : wide_dir_y < 0 ? (rrel >= 144 && rrel < 200) : 0;
            // widescreen: a column written while just behind the camera is being cleared/reused, so when the
            // map wraps around and it shows up ahead of the camera it is not real scenery yet
            int rel = (col * 8 - io[0x43]) & 255;
            map_col_behind[mp][col] = wide_dir_x > 0 ? rel >= 256 - WIDE_X0 : wide_dir_x < 0 ? (rel >= 160 && rel < 160 + WIDE_X0) : 0;
            // the world position this tile is written for: where it appears in the wide picture right now
            const int mi = addr & 0x3FF;
            map_tile_wx[mp][mi] = cam_x() + map_rel_x(rel);
            map_tile_wy[mp][mi] = cam_y() + map_rel_y(rrel);
            map_tile_frame[mp][mi] = (u32)frames + 1;
            // written just behind the camera: the game is clearing or reusing that part, it is not scenery
            if (map_col_behind[mp][col] || map_row_behind[mp][row]) map_tile_frame[mp][mi] = 0;
        }
        return;
    }
    if (addr < 0xC000) {
        if (!cart.ram_enabled) return;
        if (cart.info.mbc == 7) { mbc7_write(addr, v); return; }
        if (cart.info.mbc == 0xFF && cart.huc_mode == 0x0E) return;
        if (cart.info.mbc == 0xFE && cart.huc_mode != 0x0A) { if (cart.huc_mode == 0x0B) huc3_command(v); return; }
        cart.ram_dirty = true;
        if (cart.info.mbc == 2) { cart.ram[(addr - 0xA000) & 0x1FF] = v & 0xF; return; }
        if (cart.info.mbc == 3 && cart.ram_bank >= 8) { if (cart.ram_bank <= 0xC) cart.rtc[cart.ram_bank - 8] = v; return; }
        if (cart.ram.empty()) return;
        size_t nrb = std::max<size_t>(1, cart.ram.size() / 0x2000);
        size_t off = (size_t)(cart.ram_bank % nrb) * 0x2000 + (addr - 0xA000);
        if (off < cart.ram.size()) cart.ram[off] = v;
        return;
    }
    if (addr < 0xD000) { wram[0][addr - 0xC000] = v; return; }
    if (addr < 0xE000) { wram[wram_bank][addr - 0xD000] = v; return; }
    if (addr < 0xF000) { wram[0][addr - 0xE000] = v; return; }
    if (addr < 0xFE00) { wram[wram_bank][addr - 0xF000] = v; return; }
    if (addr < 0xFEA0) { oam[addr - 0xFE00] = v; return; }
    if (addr < 0xFF00) return;
    if (addr < 0xFF80) { io_write(addr, v); return; }
    if (addr < 0xFFFF) { hram[addr - 0xFF80] = v; return; }
    ie = v;
    force_event();
}

u8 Machine::io_read(u16 addr) {
    sync();
    u8 r = (u8)(addr & 0x7F);
    switch (r) {
    case 0x00: {
        u8 sel = io[0] & 0x30, lo = 0x0F;
        if (sgb && sgb->players > 1 && sel == 0x30) return (u8)(0xC0 | sel | sgb->joypad_id());
        bool p1 = !sgb || sgb->player == 0;      // only player 1 has a controller
        if (p1 && !(sel & 0x10)) lo &= (u8)~(buttons & 0x0F);
        if (p1 && !(sel & 0x20)) lo &= (u8)~(buttons >> 4);
        return (u8)(0xC0 | sel | lo);
    }
    case 0x01: return io[1];
    case 0x02: return (u8)(io[2] | (cgb ? 0x7C : 0x7E));
    case 0x04: return (u8)(div >> 8);
    case 0x05: case 0x06: return io[r];
    case 0x07: return (u8)(io[7] | 0xF8);
    case 0x0F: return (u8)(iflag | 0xE0);
    case 0x40: return io[0x40];
    case 0x41: {
        u8 s = (u8)(0x80 | (io[0x41] & 0x78));
        if (io[0x40] & 0x80) { s |= (u8)mode; if (ly_reg(*this) == io[0x45]) s |= 4; }
        return s;
    }
    case 0x44: return (io[0x40] & 0x80) ? (u8)ly_reg(*this) : 0;
    case 0x42: case 0x43: case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: return io[r];
    case 0x4D: return cgb ? (u8)(io[0x4D] | 0x7E) : 0xFF;
    case 0x4F: return cgb ? (u8)(0xFE | vram_bank) : 0xFF;
    case 0x55: return cgb ? (u8)(hdma_active ? ((hdma_len / 16 - 1) & 0x7F) : 0xFF) : 0xFF;
    case 0x68: case 0x6A: return cgb ? (u8)(io[r] | 0x40) : 0xFF;
    case 0x69: return cgb ? bgpal[io[0x68] & 0x3F] : 0xFF;
    case 0x6B: return cgb ? objpal[io[0x6A] & 0x3F] : 0xFF;
    case 0x70: return cgb ? (u8)(0xF8 | wram_bank) : 0xFF;
    }
    if (r >= 0x10 && r < 0x40) return apu->read(addr);
    return 0xFF;
}

void Machine::io_write(u16 addr, u8 v) {
    sync();
    force_event();
    u8 r = (u8)(addr & 0x7F);
    if (r >= 0x10 && r < 0x40) { apu->write(addr, v); return; }
    switch (r) {
    case 0x00: io[0] = v & 0x30; if (sgb) sgb->p1_write(*this, v); return;
    case 0x01: io[1] = v; return;
    case 0x02:
        io[2] = v;
        if ((v & 0x81) == 0x81) {     // internal clock transfer: completes at once (with the partner's byte, if linked)
            serial_out.push_back((char)io[1]);
            io[1] = link ? link->exchange_master(*this, io[1]) : 0xFF;
            io[2] &= 0x7F; iflag |= IRQ_SERIAL;
            force_event();
        }
        return;
    case 0x04: {
        static const u16 bits[4] = {0x200, 0x8, 0x20, 0x80};
        if ((io[7] & 4) && (div & bits[io[7] & 3])) { if (++io[5] == 0) { io[5] = io[6]; iflag |= IRQ_TIMER; } }
        div = 0; return;
    }
    case 0x05: case 0x06: io[r] = v; return;
    case 0x07: io[7] = v & 7; return;
    case 0x0F: iflag = v & 0x1F; return;
    case 0x40: {
        ppu_catch_up();
        bool was_on = io[0x40] & 0x80;
        io[0x40] = v;
        bool now_on = v & 0x80;
        if (was_on && !now_on) { ly = 0; dot = 0; mode = 0; lcd_off_dots = 0; lcd_was_off = true; lcd_off_frame = (u32)frames + 1; }   // (widescreen: a new scene)
        if (!was_on && now_on) { ly = 0; dot = 0; mode = 2; win_line = 0; lcd_off_dots = 0; ppu_update_stat(); }
        return;
    }
    case 0x41: io[0x41] = v & 0x78; ppu_update_stat(); return;
    case 0x44: return;
    case 0x45: io[0x45] = v; ppu_update_stat(); return;
    case 0x46: {
        io[0x46] = v;
        u16 src = (u16)(v << 8);
        for (int i = 0; i < 0xA0; i++) oam[i] = rd(*this, (u16)(src + i));
        return;
    }
    case 0x42: case 0x43: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B: ppu_catch_up(); io[r] = v; return;
    case 0x4D: if (cgb) io[0x4D] = (u8)((io[0x4D] & 0x80) | (v & 1)); return;
    case 0x4F: if (cgb) { vram_bank = v & 1; map_pages(); } return;
    case 0x51: io[0x51] = v; return;
    case 0x52: io[0x52] = v & 0xF0; return;
    case 0x53: io[0x53] = v & 0x1F; return;
    case 0x54: io[0x54] = v & 0xF0; return;
    case 0x55: {
        if (!cgb) return;
        hdma_src = (u16)((io[0x51] << 8) | io[0x52]);
        hdma_dst = (u16)(0x8000 | (io[0x53] << 8) | io[0x54]);
        if (hdma_active && !(v & 0x80)) { hdma_active = false; return; }
        hdma_len = ((v & 0x7F) + 1) * 16;
        if (v & 0x80) { hdma_active = true; }
        else {
            for (int i = 0; i < hdma_len; i++) write_slow((u16)(0x8000 | ((hdma_dst + i) & 0x1FFF)), rd(*this, (u16)(hdma_src + i)));
            hdma_src = (u16)(hdma_src + hdma_len); hdma_dst = (u16)(hdma_dst + hdma_len);
            hdma_len = 0;
        }
        return;
    }
    case 0x68: case 0x6A: io[r] = v & 0xBF; return;
    case 0x69: case 0x6B: {
        if (!cgb) return;
        ppu_catch_up();
        u8 idxr = (u8)(r - 1);
        u8 ix = io[idxr] & 0x3F;
        if (r == 0x69) bgpal[ix] = v; else objpal[ix] = v;
        pal_dirty = true;
        if (io[idxr] & 0x80) io[idxr] = (u8)(0x80 | ((ix + 1) & 0x3F));
        return;
    }
    case 0x70: if (cgb) { wram_bank = v & 7; if (!wram_bank) wram_bank = 1; map_pages(); } return;
    }
    io[r] = v;
}

void Machine::hdma_hblank() {
    if (!hdma_active) return;
    for (int i = 0; i < 16; i++) write_slow((u16)(0x8000 | ((hdma_dst + i) & 0x1FFF)), rd(*this, (u16)(hdma_src + i)));
    hdma_src = (u16)(hdma_src + 16); hdma_dst = (u16)(hdma_dst + 16);
    hdma_len -= 16;
    if (hdma_len <= 0) { hdma_active = false; hdma_len = 0; }
}

// ============================================================ timing
void Machine::timer_tick(int t) {
    static const u32 periods[4] = {1024, 16, 64, 256};
    u32 old = div, nw = old + (u32)t;
    div = (u16)nw;
    if (!(io[7] & 4)) return;
    u32 P = periods[io[7] & 3];
    u32 edges = nw / P - old / P;          // falling edges of the selected DIV bit
    while (edges--) {
        if (++io[5] == 0) { io[5] = io[6]; iflag |= IRQ_TIMER; }
    }
}

void Machine::sync() {
    u64 d = cycles - synced;
    if (!d) return;
    synced = cycles;
    hw_syncs++;
    int t = (int)d;
    timer_tick(t);
    int pt = double_speed ? t / 2 : t;
    ppu_tick(pt);
    apu->tick(pt);
    if (cart.info.has_rtc && !(cart.rtc[4] & 0x40)) {
        cart.rtc_cycles += pt;
        while (cart.rtc_cycles >= 4194304) {
            cart.rtc_cycles -= 4194304;
            u8* r = cart.rtc;
            if (++r[0] >= 60) { r[0] = 0; if (++r[1] >= 60) { r[1] = 0; if (++r[2] >= 24) { r[2] = 0;
                int days = r[3] | ((r[4] & 1) << 8); days++;
                if (days > 511) { days = 0; r[4] |= 0x80; }
                r[3] = days & 0xFF; r[4] = (u8)((r[4] & 0xFE) | ((days >> 8) & 1)); } } }
        }
    }
}

void Machine::schedule() {
    // next PPU transition (in PPU dots), converted to CPU cycles
    int dots;
    if (!(io[0x40] & 0x80)) dots = 70224 - lcd_off_dots;
    else if (ly < 144) dots = (mode == 2 ? 80 : mode == 3 ? 252 : 456) - dot;
    else if (ly == 153 && dot < 4) dots = 4 - dot;
    else dots = 456 - dot;
    if (dots < 1) dots = 1;
    u64 ev_at = cycles + (u64)dots * (double_speed ? 2 : 1);
    // next timer overflow (only overflows are visible without reading TIMA, and reads sync)
    if (io[7] & 4) {
        static const u32 periods[4] = {1024, 16, 64, 256};
        u32 P = periods[io[7] & 3];
        u64 first = P - (div % P);
        u64 t_over = cycles + first + (u64)(255 - io[5]) * P;
        if (t_over < ev_at) ev_at = t_over;
    }
    // keep the audio and RTC reasonably fresh even in long quiet stretches
    if (ev_at > cycles + 8192) ev_at = cycles + 8192;
    next_event = ev_at;
}

void Machine::slow_tick() {
    sync();
    if (ei_delay && --ei_delay == 0) ime = true;
    if (frame_done || (ime && (ie & iflag & 0x1F))) ev = true;
    schedule();
    if (link) {       // a partner driving the clock is only noticed when polled
        if (cycles >= link_poll_at) { link_poll_at = cycles + 2048; link->poll(*this); }
        if (next_event > link_poll_at) next_event = link_poll_at;
    }
    if (ei_delay) next_event = cycles + 1;   // EI takes effect after the next instruction
}

u8 Machine::serial_slave_exchange(u8 in) {
    if ((io[2] & 0x81) != 0x80) return 0xFF;      // not waiting for an external clock: line stays high
    u8 out = io[1];
    io[1] = in;
    io[2] &= 0x7F;
    iflag |= IRQ_SERIAL;
    force_event();
    return out;
}

// --------------------------------------------------------------------- MBC7
void Machine::mbc7_write(u16 addr, u8 v) {
    Cart& k = cart;
    if (!k.mbc7_en2 || addr >= 0xB000) return;
    switch ((addr >> 4) & 0xF) {
    case 0: if (v == 0x55) { k.accel_x = k.accel_y = 0x8000; k.accel_ready = true; } break;
    case 1:
        if (v == 0xAA && k.accel_ready) {
            auto conv = [](float t) { t = t < -1.5f ? -1.5f : t > 1.5f ? 1.5f : t; return (u16)(0x81D0 + (int)(t * 0x70)); };
            k.accel_x = conv(tilt_x); k.accel_y = conv(tilt_y); k.accel_ready = false;
        }
        break;
    case 8: {   // 93LC56 in x16 organisation: CS bit7, CLK bit6, DI bit1, DO bit0
        u8 cs = (v >> 7) & 1, clk = (v >> 6) & 1, di = (v >> 1) & 1;
        if (!cs) { k.ee_state = 0; k.ee_bits = 0; }
        else if (clk && !k.ee_clk) {         // rising edge
            switch (k.ee_state) {
            case 0: if (di) { k.ee_state = 1; k.ee_sr = 0; k.ee_bits = 0; } break;   // start bit
            case 1:
                k.ee_sr = (u16)((k.ee_sr << 1) | di);
                if (++k.ee_bits == 10) {
                    int op = (k.ee_sr >> 8) & 3, ad = k.ee_sr & 0xFF;
                    k.ee_addr = ad & 0x7F; k.ee_bits = 0; k.ee_sr = 0;
                    switch (op) {
                    case 2: k.ee_out = (u16)((k.ram[k.ee_addr * 2] << 8) | k.ram[k.ee_addr * 2 + 1]); k.ee_do = 0; k.ee_state = 2; break;   // READ
                    case 1: k.ee_state = 3; break;                                           // WRITE
                    case 3: if (k.ee_we) { k.ram[k.ee_addr * 2] = k.ram[k.ee_addr * 2 + 1] = 0xFF; cart.ram_dirty = true; } k.ee_do = 1; k.ee_state = 5; break;   // ERASE
                    default:
                        switch ((ad >> 6) & 3) {
                        case 0: k.ee_we = false; k.ee_state = 5; break;                       // EWDS
                        case 1: k.ee_state = 4; break;                                        // WRAL
                        case 2: if (k.ee_we) { for (int i = 0; i < 256; i++) k.ram[i] = 0xFF; cart.ram_dirty = true; } k.ee_do = 1; k.ee_state = 5; break;   // ERAL
                        case 3: k.ee_we = true; k.ee_state = 5; break;                        // EWEN
                        }
                    }
                }
                break;
            case 2:     // shifting out the word, MSB first
                k.ee_do = (k.ee_out >> 15) & 1; k.ee_out = (u16)(k.ee_out << 1);
                if (++k.ee_bits == 16) { k.ee_state = 2; k.ee_bits = 0; k.ee_addr = (k.ee_addr + 1) & 0x7F;   // sequential read
                    k.ee_out = (u16)((k.ram[k.ee_addr * 2] << 8) | k.ram[k.ee_addr * 2 + 1]); }
                break;
            case 3: case 4:
                k.ee_sr = (u16)((k.ee_sr << 1) | di);
                if (++k.ee_bits == 16) {
                    if (k.ee_we) {
                        if (k.ee_state == 3) { k.ram[k.ee_addr * 2] = (u8)(k.ee_sr >> 8); k.ram[k.ee_addr * 2 + 1] = (u8)k.ee_sr; }
                        else for (int i = 0; i < 128; i++) { k.ram[i * 2] = (u8)(k.ee_sr >> 8); k.ram[i * 2 + 1] = (u8)k.ee_sr; }
                        cart.ram_dirty = true;
                    }
                    k.ee_do = 1; k.ee_state = 5;      // ready
                }
                break;
            default: break;
            }
        }
        k.ee_cs = cs; k.ee_clk = clk; k.ee_di = di;
        break;
    }
    default: break;
    }
}

// --------------------------------------------------------------------- HuC3 clock (basic)
void Machine::huc3_command(u8 v) {
    Cart& k = cart;
    // advance the minute counter with emulated time
    const u64 per_min = 60ull * 4194304ull;
    if (k.huc3_base == 0) k.huc3_base = cycles;
    while (cycles - k.huc3_base >= per_min) {
        k.huc3_base += per_min;
        int mins = k.huc3_mem[0] | (k.huc3_mem[1] << 4) | (k.huc3_mem[2] << 8);
        int days = k.huc3_mem[3] | (k.huc3_mem[4] << 4) | (k.huc3_mem[5] << 8);
        if (++mins >= 1440) { mins = 0; days = (days + 1) & 0xFFF; }
        for (int i = 0; i < 3; i++) { k.huc3_mem[i] = (u8)((mins >> (i * 4)) & 0xF); k.huc3_mem[3 + i] = (u8)((days >> (i * 4)) & 0xF); }
    }
    u8 cmd = v >> 4, arg = v & 0xF;
    switch (cmd) {
    case 0x1: k.huc3_resp = (u8)(0x10 | (k.huc3_mem[k.huc3_addr] & 0xF)); k.huc3_addr++; break;   // read, advance
    case 0x3: k.huc3_mem[k.huc3_addr++] = arg; k.huc3_resp = 0x30; break;                         // write, advance
    case 0x4: k.huc3_addr = (u8)((k.huc3_addr & 0xF0) | arg); k.huc3_resp = 0x40; break;
    case 0x5: k.huc3_addr = (u8)((k.huc3_addr & 0x0F) | (arg << 4)); k.huc3_resp = 0x50; break;
    case 0x6: k.huc3_resp = (u8)(0x60 | (arg == 0xE ? 1 : 0)); break;                            // extended: report "ok"
    default: k.huc3_resp = (u8)(v & 0xF0); break;
    }
}

void Machine::service_irq() {
    u8 pend = ie & iflag & 0x1F;
    if (!pend) return;
    ime = false; halted = false;
    int bit = __builtin_ctz(pend);
    iflag &= (u8)~(1 << bit);
    push16(*this, pc);
    pc = (u16)(0x40 + bit * 8);
    tick(20);
}

void Machine::note_missing(int bank, u16 pcv) {
    size_t off = (size_t)bank * 0x4000 + (pcv & 0x3FFF);
    if (off < missing_seen.size() && !missing_seen[off] && fb_stats.missing.size() < 100000) {
        missing_seen[off] = 1;
        fb_stats.missing.push_back(((u32)bank << 16) | pcv);
    }
}

void Machine::run_frame(NativeResolver* native) {
    frame_done = false;
    while (!frame_done) {
        if (GB_UNLIKELY(locked)) { tick(4); continue; }
        if (halted) {
            if (ie & iflag & 0x1F) halted = false;
            else {
                u64 gap = next_event > cycles ? next_event - cycles : 4;
                gap = (gap + 3) & ~3ULL;
                if (gap > (1u << 20)) gap = 1u << 20;
                tick((int)gap);
                continue;
            }
        }
        if (ime && (ie & iflag & 0x1F)) { service_irq(); continue; }
        if (GB_UNLIKELY(dbg_bp != nullptr)) {
            if ((*dbg_bp)[pc] && !dbg_skip) { dbg_hit = true; return; }
            dbg_skip = false;
        }
        ev = false;
        if (GB_UNLIKELY(halt_bug)) { interp_step(*this); continue; }   // hardware quirk, one instruction
        if (pc < 0x8000) {
            int bank = pc < 0x4000 ? cart.rom_bank0 : cart.rom_bank;
            if (trace_bitmap) {
                size_t off = (size_t)bank * 0x4000 + (pc & 0x3FFF);
                if (off < trace_bitmap->size()) (*trace_bitmap)[off] = 1;
            }
            if (native) {
                NativeFn fn = native->find(bank, pc);
                if (fn && fn(*this)) { fb_stats.native_blocks++; in_fallback = false; continue; }
                if (!in_fallback) note_missing(bank, pc);   // log only entry points of untranslated runs
                in_fallback = true;
                fb_stats.rom_instr++;
            }
        } else {
            if (ram_trace && ram_trace->size() < 200000) {
                RamExec r; r.pc = pc;
                for (int i = 0; i < 3; i++) r.bytes[i] = rd(*this, (u16)(pc + i));
                u64 key = ((u64)pc << 24) | ((u64)r.bytes[0] << 16) | ((u64)r.bytes[1] << 8) | r.bytes[2];
                // open-addressing hash set
                if (ram_seen.size() < 1024) ram_seen.assign(1 << 19, ~0ULL);
                size_t mask = ram_seen.size() - 1, hidx = (size_t)((key * 0x9E3779B97F4A7C15ULL) >> 40) & mask;
                while (ram_seen[hidx] != ~0ULL && ram_seen[hidx] != key) hidx = (hidx + 1) & mask;
                if (ram_seen[hidx] == ~0ULL) { ram_seen[hidx] = key; ram_trace->push_back(r); }
            }
            if (native) {
                NativeFn fn = native->find(-1, pc);       // guarded native RAM snippets
                if (fn && fn(*this)) { fb_stats.native_blocks++; continue; }
                fb_stats.ram_instr++;
            }
        }
        interp_step(*this);
    }
    frames++;
}

// ------------------------------------------------------------- widescreen scenery memory
void Machine::wide_track_sprites() {
    // An object slot is trusted outside the screen only while it got there by moving (walking, or the
    // camera scrolling away). A slot whose position jumps is a sprite being hidden or reused.
    const int hgt = (io[0x40] & 0x04) ? 16 : 8;
    for (int i = 0; i < 40; i++) {
        const u8 oy = oam[i * 4], ox = oam[i * 4 + 1];
        const int dx = std::abs((int)ox - spr_px[i]), dy = std::abs((int)oy - spr_py[i]);
        const int sx = ox - 8, sy = oy - 16;
        const bool on = sx > -8 && sx < 160 && sy > -hgt && sy < 144;
        if (dx > 12 || dy > 12) spr_smooth[i] = 0;
        else if (dx || dy) spr_smooth[i] = (u8)std::min(255, spr_smooth[i] + 4);
        if (on && !(dx > 12 || dy > 12)) spr_smooth[i] = (u8)std::max<int>(spr_smooth[i], 40);
        spr_parked[i] = !on && spr_smooth[i] < 24;
        spr_px[i] = ox; spr_py[i] = oy;
    }
}

void Machine::wide_memory_pass() {
    wide_track_sprites();
    // the camera: the scroll used by most lines (HUD rows with their own scroll are left out)
    int cnt[64] = {0}; u16 val[64]; int nv = 0;
    for (int y = 0; y < 144; y++) {
        if (wflags[y * WIDE_W + WIDE_X0 + 80] & 2) continue;            // window over the middle
        u16 v = (u16)(wline_scx[y] | (wline_scy[y] << 8));
        int k = 0; while (k < nv && val[k] != v) k++;
        if (k == nv) { if (nv == 64) continue; val[nv++] = v; }
        cnt[k]++;
    }
    int best = -1; for (int k = 0; k < nv; k++) if (best < 0 || cnt[k] > cnt[best]) best = k;
    if (best < 0) { wide_judge(-1, -1); if (wide_extend) wide_extend_pass(-1, -1); lcd_was_off = false; frame_map_writes = 0; return; }
    int rsx = val[best] & 255, rsy = val[best] >> 8;
    if (wide_memory) {
        int dx = world_ref_scx < 0 ? 0 : (s8)(u8)(rsx - world_ref_scx);
        int dy = world_ref_scy < 0 ? 0 : (s8)(u8)(rsy - world_ref_scy);
        // a new screen (fade-out with the LCD off, a jump, or a redraw of most of the map) starts a new memory
        u32 new_scene_start = 0;
        bool cut = lcd_was_off || std::abs(dx) > 40 || std::abs(dy) > 40 || frame_map_writes > 300;   // (scrolling only writes a row or column)
        if (cut) {
            wide_cuts++;
            if (wide_places) wide_archive_scene();
            world_clear();
            reloc_pending = wide_places && !wide_scenes.empty() ? 45 : 0;
            // the new screen: what the game wrote for it (this frame, or while the screen was dark) matches the camera
            const u32 f1 = (u32)frames + 1;
            new_scene_start = f1 > 3 ? f1 - 3 : 1;
            if (lcd_was_off && lcd_off_frame && lcd_off_frame < new_scene_start) new_scene_start = lcd_off_frame;
        } else if (world.size() > 2048) world_trim();
        world_x += dx; world_y += dy;
        if (dx) { wide_dir_x = dx > 0 ? 1 : -1; wide_moved_x = (u32)frames; }
        if (dy) { wide_dir_y = dy > 0 ? 1 : -1; wide_moved_y = (u32)frames; }
        if ((frames & 511) == 0) for (auto& a : tile_anim) a >>= 1;      // forget old tile activity
        world_ref_scx = rsx; world_ref_scy = rsy;
        if (new_scene_start) { map_rebase(); scene_start = new_scene_start; new_scene_start = 0; }
        if (reloc_pending > 0) {
            reloc_pending--;
            if (wide_relocate(rsx, rsy)) reloc_pending = 0;
        }
        u64 last_key = ~0ULL; std::vector<u32>* last_chunk = nullptr;     // neighbouring pixels share a chunk
        auto cell = [&](int wx, int wy, bool create) -> u32* {
            u64 key = ((u64)(u32)(wx >> 5) << 32) | (u32)(wy >> 5);
            if (key != last_key || (!last_chunk && create)) {
                auto it = world.find(key);
                if (it == world.end()) {
                    if (!create) { last_key = key; last_chunk = nullptr; return nullptr; }
                    it = world.emplace(key, std::vector<u32>(2048, 0)).first;
                }
                last_key = key; last_chunk = &it->second;
            }
            return last_chunk ? &(*last_chunk)[(size_t)((wy & 31) * 32 + (wx & 31))] : nullptr;
        };
        const u32 now = (u32)(frames + 1) & 0xFFFFFF;
        const bool per_tile = true;      // sides are judged tile by tile (see ppu_render_range)
        u32 dmgbg[4];
        for (int i = 0; i < 4; i++) dmgbg[i] = dmg_palette[(io[0x47] >> (i * 2)) & 3];
        for (int y = 0; y < 144; y++) {
            if (wline_scx[y] != rsx || wline_scy[y] != rsy) continue;
            u8* fl = &wflags[y * WIDE_W];
            const u8* bg = &wbg[y * WIDE_W];
            // remember the visible background
            for (int x = 0; x < 160; x++) {
                int i = x + WIDE_X0;
                if (fl[i] & 3) continue;                    // a sprite or the window covers the background here
                u32* c = cell(world_x + x, world_y + y, true);
                c[0] = 0x80u | bg[i] | (now << 8);
                c[1024] = ((u32)wtile[y * WIDE_W + i] + 1) << 8;
            }
            const u8* mc = &wmc[y * WIDE_W];
            // the extended view also remembers what the wide columns showed while the map had them fresh
            if (wide_extend)
                for (int i = 0; i < WIDE_W; i++) {
                    if (i == WIDE_X0) { i = WIDE_X0 + 159; continue; }
                    if (fl[i] & (1 | 2 | 4 | 16)) continue;
                    // (a tile at the sides that passed the per-tile check was written for this very place)
                    u32* c = cell(world_x + i - WIDE_X0, world_y + y, true);
                    c[0] = 0x80u | bg[i] | (now << 8);
                    c[1024] = ((u32)wtile[y * WIDE_W + i] + 1) << 8;
                }
            // show it again at the sides where the map no longer has it: the column is stale, or the
            // game reused that part of the map after the player saw it
            for (int i = 0; i < WIDE_W; i++) {
                if (i == WIDE_X0) { i = WIDE_X0 + 159; continue; }
                if (fl[i] & 3) continue;
                const u32* c = cell(world_x + i - WIDE_X0, world_y + y, false);
                if (!c || !(*c & 0x80)) continue;
                u32 seen = *c >> 8;
                if (!(fl[i] & (4 | 16)) && (per_tile || (map_col_frame[(mc[i] >> 5) & 1][mc[i] & 31] & 0xFFFFFF) <= seen)) continue;   // map still shows it
                int ci = *c & 3, pl = (*c >> 2) & 7;
                if (wide_animate && c[1024]) {             // an animated tile: its graphics as they are now
                    const u32 r = (c[1024] >> 8) - 1, t = r & 1023;
                    if (t < 768 && tile_anim[t] >= 3) {
                        const u8* td = &vram[t >= 384 ? 1 : 0][(t % 384) * 16 + ((r >> 13) & 7) * 2];
                        const int b = 7 - ((r >> 10) & 7);
                        ci = (((td[1] >> b) & 1) << 1) | ((td[0] >> b) & 1);
                    }
                }
                fbw[y * WIDE_W + i] = cgb ? bgcol[pl][ci] : dmgbg[ci];
                fl[i] |= 8;                                 // filled from memory
            }
        }
    }
    if (wide_hud) {
        // rows with their own scroll (status bars drawn with the background) are extended with their edge pixels
        for (int y = 0; y < 144; y++) {
            if (wline_scx[y] == rsx && wline_scy[y] == rsy) continue;
            u32* row = &fbw[y * WIDE_W];
            if (wflags[y * WIDE_W + WIDE_X0] & 2) continue;    // window rows are handled while drawing
            for (int i = 0; i < WIDE_X0; i++) if (!(wflags[y * WIDE_W + i] & 1)) row[i] = row[WIDE_X0];
            for (int i = WIDE_X0 + 160; i < WIDE_W; i++) if (!(wflags[y * WIDE_W + i] & 1)) row[i] = row[WIDE_X0 + 159];
        }
    }
    wide_judge(rsx, rsy);
    if (wide_extend) wide_extend_pass(rsx, rsy);
    lcd_was_off = false;
    frame_map_writes = 0;
}

// ------------------------------------------------------------- extended view (ultrawide / taller)
void Machine::wide_extend_pass(int rsx, int rsy) {
    if (fbx.size() != (size_t)XW * XH) { fbx.assign((size_t)XW * XH, 0xFF000000u); xknown.assign((size_t)XW * XH, 0); }
    const u8 lcdc = io[0x40];
    u32 dmgbg[4];
    for (int i = 0; i < 4; i++) dmgbg[i] = dmg_palette[(io[0x47] >> (i * 2)) & 3];
    u64 last_key = ~0ULL; const std::vector<u32>* last_chunk = nullptr;
    u32 mem_seen = 0;
    auto mem = [&](int wx, int wy, u32& out) -> bool {
        if (!wide_memory) return false;
        u64 key = ((u64)(u32)(wx >> 5) << 32) | (u32)(wy >> 5);
        if (key != last_key) { auto it = world.find(key); last_key = key; last_chunk = it == world.end() ? nullptr : &it->second; }
        if (!last_chunk) return false;
        const size_t ix = (size_t)((wy & 31) * 32 + (wx & 31));
        u32 c = (*last_chunk)[ix];
        if (!(c & 0x80)) return false;
        int ci = c & 3;
        if (wide_animate && (*last_chunk)[ix + 1024]) {
            const u32 r = ((*last_chunk)[ix + 1024] >> 8) - 1, t = r & 1023;
            if (t < 768 && tile_anim[t] >= 3) {
                const u8* td = &vram[t >= 384 ? 1 : 0][(t % 384) * 16 + ((r >> 13) & 7) * 2];
                const int b = 7 - ((r >> 10) & 7);
                ci = (((td[1] >> b) & 1) << 1) | ((td[0] >> b) & 1);
            }
        }
        out = cgb ? bgcol[(c >> 2) & 7][ci] : dmgbg[ci];
        mem_seen = c >> 8;
        return true;
    };
    const bool cam = rsx >= 0;
    // rows with their own scroll or the window at the top / bottom are status bars: nothing is shown beyond them
    auto bar = [&](int y) { return !cam || wline_scx[y] != rsx || wline_scy[y] != rsy || (wflags[y * WIDE_W + WIDE_X0 + 80] & 2); };
    const bool top_free = cam && !bar(0) && !bar(4), bot_free = cam && !bar(143) && !bar(139);
    const bool top_was = wide_top_ok, bot_was = wide_bot_ok;
    wide_top_ok = top_free; wide_bot_ok = bot_free;
    const bool bg_on = cgb || (lcdc & 1);
    const int mp = (lcdc & 0x08) ? 1 : 0, mapbase = mp ? 0x1C00 : 0x1800;
    u32 fresh = line_fresh_min;
    for (int Y = 0; Y < XH; Y++) {
        const int y = Y - XY0;
        u32* o = &fbx[(size_t)Y * XW];
        u8* k = &xknown[(size_t)Y * XW];
        if (y >= 0 && y < 144) {
            const u32* w = &fbw[y * WIDE_W];
            const u8* fl = &wflags[y * WIDE_W];
            const bool hud_row = cam && wide_hud && (wline_scx[y] != rsx || wline_scy[y] != rsy);
            for (int X = 0; X < XW; X++) {
                int x = X - XX0, i = x + WIDE_X0;
                if (i >= 0 && i < WIDE_W) {
                    u8 f = fl[i];
                    o[X] = w[i];
                    k[X] = (!(f & 3) && (f & 4) && !(f & 8)) ? 0 : 255;
                    continue;
                }
                const int e = i < 0 ? 0 : WIDE_W - 1;                    // outer column of the wide picture
                const bool win_edge = wide_hud && (fl[i < 0 ? WIDE_X0 : WIDE_X0 + 159] & 2) && (fl[e] & 2);
                u32 c;
                if (hud_row || win_edge) { o[X] = w[e]; k[X] = 255; }      // status bars continue to the edge
                else if (cam && mem(world_x + x, world_y + y, c)) { o[X] = c; k[X] = 255; }
                else { o[X] = w[e]; k[X] = 0; }
            }
            continue;
        }
        const bool side_ok = y < 0 ? wide_top_ok : wide_bot_ok;
        const int ey = y < 0 ? 0 : 143;
        const int py = cam ? (y + rsy) & 255 : 0;
        const bool row_ahead = map_row_behind[mp][py >> 3] && ((y >= 144 && wide_dir_y > 0) || (y < 0 && wide_dir_y < 0));
        const bool row_fresh = cam && bg_on && side_ok && map_row_frame[mp][py >> 3] >= fresh && !row_ahead;
        for (int X = 0; X < XW; X++) {
            const int x = X - XX0;
            u32 c;
            if (!side_ok) { o[X] = fbw[ey * WIDE_W + std::min(std::max(x + WIDE_X0, 0), WIDE_W - 1)]; k[X] = 0; continue; }
            bool done = false;
            const bool have_mem = mem(world_x + x, world_y + y, c);
            // the map is used while it is fresh and was not rewritten after the player saw that spot
            // (rows that leave the screen are often cleared or reused by the game)
            const bool tiles_known = cam && wide_memory && world_ref_scx >= 0;
            if ((tiles_known ? (bg_on && side_ok) : row_fresh) && x >= -WIDE_X0 && x < 160 + WIDE_X0 &&
                (tiles_known || !(have_mem && (map_row_frame[mp][py >> 3] & 0xFFFFFF) > mem_seen))) {
                const int px = (x + rsx) & 255, col = px >> 3;
                const bool vis = x >= 0 && x < 160;
                const bool ahead = !vis && map_col_behind[mp][col] && ((x >= 160 && wide_dir_x > 0) || (x < 0 && wide_dir_x < 0));
                // with the scenery memory, a tile is used only if the game wrote it for this very place
                const bool ok = tiles_known ? tile_here(mp, (py >> 3) * 32 + col, world_x + x - (px & 7), world_y + y - (py & 7))
                                            : (vis || map_col_frame[mp][col] >= fresh) && !ahead;
                if (ok) {
                    const int mi = mapbase + (py >> 3) * 32 + col;
                    const u8 tidx = vram[0][mi], attr = cgb ? vram[1][mi] : 0;
                    const int tno = (lcdc & 0x10) ? tidx : (256 + (s8)tidx);
                    int ty = py & 7, tx = px & 7;
                    if (attr & 0x40) ty = 7 - ty;
                    if (attr & 0x20) tx = 7 - tx;
                    const u8* td = &vram[(attr & 0x08) ? 1 : 0][tno * 16 + ty * 2];
                    const int ci = (((td[1] >> (7 - tx)) & 1) << 1) | ((td[0] >> (7 - tx)) & 1);
                    o[X] = cgb ? bgcol[attr & 7][ci] : dmgbg[ci];
                    k[X] = 255; done = true;
                }
            }
            if (done) continue;
            if (have_mem) { o[X] = c; k[X] = 255; }
            else { o[X] = fbw[ey * WIDE_W + std::min(std::max(x + WIDE_X0, 0), WIDE_W - 1)]; k[X] = 0; }
        }
    }
    // objects outside the wide picture (above, below, or beyond 256 columns) that are really in play
    if (wide_sprites && (lcdc & 0x02)) {
        const int hgt = (lcdc & 0x04) ? 16 : 8;
        u32 dmgobj[2][4];
        for (int i = 0; i < 4; i++) { dmgobj[0][i] = dmg_obj0[(io[0x48] >> (i * 2)) & 3]; dmgobj[1][i] = dmg_obj1[(io[0x49] >> (i * 2)) & 3]; }
        for (int i = 39; i >= 0; i--) {
            if (spr_parked[i]) continue;
            const int sy = oam[i * 4] - 16, sx = oam[i * 4 + 1] - 8;
            if (sy >= 0 && sy + hgt <= 144 && sx >= -WIDE_X0 && sx + 8 <= 160 + WIDE_X0) continue;   // inside the wide picture
            if ((sy < 0 && !wide_top_ok) || (sy + hgt > 144 && !wide_bot_ok)) continue;
            u8 tile = oam[i * 4 + 2]; const u8 at = oam[i * 4 + 3];
            if (hgt == 16) tile &= 0xFE;
            const int bank = (cgb && (at & 0x08)) ? 1 : 0;
            for (int r = 0; r < hgt; r++) {
                const int y = sy + r, Y = y + XY0;
                if (Y < 0 || Y >= XH) continue;
                const int row = (at & 0x40) ? hgt - 1 - r : r;
                const u8* td = &vram[bank][(tile + (row >> 3)) * 16 + (row & 7) * 2];
                for (int px = 0; px < 8; px++) {
                    const int x = sx + px, X = x + XX0;
                    if (X < 0 || X >= XW) continue;
                    if (y >= 0 && y < 144 && x >= -WIDE_X0 && x < 160 + WIDE_X0) continue;   // drawn by the line renderer
                    const int bit = (at & 0x20) ? px : 7 - px;
                    const int ci = (((td[1] >> bit) & 1) << 1) | ((td[0] >> bit) & 1);
                    if (!ci) continue;
                    fbx[(size_t)Y * XW + X] = cgb ? objcol[at & 7][ci] : dmgobj[(at & 0x10) ? 1 : 0][ci];
                    xknown[(size_t)Y * XW + X] = 255;
                }
            }
        }
    }
    // a side is opened only while most of it shows something real (otherwise it would be an empty band)
    auto share = [&](int Y0, int Y1) {
        int n = 0, t = 0;
        for (int Y = Y0; Y < Y1; Y++) for (int X = XX0; X < XX0 + 160; X++) { t++; n += xknown[(size_t)Y * XW + X] != 0; }
        return t ? (float)n / t : 0.f;
    };
    wide_top_score += ((top_free ? share(XY0 - 16, XY0) : 0.f) - wide_top_score) * 0.12f;
    wide_bot_score += ((bot_free ? share(XY0 + 144, XY0 + 160) : 0.f) - wide_bot_score) * 0.12f;
    wide_top_ok = top_free && wide_top_score > (top_was ? 0.45f : 0.7f);
    wide_bot_ok = bot_free && wide_bot_score > (bot_was ? 0.45f : 0.7f);
}

void Machine::wide_judge(int rsx, int rsy) {
    int bl = 0, tl = 0, br = 0, tr = 0, win_rows = 0;
    for (int y = 0; y < 144; y++) {
        const u8* fl = &wflags[y * WIDE_W];
        if (fl[WIDE_X0] & 2) win_rows++;                                   // window from the left edge of the screen
        if (rsx >= 0 && (wline_scx[y] != rsx || wline_scy[y] != rsy)) continue;   // HUD rows with their own scroll
        for (int i = 0; i < WIDE_W; i++) {
            if (i == WIDE_X0) { i = WIDE_X0 + 159; continue; }
            u8 f = fl[i];
            bool bad = !(f & 3) && (f & 20) && !(f & 8);                   // stale map column nobody filled
            if (i < WIDE_X0) { tl++; bl += bad; } else { tr++; br += bad; }
        }
    }
    wide_bad_l = tl ? (float)bl / tl : 0;
    wide_bad_r = tr ? (float)br / tr : 0;
    wide_menu = rsx < 0 || win_rows >= 100;
    bool bad = wide_menu || (wide_bad_l > 0.45f && wide_bad_r > 0.45f);
    if (bad) { wide_bad_run++; wide_good_run = 0; } else { wide_good_run++; wide_bad_run = 0; }
    if (!wide_smart) { wide_narrow = false; wide_vis = 1.0f; return; }
    if (!wide_narrow && wide_bad_run >= 4) wide_narrow = true;            // react fast to leftovers
    else if (wide_narrow && wide_good_run >= 40) wide_narrow = false;     // widen again only when the scene is stable
    if (wide_narrow) wide_vis = std::max(0.0f, wide_vis - 1.0f / 10);
    else wide_vis = std::min(1.0f, wide_vis + 1.0f / 20);
}

void Machine::step_one() {
    dbg_skip = false;
    if (locked) { tick(4); return; }
    if (halted) {
        if (ie & iflag & 0x1F) halted = false;
        else { u64 gap = next_event > cycles ? next_event - cycles : 4; gap = (gap + 3) & ~3ULL; tick((int)std::min<u64>(gap, 1u << 20)); return; }
    }
    if (ime && (ie & iflag & 0x1F)) { service_irq(); return; }
    ev = false;
    interp_step(*this);
    if (frame_done) { frame_done = false; frames++; }
}

// ============================================================ PPU
static inline u32 cgb_color(const Machine& m, u16 c) {
    int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    int R, G, B;
    if (m.color_correct) {
        R = std::min(255, (r * 26 + g * 4 + b * 2) >> 2);
        G = std::min(255, (g * 24 + b * 8) >> 2);
        B = std::min(255, (r * 6 + g * 4 + b * 22) >> 2);
    } else { R = (r << 3) | (r >> 2); G = (g << 3) | (g >> 2); B = (b << 3) | (b >> 2); }
    return 0xFF000000u | (R << 16) | (G << 8) | B;
}

void Machine::ppu_update_stat() {
    if (!(io[0x40] & 0x80)) { stat_line = false; return; }
    u8 s = io[0x41];
    bool line = ((s & 0x40) && ly_reg(*this) == io[0x45]) || ((s & 0x08) && mode == 0) || ((s & 0x10) && mode == 1) || ((s & 0x20) && mode == 2);
    if (line && !stat_line) iflag |= IRQ_STAT;
    stat_line = line;
}

void Machine::ppu_tick(int t) {
    if (!(io[0x40] & 0x80)) {
        lcd_off_dots += t;
        if (lcd_off_dots >= 70224) {
            lcd_off_dots -= 70224;
            u32 blank = cgb ? 0xFFFFFFFF : dmg_palette[0];
            for (int i = 0; i < 160 * 144; i++) fb[i] = blank;
            for (int i = 0; i < WIDE_W * 144; i++) { fbw[i] = blank; pinfo[i].tile = 0xFFFF; }
            lcd_was_off = true;
            frame_done = true;
        }
        return;
    }
    dot += t;
    for (;;) {
        if (ly < 144) {
            if (mode == 2 && dot >= 80) { mode = 3; ppu_line_begin(); ppu_update_stat(); }
            else if (mode == 3 && dot >= 252) { mode = 0; ppu_line_end(); hdma_hblank(); ppu_update_stat(); }
            else if (dot >= 456) {
                dot -= 456; ly++;
                if (ly == 144) {
                    mode = 1; iflag |= IRQ_VBLANK; frame_done = true;
                    if (widescreen) wide_memory_pass();
                    if (sgb) sgb->on_vblank(*this);
                    // hardware quirk: entering VBlank also requests the mode-2 (OAM) STAT interrupt
                    if ((io[0x41] & 0x20) && !stat_line) iflag |= IRQ_STAT;
                }
                else mode = 2;
                ppu_update_stat();
            } else break;
        } else {
            if (ly == 153 && dot >= 4 && !ly153_zero) { ly153_zero = true; ppu_update_stat(); }
            if (dot >= 456) {
                dot -= 456; ly++;
                ly153_zero = false;
                if (ly > 153) { ly = 0; mode = 2; win_line = 0; }
                ppu_update_stat();
            } else break;
        }
    }
}

void Machine::ppu_line_begin() {
    if (ly < 144) { wline_scx[ly] = io[0x43]; wline_scy[ly] = io[0x42]; }
    if (ly == 0) wy_triggered = false;
    if (ly == io[0x4A]) wy_triggered = true;
    // OAM scan: up to 10 sprites on this line (or all of them when the limit is disabled)
    const u8 lcdc = io[0x40];
    int hgt = (lcdc & 0x04) ? 16 : 8;
    int limit = no_sprite_limit ? 40 : 10;
    line_nsel = 0;
    for (int i = 0; i < 40 && line_nsel < limit; i++) {
        int sy = oam[i * 4] - 16;
        if (ly >= sy && ly < sy + hgt) line_sel[line_nsel++] = i;
    }
    if (!cgb) std::stable_sort(line_sel, line_sel + line_nsel, [&](int p, int q) { return oam[p * 4 + 1] < oam[q * 4 + 1]; });
    line_x = 0;
    line_win_used = false;
    line_win_active = false;
    line_scx_low = io[0x43] & 7;
}

void Machine::ppu_catch_up() {
    if (!(io[0x40] & 0x80) || ly >= 144 || mode != 3) return;
    int x = dot - ppu_px_offset - line_scx_low;
    if (x > 160) x = 160;
    if (x > line_x) { ppu_render_range(line_x, x); line_x = x; }
}

void Machine::ppu_line_end() {
    if (line_x < 160) ppu_render_range(line_x, 160);
    line_x = 160;
    if (widescreen) {
        // columns of the map the game did not refresh together with the visible ones usually hold
        // leftovers of other screens: they are drawn as plain background (wide_edge >= 1)
        const int map = (io[0x40] & 0x08) ? 1 : 0, c0 = io[0x43] >> 3;
        u32 mn = 0xFFFFFFFFu;
        for (int k = 1; k < 19; k++) mn = std::min(mn, map_col_frame[map][(c0 + k) & 31]);   // inner visible columns
        line_fresh_min = mn > 90 ? mn - 90 : 0;      // 1.5 s tolerance: screens drawn over several frames
        // the visible screen was drawn after the last time the LCD went dark: everything older is from before
        if (lcd_off_frame && mn >= lcd_off_frame) line_fresh_min = std::max(line_fresh_min, lcd_off_frame);
        line_cam_x = cam_x(); line_cam_y = cam_y();
        ppu_render_range(-WIDE_X0, 0); ppu_render_range(160, 160 + WIDE_X0);
        if (wide_edge == 2)       // gentle fade towards the outer edges
            for (int i = 0; i < WIDE_X0; i++) {
                int k = 256 - (WIDE_X0 - i) * 140 / WIDE_X0;     // 0.45 .. ~1.0
                for (int side = 0; side < 2; side++) {
                    u32& c = line_buf[side ? WIDE_W - 1 - i : i];
                    c = 0xFF000000u | ((((c >> 16) & 255) * k >> 8) << 16) | ((((c >> 8) & 255) * k >> 8) << 8) | ((c & 255) * k >> 8);
                }
            }
    }
    if (line_win_used) win_line++;
    const int y = ly;
    if (sgb) sgb->colorize_line(*this, y, line_shade);
    memcpy(&fb[y * 160], &line_buf[WIDE_X0], 160 * sizeof(u32));
    if (widescreen) memcpy(&fbw[y * WIDE_W], line_buf, sizeof line_buf);
    if (track_pixels) {
        if (widescreen) memcpy(&pinfo[y * WIDE_W], line_pi, sizeof line_pi);
        else memcpy(&pinfo[y * WIDE_W + WIDE_X0], &line_pi[WIDE_X0], 160 * sizeof(PixelInfo));
    }
}

void Machine::ppu_render_range(int xs, int xe) {
    if (xe <= xs) return;
    const u8 lcdc = io[0x40];
    const int y = ly;
    u32* line = line_buf;
    PixelInfo* pil = line_pi;
    u8* bgidx = line_bgidx;
    u8* bgattr = line_bgattr;
    const u8 bgp = io[0x47];
    u32 dmgbg[4];
    if (cgb) {
        if (pal_dirty) {
            pal_dirty = false;
            for (int p = 0; p < 8; p++)
                for (int i = 0; i < 4; i++) {
                    bgcol[p][i] = cgb_color(*this, (u16)(bgpal[p * 8 + i * 2] | (bgpal[p * 8 + i * 2 + 1] << 8)));
                    objcol[p][i] = cgb_color(*this, (u16)(objpal[p * 8 + i * 2] | (objpal[p * 8 + i * 2 + 1] << 8)));
                }
        }
    } else {
        for (int i = 0; i < 4; i++) dmgbg[i] = dmg_palette[(bgp >> (i * 2)) & 3];
    }
    bool bg_on = cgb || (lcdc & 1);
    if (bg_on) {
        const int wx = io[0x4B] - 7;
        const bool win = (lcdc & 0x20) && wy_triggered && io[0x4B] <= 166;
        const u8 scy = io[0x42], scx = io[0x43];
        int last_key = -1;
        u8 lo = 0, hi = 0, attr = 0;
        int tno = 0, ty = 0, bank = 0;
        for (int x = xs; x < xe; x++) {
            int mapbase, px, py;
            bool vis = x >= 0 && x < 160;
            if (vis && win && !line_win_active && x >= wx) { line_win_active = true; line_win_x0 = wx; }
            bool in_win = vis ? (win && line_win_active) : (win && x >= wx);
            int wx_used = (vis && line_win_active) ? line_win_x0 : wx;
            bool edge_fill = false, stale = false, old_col = false;
            if (win && (x < 0 || x >= 160)) {
                // widescreen: a window that touches the screen edge (HUD bars) is extended
                // with its background colour instead of showing unrelated map tiles
                if (x < 0 && wx <= 0) { in_win = true; edge_fill = true; }
                else if (x >= 160 && in_win) edge_fill = true;
            }
            if (in_win) {
                // HUD bars: the edge pixel column of the window is repeated outwards, so borders and
                // lines continue across the whole width
                int xc = edge_fill ? (x < 0 ? std::max(wx_used, 0) : 159) : x;
                mapbase = (lcdc & 0x40) ? 0x1C00 : 0x1800; px = xc - wx_used; py = win_line;
                if (vis) line_win_used = true;
            } else {
                // fine scroll (SCX & 7) is latched at the start of the line; the tile column
                // uses the current SCX at fetch time
                mapbase = (lcdc & 0x08) ? 0x1C00 : 0x1800;
                int fx = x + line_scx_low;
                px = ((((fx >> 3) + (scx >> 3)) & 31) << 3) | (fx & 7);
                py = (y + scy) & 255;
                const int mpi = (lcdc & 0x08) ? 1 : 0;
                bool old_map;
                if (!vis && wide_memory && world_ref_scx >= 0) {
                    // the tile is real scenery only if the game wrote it for the place where it shows now
                    const int mi = (py >> 3) * 32 + ((px >> 3) & 31);
                    old_map = !tile_here(mpi, mi, line_cam_x + x - (px & 7), line_cam_y + y - (py & 7));
                } else
                    old_map = !vis && (map_col_frame[mpi][(px >> 3) & 31] < line_fresh_min ||
                                       (map_col_behind[mpi][(px >> 3) & 31] && ((x >= 160 && wide_dir_x > 0) || (x < 0 && wide_dir_x < 0))));
                if (old_map) {
                    if (wide_edge) edge_fill = stale = true;
                    else old_col = true;                     // shown as is, but the smart mode still counts it
                }
            }
            int mi = mapbase + (py >> 3) * 32 + ((px >> 3) & 31);
            int key = (mi << 3) | (py & 7);
            if (key != last_key) {
                last_key = key;
                u8 tidx = vram[0][mi];
                attr = cgb ? vram[1][mi] : 0;
                bank = (attr & 0x08) ? 1 : 0;
                tno = (lcdc & 0x10) ? tidx : (256 + (s8)tidx);
                ty = py & 7;
                if (attr & 0x40) ty = 7 - ty;
                const u8* td = &vram[bank][tno * 16 + ty * 2];
                lo = td[0]; hi = td[1];
            }
            int tx = px & 7;
            if (attr & 0x20) tx = 7 - tx;
            int ci = stale ? 0 : ((((hi >> (7 - tx)) & 1) << 1) | ((lo >> (7 - tx)) & 1));
            int i = x + WIDE_X0;
            if (y < 144) {
                wflags[y * WIDE_W + i] = (u8)((in_win ? 2 : 0) | (stale ? 4 : 0) | (old_col ? 16 : 0));
                wbg[y * WIDE_W + i] = (u8)(ci | ((attr & 7) << 2));
                wmc[y * WIDE_W + i] = (u8)(((px >> 3) & 31) | ((lcdc & 0x08) ? 0x20 : 0));
                wtile[y * WIDE_W + i] = (stale || in_win) ? 0xFFFF : (u16)((bank * 384 + tno) | (tx << 10) | (ty << 13));
            }
            bgidx[i] = (u8)ci; bgattr[i] = attr;
            line_shade[i] = (u8)((bgp >> (ci * 2)) & 3);
            if (cgb) {
                line[i] = bgcol[attr & 7][ci];
                pil[i].tile = (u16)(bank * 384 + tno); pil[i].pal = (u8)(attr & 7); pil[i].sub = (u8)(tx | (ty << 3));
            } else {
                line[i] = dmgbg[ci];
                pil[i].tile = (u16)tno; pil[i].pal = bgp; pil[i].sub = (u8)(tx | (ty << 3));
            }
            if (edge_fill) pil[i].tile = 0xFFFF;
        }
    } else {
        for (int x = xs; x < xe; x++) { int i = x + WIDE_X0; line[i] = dmg_palette[0]; bgidx[i] = 0; bgattr[i] = 0; pil[i].tile = 0xFFFF; line_shade[i] = 0; }
    }

    if (lcdc & 0x02) {
        int hgt = (lcdc & 0x04) ? 16 : 8;
        u32 dmgobj[2][4];
        if (!cgb) for (int i = 0; i < 4; i++) { dmgobj[0][i] = dmg_obj0[(io[0x48] >> (i * 2)) & 3]; dmgobj[1][i] = dmg_obj1[(io[0x49] >> (i * 2)) & 3]; }
        for (int k = line_nsel - 1; k >= 0; k--) {
            int i = line_sel[k];
            int ox = oam[i * 4 + 1];
            // "parked" (hidden) sprites stay hidden; objects that walked or scrolled out of the screen are drawn
            if (widescreen && !wide_parked && (ox == 0 || ox >= 168) && (!wide_sprites || spr_parked[i])) continue;
            int sy = oam[i * 4] - 16, sx = ox - 8;
            if (sx + 8 <= xs || sx >= xe) continue;
            u8 tile = oam[i * 4 + 2], at = oam[i * 4 + 3];
            int row = y - sy;
            if (row >= hgt) continue;
            if (at & 0x40) row = hgt - 1 - row;
            if (hgt == 16) tile &= 0xFE;
            int tno = tile + (row >> 3);
            int bank = (cgb && (at & 0x08)) ? 1 : 0;
            const u8* td = &vram[bank][tno * 16 + (row & 7) * 2];
            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x < xs || x >= xe) continue;
                int bit = (at & 0x20) ? px : 7 - px;
                int ci = (((td[1] >> bit) & 1) << 1) | ((td[0] >> bit) & 1);
                if (!ci) continue;
                int li = x + WIDE_X0;
                if (y < 144) wflags[y * WIDE_W + li] |= 1;
                if (cgb) {
                    bool master = lcdc & 1;
                    if (master && bgidx[li] && ((bgattr[li] & 0x80) || (at & 0x80))) continue;
                    int p = at & 7;
                    line[li] = objcol[p][ci];
                    pil[li].tile = (u16)(bank * 384 + tno); pil[li].pal = (u8)(8 + p); pil[li].sub = (u8)((7 - bit) | ((row & 7) << 3));
                } else {
                    if ((at & 0x80) && bgidx[li]) continue;
                    u8 obp = (at & 0x10) ? io[0x49] : io[0x48];
                    line[li] = dmgobj[(at & 0x10) ? 1 : 0][ci];
                    line_shade[li] = (u8)((obp >> (ci * 2)) & 3);
                    pil[li].tile = (u16)(tno | ((at & 0x10) ? 0xC000 : 0x8000)); pil[li].pal = obp; pil[li].sub = (u8)((7 - bit) | ((row & 7) << 3));
                }
            }
        }
    }
}

// ============================================================ saves / states
std::vector<u8> Machine::save_data() const {
    std::vector<u8> d = cart.ram;
    if (cart.info.has_rtc) d.insert(d.end(), cart.rtc, cart.rtc + 5);
    return d;
}
void Machine::load_save_data(const std::vector<u8>& d) {
    size_t n = std::min(d.size(), cart.ram.size());
    memcpy(cart.ram.data(), d.data(), n);
    if (cart.info.has_rtc && d.size() >= cart.ram.size() + 5) memcpy(cart.rtc, &d[cart.ram.size()], 5);
}

template <class T> static void put(std::vector<u8>& s, const T& v) { const u8* p = (const u8*)&v; s.insert(s.end(), p, p + sizeof(T)); }
template <class T> static void get(const u8*& p, T& v) { memcpy(&v, p, sizeof(T)); p += sizeof(T); }

std::vector<u8> Machine::save_state() const {
    const_cast<Machine*>(this)->sync();     // bring the lazily updated hardware (timer, PPU, sound) up to date first
    std::vector<u8> s;
    s.insert(s.end(), {'N', 'B', 'S', '1'});
    put(s, a); put(s, f); put(s, b); put(s, c); put(s, d); put(s, e); put(s, h); put(s, l); put(s, sp); put(s, pc);
    put(s, ime); put(s, ei_delay); put(s, halted); put(s, cgb); put(s, double_speed);
    s.insert(s.end(), (const u8*)vram, (const u8*)vram + sizeof vram);
    s.insert(s.end(), (const u8*)wram, (const u8*)wram + sizeof wram);
    s.insert(s.end(), oam, oam + sizeof oam); s.insert(s.end(), io, io + sizeof io); s.insert(s.end(), hram, hram + sizeof hram);
    put(s, ie); put(s, iflag); put(s, vram_bank); put(s, wram_bank); put(s, div);
    put(s, dot); put(s, ly); put(s, mode); put(s, stat_line); put(s, win_line); put(s, lcd_off_dots);
    s.insert(s.end(), bgpal, bgpal + 64); s.insert(s.end(), objpal, objpal + 64);
    put(s, hdma_active); put(s, hdma_src); put(s, hdma_dst); put(s, hdma_len);
    put(s, cart.rom_bank); put(s, cart.rom_bank0); put(s, cart.ram_bank); put(s, cart.ram_enabled); put(s, cart.mbc1_mode);
    put(s, cart.bank_lo); put(s, cart.bank_hi);
    s.insert(s.end(), cart.rtc, cart.rtc + 5); s.insert(s.end(), cart.rtc_latched, cart.rtc_latched + 5);
    u32 rs = (u32)cart.ram.size(); put(s, rs); s.insert(s.end(), cart.ram.begin(), cart.ram.end());
    apu->save(s);
    if (sgb) { s.insert(s.end(), {'S', 'G', 'B', '1'}); const u8* p = (const u8*)sgb; s.insert(s.end(), p, p + sizeof(Sgb)); }
    return s;
}

void Machine::map_rebase() {
    const int cx = cam_x(), cy = cam_y();
    for (int mp = 0; mp < 2; mp++)
        for (int mi = 0; mi < 1024; mi++) {
            int rel = ((mi & 31) * 8 - io[0x43]) & 255, rrel = ((mi >> 5) * 8 - io[0x42]) & 255;
            map_tile_wx[mp][mi] = cx + map_rel_x(rel);
            map_tile_wy[mp][mi] = cy + map_rel_y(rrel);
        }
}

bool Machine::load_state(const std::vector<u8>& s, bool keep_scene) {
    if (s.size() < 16 || memcmp(s.data(), "NBS1", 4) != 0) return false;
    const u8* p = s.data() + 4;
    bool scgb;
    get(p, a); get(p, f); get(p, b); get(p, c); get(p, d); get(p, e); get(p, h); get(p, l); get(p, sp); get(p, pc);
    get(p, ime); get(p, ei_delay); get(p, halted); get(p, scgb); get(p, double_speed);
    if (scgb != cgb) return false;
    if (!keep_scene) {
        for (auto& mp : map_col_frame) for (auto& c : mp) c = (u32)frames + 1;   // the restored map counts as fresh
        world_clear();
    }
    memcpy(vram, p, sizeof vram); p += sizeof vram;
    memcpy(wram, p, sizeof wram); p += sizeof wram;
    memcpy(oam, p, sizeof oam); p += sizeof oam; memcpy(io, p, sizeof io); p += sizeof io; memcpy(hram, p, sizeof hram); p += sizeof hram;
    get(p, ie); get(p, iflag); get(p, vram_bank); get(p, wram_bank); get(p, div);
    get(p, dot); get(p, ly); get(p, mode); get(p, stat_line); get(p, win_line); get(p, lcd_off_dots);
    memcpy(bgpal, p, 64); p += 64; memcpy(objpal, p, 64); p += 64;
    get(p, hdma_active); get(p, hdma_src); get(p, hdma_dst); get(p, hdma_len);
    get(p, cart.rom_bank); get(p, cart.rom_bank0); get(p, cart.ram_bank); get(p, cart.ram_enabled); get(p, cart.mbc1_mode);
    get(p, cart.bank_lo); get(p, cart.bank_hi);
    memcpy(cart.rtc, p, 5); p += 5; memcpy(cart.rtc_latched, p, 5); p += 5;
    u32 rs; get(p, rs);
    if (rs == cart.ram.size()) memcpy(cart.ram.data(), p, rs);
    p += rs;
    p = apu->load(p);
    if (sgb && (size_t)(p - s.data()) + 4 + sizeof(Sgb) <= s.size() && memcmp(p, "SGB1", 4) == 0) memcpy((void*)sgb, p + 4, sizeof(Sgb));
    locked = false; ev = true;
    synced = cycles; next_event = 0; pal_dirty = true;
    for (int i = 0; i < 768; i++) tile_gen[i] = ++vram_write_gen;
    map_pages();
    // the restored map counts as written for the place it shows now (run-ahead puts back its own copy).
    // Going back in time (keep_scene), only what is on the real screen is sure: the sides come from the
    // remembered scenery, which is kept
    map_rebase();
    for (int mp = 0; mp < 2; mp++)
        for (int mi = 0; mi < 1024; mi++) {
            const int rel = ((mi & 31) * 8 - io[0x43]) & 255, rrel = ((mi >> 5) * 8 - io[0x42]) & 255;
            const bool on_screen = (rel < 160 || rel > 248) && (rrel < 144 || rrel > 248);
            map_tile_frame[mp][mi] = !keep_scene || on_screen ? (u32)frames + 1 : 0;
        }
    if (scene_start > (u32)frames + 1) scene_start = (u32)frames + 1;
    return true;
}

u64 Machine::state_hash() const {
    u64 hsh = 1469598103934665603ULL;
    auto mix = [&](const void* data, size_t n) {
        const u8* q = (const u8*)data;
        for (size_t i = 0; i < n; i++) { hsh ^= q[i]; hsh *= 1099511628211ULL; }
    };
    u8 regs[8] = {a, f, b, c, d, e, h, l};
    mix(regs, 8); mix(&sp, 2); mix(&pc, 2);
    mix(wram, sizeof wram); mix(vram, sizeof vram); mix(hram, sizeof hram); mix(oam, sizeof oam);
    mix(fb, sizeof fb);
    if (!cart.ram.empty()) mix(cart.ram.data(), cart.ram.size());
    return hsh;
}

} // namespace gb

namespace gb {
// ------------------------------------------------------------------ widescreen: visited places
void Machine::world_trim() {
    // keeps the most recently seen 3/4 of the memory
    std::vector<std::pair<u32, u64>> age;
    age.reserve(world.size());
    for (auto& kv : world) {
        u32 newest = 0;
        for (u32 c : kv.second) if (c & 0x80) newest = std::max(newest, c >> 8);
        age.push_back({newest, kv.first});
    }
    std::sort(age.begin(), age.end());
    for (size_t i = 0; i < age.size() / 4; i++) world.erase(age[i].second);
}

void Machine::wide_archive_scene() {
    if (world.size() < 4) return;                      // a blank or very short screen is not worth keeping
    WideScene sc;
    sc.world.swap(world);
    sc.frame = (u32)frames;
    sc.cam_x = world_x; sc.cam_y = world_y;
    wide_scenes.push_back(std::move(sc));
    // at most 8 places and ~24 MB of memory; the oldest are forgotten first
    size_t total = 0;
    for (auto& w : wide_scenes) total += w.world.size();
    while (wide_scenes.size() > 24 || (total > 4096 && wide_scenes.size() > 1)) {
        total -= wide_scenes.front().world.size();
        wide_scenes.erase(wide_scenes.begin());
    }
}

// ---- remembered places kept between sessions
std::vector<u8> Machine::wide_export() const {
    std::vector<u8> d;
    auto put = [&](const void* p, size_t n) { const u8* b = (const u8*)p; d.insert(d.end(), b, b + n); };
    put("NBWORLD2", 8);
    u32 n = (u32)wide_scenes.size() + (world.size() >= 4 ? 1 : 0);
    put(&n, 4);
    auto scene = [&](const std::unordered_map<u64, std::vector<u32>>& w, int cx, int cy) {
        int32_t c[2] = {cx, cy}; u32 k = (u32)w.size();
        put(c, 8); put(&k, 4);
        for (auto& kv : w) { put(&kv.first, 8); put(kv.second.data(), 2048 * 4); }
    };
    for (auto& sc : wide_scenes) scene(sc.world, sc.cam_x, sc.cam_y);
    if (world.size() >= 4) scene(world, world_x, world_y);
    return d;
}

bool Machine::wide_import(const std::vector<u8>& d) {
    size_t p = 0;
    auto get = [&](void* o, size_t n) { if (p + n > d.size()) return false; memcpy(o, &d[p], n); p += n; return true; };
    char mg[8];
    if (!get(mg, 8) || memcmp(mg, "NBWORLD2", 8)) return false;
    u32 n = 0;
    if (!get(&n, 4) || n > 64) return false;
    std::vector<WideScene> in;
    for (u32 i = 0; i < n; i++) {
        int32_t c[2]; u32 k = 0;
        if (!get(c, 8) || !get(&k, 4) || k > 8192) return false;
        WideScene sc; sc.cam_x = c[0]; sc.cam_y = c[1]; sc.imported = true;
        for (u32 j = 0; j < k; j++) {
            u64 key; std::vector<u32> v(2048);
            if (!get(&key, 8) || !get(v.data(), 2048 * 4)) return false;
            // "seen" is a frame number of the old session: from now on the live map wins wherever it is fresh
            for (int q = 0; q < 1024; q++) if (v[q] & 0x80) v[q] |= 0xFFFFFF00u;
            sc.world.emplace(key, std::move(v));
        }
        if (sc.world.size() >= 4) in.push_back(std::move(sc));
    }
    for (auto& sc : in) wide_scenes.push_back(std::move(sc));
    reloc_pending = wide_places && !wide_scenes.empty() ? 45 : 0;
    return true;
}

void Machine::wide_merge_predicted(const Machine& scout) {
    // only what the copy saw in the last few frames: a newer look replaces an older prediction
    const u32 s_seen_floor = (u32)((scout.frames + 1) & 0xFFFFFF) > 4 ? (u32)((scout.frames + 1) & 0xFFFFFF) - 4 : 0;
    for (auto& kv : scout.world) {
        std::vector<u32>* dst = nullptr;
        const std::vector<u32>& src = kv.second;
        for (int q = 0; q < 1024; q++) {
            if (!(src[q] & 0x80)) continue;
            if (!dst) { auto it = world.find(kv.first); if (it == world.end()) it = world.emplace(kv.first, std::vector<u32>(2048, 0)).first; dst = &it->second; }
            const u32 d = (*dst)[q];
            if ((d & 0x80) && !(d & 0x20)) continue;              // the player's own view is never replaced
            if ((d & 0x80) && (src[q] >> 8) < s_seen_floor) continue;   // an older look than the one already offered
            (*dst)[q] = (src[q] & 0x9F) | 0x20u | 0xFFFFFF00u;    // predicted; the live map wins where it is fresh
            (*dst)[q + 1024] = src[q + 1024];
        }
    }
}

static inline u64 block_hash(const u8* v) {
    u64 h = 1469598103934665603ULL;
    for (int i = 0; i < 64; i++) { h ^= v[i]; h *= 1099511628211ULL; }
    return h;
}

bool Machine::wide_relocate(int rsx, int rsy) {
    // the visible background of this frame (colour index + palette), only rows with the camera's scroll
    static thread_local u8 val[160 * 144];
    static thread_local u8 ok[160 * 144];
    int valid = 0;
    for (int y = 0; y < 144; y++) {
        bool row = wline_scx[y] == rsx && wline_scy[y] == rsy;
        for (int x = 0; x < 160; x++) {
            int i = y * WIDE_W + WIDE_X0 + x;
            bool g = row && !(wflags[i] & 3);
            ok[y * 160 + x] = g; val[y * 160 + x] = wbg[i] & 0x1F;
            valid += g;
        }
    }
    if (valid < 160 * 144 / 3) return false;
    // index the stored places by 8x8 blocks (only blocks with some detail: plain areas match anywhere)
    for (auto& sc : wide_scenes) {
        if (sc.indexed) continue;
        sc.indexed = true;
        u8 b[64];
        for (auto& kv : sc.world) {
            int cx = (int)(int32_t)(u32)(kv.first >> 32), cy = (int)(int32_t)(u32)kv.first;
            const std::vector<u32>& c = kv.second;
            for (int by = 0; by < 4; by++) for (int bx = 0; bx < 4; bx++) {
                bool full = true, flat = true;
                for (int k = 0; k < 64 && full; k++) {
                    u32 cell = c[(by * 8 + k / 8) * 32 + bx * 8 + (k & 7)];
                    if (!(cell & 0x80)) full = false;
                    b[k] = cell & 0x1F;
                    if (b[k] != b[0]) flat = false;
                }
                if (!full || flat) continue;
                u64 pos = ((u64)(u32)(cx * 32 + bx * 8) << 32) | (u32)(cy * 32 + by * 8);
                auto& v = sc.index[block_hash(b)];
                if (v.size() < 64) v.push_back(pos);
            }
        }
    }
    // every screen block, at every alignment, votes for the place and offset it would come from
    int best_scene = -1; u64 best_off = 0; int best_votes = 0;
    for (size_t si = 0; si < wide_scenes.size(); si++) {
        WideScene& sc = wide_scenes[si];
        if (sc.index.empty()) continue;
        std::unordered_map<u64, int> votes;
        u8 b[64];
        for (int ay = 0; ay < 8; ay++) for (int ax = 0; ax < 8; ax++)
            for (int sy = ay; sy + 8 <= 144; sy += 8) for (int sx = ax; sx + 8 <= 160; sx += 8) {
                bool good = true, flat = true;
                for (int k = 0; k < 64 && good; k++) {
                    int p = (sy + k / 8) * 160 + sx + (k & 7);
                    if (!ok[p]) good = false;
                    b[k] = val[p];
                    if (b[k] != b[0]) flat = false;
                }
                if (!good || flat) continue;
                auto it = sc.index.find(block_hash(b));
                if (it == sc.index.end()) continue;
                for (u64 pos : it->second) {
                    int wx = (int)(int32_t)(u32)(pos >> 32) - sx, wy = (int)(int32_t)(u32)pos - sy;
                    ++votes[((u64)(u32)wx << 32) | (u32)wy];
                }
            }
        // repeating scenery matches at several offsets: among the strongest, the one nearest to where the
        // player left this place wins
        int top = 0;
        for (auto& kv : votes) top = std::max(top, kv.second);
        if (top < 8 || top <= best_votes) continue;
        // (memory loaded from the save folder: the spot with the most remembered scenery around the screen)
        long long best_d = 0; u64 off = 0; bool have = false;
        for (auto& kv : votes) {
            if (kv.second * 10 < top * 9) continue;
            int wx = (int)(int32_t)(u32)(kv.first >> 32), wy = (int)(int32_t)(u32)kv.first;
            long long d;
            if (!sc.imported) d = (long long)(wx - sc.cam_x) * (wx - sc.cam_x) + (long long)(wy - sc.cam_y) * (wy - sc.cam_y);
            else {
                int known = 0;
                for (int cy = (wy - 64) >> 5; cy <= (wy + 208) >> 5; cy++)
                    for (int cx = (wx - 96) >> 5; cx <= (wx + 256) >> 5; cx++) {
                        auto it = sc.world.find(((u64)(u32)cx << 32) | (u32)cy);
                        if (it == sc.world.end()) continue;
                        for (int q = 0; q < 1024; q += 16) known += (it->second[q] & 0x80) != 0;
                    }
                d = -known;
            }
            if (!have || d < best_d) { best_d = d; off = kv.first; have = true; }
        }
        best_votes = top; best_off = off; best_scene = (int)si;
    }
    if (best_scene < 0 || best_votes < 8) return false;
    // check the whole screen against that place before trusting it
    WideScene& sc = wide_scenes[best_scene];
    int ox = (int)(int32_t)(u32)(best_off >> 32), oy = (int)(int32_t)(u32)best_off;
    int same = 0, overlap = 0;
    for (int y = 0; y < 144; y += 2) for (int x = 0; x < 160; x += 2) {
        if (!ok[y * 160 + x]) continue;
        int wx = ox + x, wy = oy + y;
        auto it = sc.world.find(((u64)(u32)(wx >> 5) << 32) | (u32)(wy >> 5));
        if (it == sc.world.end()) continue;
        u32 c = it->second[(size_t)((wy & 31) * 32 + (wx & 31))];
        if (!(c & 0x80)) continue;
        overlap++;
        same += (c & 0x1F) == val[y * 160 + x];
    }
    if (overlap < 1500 || same < overlap * 97 / 100) return false;
    // welcome back: this place's memory becomes the current one, with the camera where the screen is
    world.swap(sc.world);                              // (what was seen since the change is the same place)
    wide_scenes.erase(wide_scenes.begin() + best_scene);
    world_x = ox; world_y = oy;
    world_ref_scx = rsx; world_ref_scy = rsy;
    reloc_count++;
    return true;
}

} // namespace gb

namespace gb {
void WideScout::step(Machine& m, int frames_per_call) {
    if (!m.widescreen || !m.wide_memory) return;
    if (!s) { s = std::make_unique<Machine>(); std::string err; if (!s->load_rom(m.cart.rom, err)) { s.reset(); return; } }
    if (m.wide_cuts != main_cuts) { main_cuts = m.wide_cuts; dead = true; wait = 20; }       // new scene: let it settle
    if (wait > 0) { wait--; return; }
    // the camera turned back: what the copy is looking at is no longer ahead
    if (!dead && ((hold_x && m.wide_dir_x && m.wide_dir_x != hold_x && (u32)m.frames - m.wide_moved_x < 8) ||
                  (hold_y && m.wide_dir_y && m.wide_dir_y != hold_y && (u32)m.frames - m.wide_moved_y < 8))) dead = true;
    if (dead || age >= horizon) {
        if (m.world_ref_scx < 0) return;                       // no camera yet
        if (!s->load_state(m.save_state())) return;
        s->widescreen = true; s->wide_edge = 1; s->wide_memory = true; s->wide_extend = false; s->wide_places = false;
        s->wide_smart = false; s->wide_sprites = false; s->link = nullptr;
        s->world_clear(); s->wide_scenes.clear();
        s->world_x = m.world_x; s->world_y = m.world_y; s->world_ref_scx = m.world_ref_scx; s->world_ref_scy = m.world_ref_scy;
        s->lcd_was_off = false; s->frame_map_writes = 0;
        own_cuts = s->wide_cuts; age = 0; lead = 0; dead = false;
        const u32 t = (u32)m.frames;
        hold_x = t - m.wide_moved_x < 60 ? m.wide_dir_x : 0;
        hold_y = t - m.wide_moved_y < 60 ? m.wide_dir_y : 0;
    }
    // hold the direction the camera moved in during the last second
    u8 b = m.buttons;
    if (hold_x) b = (u8)((b & ~(BTN_LEFT | BTN_RIGHT)) | (hold_x > 0 ? BTN_RIGHT : BTN_LEFT));
    if (hold_y) b = (u8)((b & ~(BTN_UP | BTN_DOWN)) | (hold_y > 0 ? BTN_DOWN : BTN_UP));
    const int n = lead < max_lead ? frames_per_call : 1;      // get ahead, then keep pace
    lead += n - 1;
    for (int i = 0; i < n && !dead; i++) {
        s->buttons = b;
        s->run_frame(nullptr);
        s->apu->out.clear();
        age++;
        if (s->wide_cuts != own_cuts) dead = true;             // it left this scene: nothing more to learn
    }
    if (!dead) m.wide_merge_predicted(*s);
    else wait = 30;
}
// ------------------------------------------------------------------ run-ahead
// (the raw copy of the first part also carries the unused bytes between fields, so saved states stay byte-identical)
void Machine::ahead_drop_sound() { apu->out.clear(); }

void Machine::ahead_keep_apu(RunAhead& ra) {
    if (!ra.apu) ra.apu = std::make_shared<Apu>(*apu); else *ra.apu = *apu;
    memcpy((void*)ra.apu.get(), (const void*)apu, offsetof(Apu, out));
}

void Machine::ahead_end(RunAhead& ra) {
    if (!ra.active) return;
    ra.active = false;
    load_state(ra.state, true);
    if (ra.apu) { *apu = *ra.apu; memcpy((void*)apu, (const void*)ra.apu.get(), offsetof(Apu, out)); }   // the sound continues exactly where it was
    frames = ra.frames;
    memcpy(map_col_frame, ra.mcf, sizeof ra.mcf); memcpy(map_row_frame, ra.mrf, sizeof ra.mrf);
    memcpy(map_col_behind, ra.mcb, sizeof ra.mcb); memcpy(map_row_behind, ra.mrb, sizeof ra.mrb);
    if (ra.tiles.size() == sizeof map_tile_wx + sizeof map_tile_wy + sizeof map_tile_frame + sizeof scene_start) {
        const u8* t = ra.tiles.data();
        memcpy(map_tile_wx, t, sizeof map_tile_wx); t += sizeof map_tile_wx;
        memcpy(map_tile_wy, t, sizeof map_tile_wy); t += sizeof map_tile_wy;
        memcpy(map_tile_frame, t, sizeof map_tile_frame); t += sizeof map_tile_frame;
        memcpy(&scene_start, t, sizeof scene_start);
    }
}

// the future picture (fb) goes in the middle of the real wide / extended picture; the sides move with the
// scroll made meanwhile (a few pixels), so they line up with it
void Machine::ahead_paste(int sx0, int sy0) {
    int dx = (int)(s8)(u8)(io[0x43] - sx0), dy = (int)(s8)(u8)(io[0x42] - sy0);
    if (std::abs(dx) > 8 || std::abs(dy) > 8 || !(io[0x40] & 0x80)) dx = dy = 0;   // a jump or a new scene: no shift
    if (dx) {
        static thread_local std::vector<u32> row; static thread_local std::vector<u8> frow;
        row.resize(WIDE_W); frow.resize(WIDE_W);
        for (int y = 0; y < 144; y++) {
            u32* w = &fbw[y * WIDE_W]; u8* f = &wflags[y * WIDE_W];
            for (int x = 0; x < WIDE_W; x++) { int sx = std::min(std::max(x + dx, 0), WIDE_W - 1); row[x] = w[sx]; frow[x] = f[sx]; }
            memcpy(w, row.data(), WIDE_W * 4); memcpy(f, frow.data(), WIDE_W);
        }
    }
    for (int y = 0; y < 144; y++) memcpy(&fbw[y * WIDE_W + WIDE_X0], &fb[y * 160], 160 * 4);
    if (!wide_extend || fbx.size() != (size_t)XW * XH) return;
    if (dx || dy) {
        static thread_local std::vector<u32> px; static thread_local std::vector<u8> kn;
        px.assign(fbx.begin(), fbx.end()); kn.assign(xknown.begin(), xknown.end());
        for (int y = 0; y < XH; y++) for (int x = 0; x < XW; x++) {
            int sx = x + dx, sy = y + dy;
            bool in = sx >= 0 && sx < XW && sy >= 0 && sy < XH;
            fbx[(size_t)y * XW + x] = in ? px[(size_t)sy * XW + sx] : 0xFF000000u;
            xknown[(size_t)y * XW + x] = in ? kn[(size_t)sy * XW + sx] : 0;
        }
    }
    for (int y = 0; y < 144; y++) {
        memcpy(&fbx[(size_t)(y + XY0) * XW + XX0], &fb[y * 160], 160 * 4);
        memset(&xknown[(size_t)(y + XY0) * XW + XX0], 255, 160);
    }
}

} // namespace gb
