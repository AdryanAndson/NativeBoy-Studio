#include "analysis.h"
#include "../core/optable.h"
#include <map>
#include "../core/autoplay.h"
#include "../core/apu.h"
#include <algorithm>
#include <memory>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>

namespace nb {

std::string hex4(unsigned v) { char b[8]; snprintf(b, sizeof b, "%04X", v & 0xFFFF); return b; }
std::string hex2(unsigned v) { char b[8]; snprintf(b, sizeof b, "%02X", v & 0xFF); return b; }

int Analysis::instr_len(u32 off) const {
    if (off >= rom.size()) return 1;
    u8 op = rom[off];
    return op == 0xCB ? 2 : op_info(op).len;
}

// ---------------------------------------------------------------------------------------------
// symbols
static std::string sanitize_ident(const std::string& in) {
    std::string o;
    for (char c : in) {
        unsigned char u = (unsigned char)c;
        if (isalnum(u) || c == '_' || c == '#' || c == '@') o += c;
        else if (c == '.' || c == ':' || c == '-') o += '_';
    }
    if (o.empty()) return o;
    if (isdigit((unsigned char)o[0]) || o[0] == '#' || o[0] == '@') o = "_" + o;
    return o;
}

int load_sym_file(const std::string& text, size_t rom_size, Symbols& out) {
    int count = 0;
    std::set<std::string> used;
    for (auto& kv : out.rom) used.insert(kv.second);
    for (auto& kv : out.ram) used.insert(kv.second);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        p = e + 1;
        size_t sc = l.find(';');
        if (sc != std::string::npos) l = l.substr(0, sc);
        unsigned bk = 0, ad = 0; char nm[256] = {0};
        if (sscanf(l.c_str(), " %x:%x %255s", &bk, &ad, nm) != 3) continue;
        std::string name = sanitize_ident(nm);
        if (name.empty() || ad > 0xFFFF) continue;
        if (used.count(name)) {   // RGBDS needs unique names
            for (int k = 2;; k++) { std::string t = name + "_" + std::to_string(k); if (!used.count(t)) { name = t; break; } }
        }
        if (ad < 0x8000) {
            u32 off = ad < 0x4000 ? ad : Analysis::off_of(bk == 0 ? 1 : (int)bk, (u16)ad);
            if (off >= rom_size || out.rom.count(off)) continue;
            out.rom[off] = name;
        } else {
            if (out.ram.count((u16)ad) || !hw_register_name((u16)ad).empty()) continue;
            out.ram[(u16)ad] = name;
        }
        used.insert(name);
        count++;
    }
    return count;
}

std::string hw_register_name(u16 a) {
    static const struct { u16 a; const char* n; } regs[] = {
        {0xFF00, "rP1"}, {0xFF01, "rSB"}, {0xFF02, "rSC"}, {0xFF04, "rDIV"}, {0xFF05, "rTIMA"}, {0xFF06, "rTMA"}, {0xFF07, "rTAC"},
        {0xFF0F, "rIF"}, {0xFF10, "rNR10"}, {0xFF11, "rNR11"}, {0xFF12, "rNR12"}, {0xFF13, "rNR13"}, {0xFF14, "rNR14"},
        {0xFF16, "rNR21"}, {0xFF17, "rNR22"}, {0xFF18, "rNR23"}, {0xFF19, "rNR24"}, {0xFF1A, "rNR30"}, {0xFF1B, "rNR31"},
        {0xFF1C, "rNR32"}, {0xFF1D, "rNR33"}, {0xFF1E, "rNR34"}, {0xFF20, "rNR41"}, {0xFF21, "rNR42"}, {0xFF22, "rNR43"},
        {0xFF23, "rNR44"}, {0xFF24, "rNR50"}, {0xFF25, "rNR51"}, {0xFF26, "rNR52"}, {0xFF40, "rLCDC"}, {0xFF41, "rSTAT"},
        {0xFF42, "rSCY"}, {0xFF43, "rSCX"}, {0xFF44, "rLY"}, {0xFF45, "rLYC"}, {0xFF46, "rDMA"}, {0xFF47, "rBGP"},
        {0xFF48, "rOBP0"}, {0xFF49, "rOBP1"}, {0xFF4A, "rWY"}, {0xFF4B, "rWX"}, {0xFF4D, "rKEY1"}, {0xFF4F, "rVBK"},
        {0xFF51, "rHDMA1"}, {0xFF52, "rHDMA2"}, {0xFF53, "rHDMA3"}, {0xFF54, "rHDMA4"}, {0xFF55, "rHDMA5"}, {0xFF56, "rRP"},
        {0xFF68, "rBCPS"}, {0xFF69, "rBCPD"}, {0xFF6A, "rOCPS"}, {0xFF6B, "rOCPD"}, {0xFF70, "rSVBK"}, {0xFFFF, "rIE"}};
    for (auto& r : regs) if (r.a == a) return r.n;
    return "";
}

// ---------------------------------------------------------------------------------------------
// Indirect calls through helper routines (the way most games call code in other banks):
//   CallHL:  jp hl                                  -> the target is hl
//   FarCall: [ld a, r] / ld [$2000-$3FFF], a / ... / jp hl | call CallHL | rst (CallHL)
//                                                    -> the target is hl in the bank held by r (or a)
// The descent keeps the constants loaded into the registers (and pushed on the stack) inside a block,
// so "ld a, BANK / ld hl, Func / call FarCall" and "ld de, Func / push de / ret" reveal their targets
// without playing the game.
enum HelperKind { HK_NONE = 0, HK_JPHL = 1, HK_FAR = 2 };
struct Helper { int kind = HK_NONE; int bank_reg = 7; };   // bank_reg: 0..7 = b c d e h l - a

// register written by an opcode (bit mask over b c d e h l - a), or -1 = unknown effect (clear all)
static int written_regs(const std::vector<u8>& rom, u32 off) {
    const u8 op = rom[off];
    auto bit = [](int r) { return r == 6 ? 0 : 1 << r; };
    if (op >= 0x40 && op <= 0x7F) return op == 0x76 ? 0 : bit((op >> 3) & 7);
    if ((op & 0xC7) == 0x06 || (op & 0xC7) == 0x04 || (op & 0xC7) == 0x05) return bit((op >> 3) & 7);
    switch (op) {
    case 0x01: case 0x03: case 0x0B: case 0xC1: return bit(0) | bit(1);
    case 0x11: case 0x13: case 0x1B: case 0xD1: return bit(2) | bit(3);
    case 0x21: case 0x23: case 0x2B: case 0xE1: case 0x09: case 0x19: case 0x29: case 0x39: case 0xF8: return bit(4) | bit(5);
    case 0x22: case 0x32: return bit(4) | bit(5);
    case 0x2A: case 0x3A: return bit(4) | bit(5) | bit(7);
    case 0xF1: return bit(7);
    case 0x0A: case 0x1A: case 0xF0: case 0xF2: case 0xFA: case 0x07: case 0x0F: case 0x17: case 0x1F: case 0x27: case 0x2F: return bit(7);
    case 0x00: case 0x02: case 0x12: case 0x08: case 0x31: case 0x33: case 0x3B: case 0x34: case 0x35: case 0x36: case 0x37: case 0x3F:
    case 0xE0: case 0xE2: case 0xEA: case 0xF3: case 0xFB: case 0xC5: case 0xD5: case 0xE5: case 0xF5: case 0xE8: case 0xF9: case 0xFE: case 0x10:
        return 0;
    case 0xCB: {
        if (off + 1 >= rom.size()) return -1;
        const u8 c = rom[off + 1];
        if (c >= 0x40 && c <= 0x7F) return 0;               // bit n, r
        return bit(c & 7);
    }
    default: break;
    }
    if (op >= 0x80 && op <= 0xB7) return bit(7);              // add/adc/sub/sbc/and/xor/or (cp does not write)
    if (op >= 0xB8 && op <= 0xBF) return 0;
    if (op == 0xC6 || op == 0xCE || op == 0xD6 || op == 0xDE || op == 0xE6 || op == 0xEE || op == 0xF6) return bit(7);
    return -1;
}

// A routine that only selects a ROM bank and returns: "ld a, r / ld [$2000], a / ret" (it may also keep
// the bank in a variable). src = register of the caller holding the bank (0..7 = b c d e h l - a);
// wmask = registers it changes.
struct BankSetter { bool ok = false; int src = -1; int wmask = 0; };
static BankSetter bank_setter(const Analysis& a, u32 off) {
    BankSetter r;
    const size_t n = a.rom.size();
    const u32 bank_end = (off / 0x4000 + 1) * 0x4000;
    int sym[8] = {0, 1, 2, 3, 4, 5, -1, 7};
    for (int i = 0; i < 16 && off < n && off < bank_end; i++) {
        const u8 op = a.rom[off];
        const OpInfo& oi = op_info(op);
        const int len = op == 0xCB ? 2 : oi.len;
        const u16 n16 = len == 3 && off + 2 < n ? (u16)(a.rom[off + 1] | (a.rom[off + 2] << 8)) : 0;
        if ((oi.kind == OK_RET && op == 0xC9) || oi.kind == OK_RETI) { r.ok = r.src >= 0; return r; }   // (reti: also turns interrupts back on)
        if (oi.kind != OK_NORMAL && oi.kind != OK_EI && oi.kind != OK_DI) return r;
        if (op >= 0x40 && op <= 0x7F && op != 0x76 && (op & 7) != 6 && ((op >> 3) & 7) != 6) { sym[(op >> 3) & 7] = sym[op & 7]; r.wmask |= 1 << ((op >> 3) & 7); }
        else if (op == 0xEA && n16 >= 0x2000 && n16 < 0x4000) r.src = sym[7];
        else {
            const int w = written_regs(a.rom, off);
            if (w < 0) return r;
            for (int k = 0; k < 8; k++) if (w & (1 << k)) sym[k] = -1;
            r.wmask |= w;
        }
        off += len;
    }
    return r;
}

static Helper classify_helper(const Analysis& a, u32 off, std::map<u32, Helper>& cache, int depth = 0) {
    auto it = cache.find(off);
    if (it != cache.end()) return it->second;
    Helper h;
    cache[off] = h;                                           // (guards against loops)
    const size_t n = a.rom.size();
    if (off >= n) return h;
    if (a.rom[off] == 0xE9) { h.kind = HK_JPHL; cache[off] = h; return h; }
    // a far-call routine: symbolic registers (which register of the caller each one holds now), until the
    // code at hl is jumped to / called with a bank selected from one of the caller's registers
    int sym[8] = {0, 1, 2, 3, 4, 5, -1, 7};
    int bank_src = -1;
    std::vector<std::pair<int, int>> stk;                     // pushed register pairs (symbols)
    auto hl_intact = [&]() { return sym[4] == 4 && sym[5] == 5; };
    const u32 bank_end = (off / 0x4000 + 1) * 0x4000;
    u32 p = off;
    for (int i = 0; i < 24 && p < n && p < bank_end; i++) {
        const u8 op = a.rom[p];
        const OpInfo& oi = op_info(op);
        const int len = op == 0xCB ? 2 : oi.len;
        const u16 n16 = len == 3 && p + 2 < n ? (u16)(a.rom[p + 1] | (a.rom[p + 2] << 8)) : 0;
        if (oi.kind == OK_JP_HL) { if (bank_src >= 0 && hl_intact()) { h.kind = HK_FAR; h.bank_reg = bank_src; } break; }
        if (oi.kind == OK_CALL || oi.kind == OK_RST || oi.kind == OK_JP) {
            const u16 t = oi.kind == OK_RST ? (u16)oi.rst : n16;
            if (t >= 0x4000 || depth >= 2) break;
            if (a.rom[t] == 0xE9 || classify_helper(a, t, cache, depth + 1).kind == HK_JPHL) {
                if (bank_src >= 0 && hl_intact()) { h.kind = HK_FAR; h.bank_reg = bank_src; }
                break;
            }
            if (oi.kind == OK_JP) break;
            const BankSetter bs = bank_setter(a, t);           // "call SetBank": the bank comes from its register
            if (!bs.ok) break;
            bank_src = bs.src >= 0 ? sym[bs.src] : -1;
            for (int k = 0; k < 8; k++) if (bs.wmask & (1 << k)) sym[k] = -1;
            p += len;
            continue;
        }
        if (oi.kind == OK_RET || oi.kind == OK_RETI || oi.kind == OK_JR || oi.kind == OK_JR_CC || oi.kind == OK_JP_CC || oi.kind == OK_CALL_CC || oi.kind == OK_ILLEGAL) break;
        if (op >= 0x40 && op <= 0x7F && op != 0x76 && (op & 7) != 6 && ((op >> 3) & 7) != 6) sym[(op >> 3) & 7] = sym[op & 7];
        else if (op == 0xEA && n16 >= 0x2000 && n16 < 0x4000) bank_src = sym[7];
        else if (op == 0xC5 || op == 0xD5 || op == 0xE5) { const int r = op == 0xC5 ? 0 : op == 0xD5 ? 2 : 4; stk.push_back({sym[r], sym[r + 1]}); }
        else if (op == 0xF5) stk.push_back({sym[7], -1});
        else if (op == 0xC1 || op == 0xD1 || op == 0xE1 || op == 0xF1) {
            std::pair<int, int> v = {-1, -1};
            if (!stk.empty()) { v = stk.back(); stk.pop_back(); }
            if (op == 0xF1) sym[7] = v.first;
            else { const int r = op == 0xC1 ? 0 : op == 0xD1 ? 2 : 4; sym[r] = v.first; sym[r + 1] = v.second; }
        } else {
            const int w = written_regs(a.rom, p);
            if (w < 0) break;
            for (int k = 0; k < 8; k++) if (w & (1 << k)) sym[k] = -1;
        }
        p += len;
    }
    cache[off] = h;
    return h;
}

// recursive descent. "no_fall" = call sites followed by data (jump tables), not code.
static void descend(Analysis& a, const std::vector<u8>* trace, const std::set<u32>& no_fall, const std::set<u32>& more_entries) {
    size_t n = a.rom.size();
    a.fl.assign(n, 0);
    a.indirect.clear(); a.unresolved_far.clear(); a.illegal.clear();
    a.n_instr = a.n_overlap = 0;

    struct Work { u32 off; int known_bank; };
    std::deque<Work> work;
    auto add = [&](u32 off, u8 flag, int kb) {
        if (off >= n) return;
        a.fl[off] |= flag;
        if (!(a.fl[off] & AF_START)) work.push_back({off, kb});
    };
    add(0x100, AF_ENTRY, -1);
    for (u16 v = 0x40; v <= 0x60; v += 8) if (a.rom[v] != 0xFF) add(v, AF_ENTRY, -1);
    if (trace)
        for (size_t i = 0; i < n && i < trace->size(); i++)
            if ((*trace)[i]) add((u32)i, AF_TRACED, -1);
    for (u32 o : a.extra_entries) add(o, AF_JUMP, -1);
    for (u32 o : more_entries) add(o, AF_JUMP, -1);

    std::map<u32, Helper> helpers;
    int mbc_mask = a.info.mbc == 1 ? 0x1F : a.info.mbc == 2 ? 0x0F : a.info.mbc == 3 ? 0x7F : 0x1FF;
    // addresses loaded as constants ("ld bc, Handler / call SetHandler"): maybe code kept in a variable
    std::vector<std::pair<u32, int>> code_ptrs;
    // a routine that ends in "reti" (it switches banks with interrupts off): the code after the call is
    // resumed by the dispatcher, so it must be an entry point
    auto ends_in_reti = [&](u32 t) {
        const u32 be = (t / 0x4000 + 1) * 0x4000;
        for (int i = 0; i < 40 && t < n && t < be; i++) {
            const u8 o2 = a.rom[t];
            const OpInfo& k = op_info(o2);
            if (k.kind == OK_RETI) return true;
            if (k.kind == OK_RET || k.kind == OK_JP || k.kind == OK_JR || k.kind == OK_JP_HL || k.kind == OK_ILLEGAL) return false;
            t += o2 == 0xCB ? 2 : k.len;
        }
        return false;
    };
    for (int round = 0; round < 6; round++) {
    while (!work.empty()) {
        Work w = work.front(); work.pop_front();
        u32 off = w.off;
        int known_bank = w.known_bank;
        int last_ld_a = -1;
        int reg[8]; for (int& r : reg) r = -1;                 // known constants of b c d e h l - a (-1 = unknown)
        std::vector<int> stack;                                // known words pushed in this block (-1 = unknown)
        auto hl = [&]() { return reg[4] >= 0 && reg[5] >= 0 ? (reg[4] << 8) | reg[5] : -1; };
        while (off < n && !(a.fl[off] & AF_START)) {
            int bank = Analysis::bank_of(off);
            u16 addr = Analysis::addr_of(off);
            u8 op = a.rom[off];
            const OpInfo& oi = op_info(op);
            int len = op == 0xCB ? 2 : oi.len;
            if ((off % 0x4000) + len > 0x4000) break;      // would run past the bank
            if (a.fl[off] & AF_BODY) a.n_overlap++;
            a.fl[off] |= AF_START;
            for (int i = 0; i < len; i++) a.fl[off + i] |= AF_BODY;
            a.n_instr++;
            u16 n16 = len == 3 ? (u16)(a.rom[off + 1] | (a.rom[off + 2] << 8)) : 0;
            u8 n8 = len >= 2 ? a.rom[off + 1] : 0;
            // sound register writes
            if ((op == 0xE0 && n8 >= 0x10 && n8 < 0x40) || (op == 0xEA && n16 >= 0xFF10 && n16 < 0xFF40)) a.fl[off] |= AF_SOUND;
            // track "ld a, n8 / ld [$2000-$3FFF], a" to guess bank switches
            if (op == 0x3E) last_ld_a = n8;
            else if (op == 0xEA && n16 >= 0x2000 && n16 < 0x4000 && last_ld_a >= 0) {
                int bk = last_ld_a;
                if (a.info.mbc == 1) { bk &= 0x1F; if (!bk) bk = 1; }
                if (a.info.mbc == 3) { bk &= 0x7F; if (!bk) bk = 1; }
                if (a.info.mbc == 2) { bk &= 0x0F; if (!bk) bk = 1; }
                known_bank = bk;
            } else if (op == 0xAF || (op >= 0x78 && op <= 0x7F) || op == 0xF0 || op == 0xFA || op == 0x1A || op == 0x0A || op == 0x2A || op == 0x3A || op == 0xF1) {
                last_ld_a = -1;
            }
            auto target = [&](u16 t, u8 flag) {
                if (t < 0x4000) add(t, flag, -1);
                else if (t < 0x8000) {
                    if (bank != 0) add(Analysis::off_of(bank, t), flag, -1);
                    else if (a.info.mbc == 0 && n >= 0x8000) add(Analysis::off_of(1, t), flag, -1);   // no MBC: bank 1 is fixed
                    else if (known_bank > 0 && (size_t)known_bank * 0x4000 < n) add(Analysis::off_of(known_bank, t), flag, -1);
                    else a.unresolved_far.push_back({off, t});
                }
            };
            // a call into a helper: CallHL (target = hl) or FarCall (target = hl in the bank of a register)
            auto via_helper = [&](u16 dest) {
                if (dest >= 0x4000 && bank == 0) return;
                const u32 hoff = dest < 0x4000 ? dest : Analysis::off_of(bank, dest);
                const Helper hp = classify_helper(a, hoff, helpers);
                const int t = hl();
                // the callee returns here after an indirect jump: the native code needs an entry point
                if (hp.kind != HK_NONE && (oi.kind == OK_CALL || oi.kind == OK_CALL_CC || oi.kind == OK_RST)) add(off + len, AF_JUMP, known_bank);
                if (hp.kind == HK_JPHL && t >= 0) target((u16)t, AF_CALL);
                else if (hp.kind == HK_FAR && t >= 0) {
                    const int bv = reg[hp.bank_reg];
                    if (t < 0x4000) add((u32)t, AF_CALL, -1);
                    else if (t < 0x8000 && bv >= 0) {
                        int bk = bv & mbc_mask; if (!bk && a.info.mbc != 5) bk = 1;
                        if ((size_t)bk * 0x4000 < n) add(Analysis::off_of(bk, (u16)t), AF_CALL, bk);
                    }
                }
            };
            u32 next = off + len;
            bool stop = false;
            switch (oi.kind) {
            case OK_JP: target(n16, AF_JUMP); via_helper(n16); stop = true; break;
            case OK_JP_CC: target(n16, AF_JUMP); break;
            case OK_JR: target((u16)(addr + 2 + (s8)n8), AF_JUMP); stop = true; break;
            case OK_JR_CC: target((u16)(addr + 2 + (s8)n8), AF_JUMP); break;
            case OK_CALL: case OK_CALL_CC:
                target(n16, AF_CALL); via_helper(n16);
                if ((n16 < 0x4000 || bank != 0) && ends_in_reti(n16 < 0x4000 ? n16 : Analysis::off_of(bank, n16))) add(off + len, AF_JUMP, known_bank);
                if (no_fall.count(off)) stop = true;
                break;
            case OK_RST: add(oi.rst, AF_CALL, -1); via_helper((u16)oi.rst); if (no_fall.count(off)) stop = true; break;
            case OK_RET: case OK_RETI:
                // "ld de, Func / push de / ret": the pushed constant is where it goes
                // (the words pushed under it are return addresses for later "ret"s)
                if (op == 0xC9) for (int v : stack) if (v >= 0 && v < 0x8000) target((u16)v, AF_JUMP);
                stop = true; break;
            case OK_JP_HL: if (hl() >= 0) target((u16)hl(), AF_JUMP); a.indirect.push_back(off); stop = true; break;
            case OK_ILLEGAL: a.illegal.push_back(off); stop = true; break;
            default: break;
            }
            // (an "ld hl" followed by "add hl" indexes a table: data, not a routine)
            bool indexes = false;
            if (op == 0x21) for (u32 q = off + 3, k = 0; k < 4 && q < n; k++) { const u8 o2 = a.rom[q]; if (o2 == 0x09 || o2 == 0x19 || o2 == 0x29 || o2 == 0x2A || o2 == 0x7E) { indexes = true; break; } q += o2 == 0xCB ? 2 : op_info(o2).len; }
            if ((op == 0x01 || op == 0x11 || op == 0x21) && !indexes && n16 >= 0x150 && n16 < 0x8000) {
                if (n16 < 0x4000) code_ptrs.push_back({n16, -1});
                else if (bank != 0) code_ptrs.push_back({Analysis::off_of(bank, n16), bank});
            }
            // the constants after this instruction
            if (op == 0x06 || op == 0x0E || op == 0x16 || op == 0x1E || op == 0x26 || op == 0x2E || op == 0x3E) reg[(op >> 3) & 7] = n8;
            else if (op == 0x01) { reg[0] = n16 >> 8; reg[1] = n16 & 0xFF; }
            else if (op == 0x11) { reg[2] = n16 >> 8; reg[3] = n16 & 0xFF; }
            else if (op == 0x21) { reg[4] = n16 >> 8; reg[5] = n16 & 0xFF; }
            else if (op >= 0x40 && op <= 0x7F && op != 0x76 && (op & 7) != 6 && ((op >> 3) & 7) != 6) reg[(op >> 3) & 7] = reg[op & 7];
            else if (op == 0xAF) reg[7] = 0;
            else if (op == 0xC5 || op == 0xD5 || op == 0xE5) {
                const int r = op == 0xC5 ? 0 : op == 0xD5 ? 2 : 4;
                stack.push_back(reg[r] >= 0 && reg[r + 1] >= 0 ? (reg[r] << 8) | reg[r + 1] : -1);
            } else if (op == 0xF5) stack.push_back(-1);
            else if (op == 0xC1 || op == 0xD1 || op == 0xE1 || op == 0xF1) {
                const int v = stack.empty() ? -1 : stack.back();
                if (!stack.empty()) stack.pop_back();
                if (op == 0xF1) reg[7] = -1;
                else { const int r = op == 0xC1 ? 0 : op == 0xD1 ? 2 : 4; reg[r] = v >= 0 ? v >> 8 : -1; reg[r + 1] = v >= 0 ? v & 0xFF : -1; }
            } else if (oi.kind == OK_CALL || oi.kind == OK_CALL_CC || oi.kind == OK_RST) { for (int& r : reg) r = -1; stack.clear(); }
            else if (op == 0x31 || op == 0xF9 || op == 0xE8 || op == 0x33 || op == 0x3B) stack.clear();
            else {
                const int wr = written_regs(a.rom, off);
                if (wr < 0) { for (int& r : reg) r = -1; stack.clear(); }
                else for (int r = 0; r < 8; r++) if (wr & (1 << r)) reg[r] = -1;
            }
            if (stop) break;
            off = next;
            if (off < n && (a.fl[off] & AF_START)) break;
        }
    }
    // the constants that point right after the end of a known routine (ret / jp / jr / reti) start a
    // routine of their own: functions follow each other in the ROM. Anything else is taken as data.
    bool more = false;
    for (auto& cp : code_ptrs) {
        const u32 t = cp.first;
        if (t >= n || (a.fl[t] & (AF_START | AF_BODY)) || t < 1) continue;
        bool after_end = false;
        for (int k = 1; k <= 3 && !after_end; k++) {
            if (t < (u32)k) break;
            const u32 s0 = t - k;
            if (!(a.fl[s0] & AF_START) || s0 / 0x4000 != t / 0x4000) continue;
            const u8 o2 = a.rom[s0];
            const OpInfo& k2 = op_info(o2);
            if ((o2 == 0xCB ? 2 : k2.len) != k) continue;
            after_end = (k2.kind == OK_RET && o2 == 0xC9) || k2.kind == OK_RETI || k2.kind == OK_JP || k2.kind == OK_JR;   // (after "jp hl" there is usually a table)
        }
        if (!after_end) continue;
        // and it must decode cleanly up to its own end
        bool clean = false;
        int count = 0, moves = 0;                              // text decodes as "ld r, r" almost only
        const u32 be = (t / 0x4000 + 1) * 0x4000;
        u32 q = t;
        for (int i = 0; i < 400 && q < n && q < be; i++) {
            const u8 o2 = a.rom[q];
            const OpInfo& k2 = op_info(o2);
            const int l2 = o2 == 0xCB ? 2 : k2.len;
            count++; if (o2 >= 0x40 && o2 <= 0x7F) moves++;
            if (k2.kind == OK_ILLEGAL || q + l2 > be) break;
            if (i && (a.fl[q] & AF_BODY) && !(a.fl[q] & AF_START)) break;          // runs into the middle of an instruction
            if ((k2.kind == OK_RET && o2 == 0xC9) || k2.kind == OK_RETI || k2.kind == OK_JP || k2.kind == OK_JR || k2.kind == OK_JP_HL || (i && (a.fl[q] & AF_START))) { clean = true; break; }
            q += l2;
        }
        if (clean && moves * 2 <= count) { add(t, AF_JUMP, cp.second); more = true; }
    }
    code_ptrs.clear();
    if (!more) break;
    }
}

// A dispatcher pops its return address (= the table right after the call) and ends in "jp hl":
//   TableJump: add a / pop hl / ld e,a / ld d,0 / add hl,de / ld a,[hl+] / ld h,[hl] / ld l,a / jp hl
static bool is_dispatcher(const Analysis& a, u32 off) {
    size_t n = a.rom.size();
    bool popped = false;
    u32 bank_end = (off / 0x4000 + 1) * 0x4000;
    for (int i = 0; i < 24 && off < n && off < bank_end; i++) {
        u8 op = a.rom[off];
        const OpInfo& oi = op_info(op);
        if (op == 0xE1 && i < 4) popped = true;                 // pop hl
        if (op == 0xD1 && i < 4) popped = true;                 // pop de (variant using de as the pointer)
        if (oi.kind == OK_JP_HL) return popped;
        if (oi.kind == OK_RET || oi.kind == OK_RETI || oi.kind == OK_JP || oi.kind == OK_JR || oi.kind == OK_ILLEGAL ||
            oi.kind == OK_CALL || oi.kind == OK_RST || op == 0xC5 || op == 0xD5 || op == 0xE5 || op == 0xF5) return false;
        off += op == 0xCB ? 2 : oi.len;
    }
    return false;
}

void analyze(Analysis& a, const std::vector<u8>* trace, const std::vector<Machine::RamExec>* ram) {
    parse_header(a.rom, a.info);
    size_t n = a.rom.size();
    a.funcs.clear(); a.ram_code.clear(); a.jump_tables.clear(); a.table_words.clear(); a.dispatchers.clear();
    a.n_traced = 0;
    std::set<u32> no_fall, more;
    for (int pass = 0; pass < 6; pass++) {
        descend(a, trace, no_fall, more);
        // jump tables: data placed right after calls to a dispatcher routine
        bool changed = false;
        std::vector<JumpTable> found;
        std::map<u32, u32> words;
        for (u32 off = 0; off < n; off++) {
            if (!(a.fl[off] & AF_START)) continue;
            u8 op = a.rom[off];
            const OpInfo& oi = op_info(op);
            u32 tgt;
            if (oi.kind == OK_RST) tgt = oi.rst;
            else if (op == 0xCD) {
                u16 t = (u16)(a.rom[off + 1] | (a.rom[off + 2] << 8));
                if (t < 0x4000) tgt = t; else if (t < 0x8000 && off >= 0x4000) tgt = Analysis::off_of(Analysis::bank_of(off), t); else continue;
            } else continue;
            if (!a.dispatchers.count(tgt)) { if (!is_dispatcher(a, tgt)) continue; a.dispatchers.insert(tgt); }
            u32 p = off + (op == 0xCD ? 3 : 1);
            if (trace && p < trace->size() && (*trace)[p]) continue;      // execution continued after the call: not a table
            JumpTable jt; jt.call_off = off; jt.table_off = p;
            int bank = Analysis::bank_of(off);
            u32 bank_end = (off / 0x4000 + 1) * 0x4000;
            while (p + 1 < bank_end && p + 1 < n && jt.targets.size() < 256) {
                if (!jt.targets.empty() && (a.fl[p] & (AF_JUMP | AF_CALL | AF_ENTRY | AF_TRACED))) break;   // code starts here
                if (trace && ((*trace)[p] || (*trace)[p + 1])) break;
                u16 w = (u16)(a.rom[p] | (a.rom[p + 1] << 8));
                u32 to;
                if (w < 0x4000) to = w;
                else if (w < 0x8000 && bank != 0) to = Analysis::off_of(bank, w);
                else if (w < 0x8000 && a.info.mbc == 0 && n >= 0x8000) to = Analysis::off_of(1, w);
                else break;
                if (w < 0x0100 || to >= n) break;
                u8 top = a.rom[to];
                if (op_info(top).kind == OK_ILLEGAL || top == 0xFF) break;
                if (to >= jt.table_off && to <= p + 1) break;             // points into the table itself
                jt.targets.push_back(to);
                words[p] = to;
                p += 2;
            }
            if (jt.targets.empty()) continue;
            found.push_back(jt);
            if (!no_fall.count(off)) { no_fall.insert(off); changed = true; }
            for (u32 t : jt.targets) if (!more.count(t)) { more.insert(t); changed = true; }
        }
        // tables read through hl:  ld hl,Table / ...index... / ld a,[hl+] / ld h,[hl] / ld l,a / jp hl
        //                      or  ... / ld e,[hl] / inc hl / ld d,[hl] / ld l,e / ld h,d / jp hl  (also push de / ret variants not handled)
        for (u32 jo : a.indirect) {
            static const u8 pa[4] = {0x2A, 0x66, 0x6F, 0xE9}, pb[6] = {0x5E, 0x23, 0x56, 0x6B, 0x62, 0xE9}, pc_[6] = {0x5E, 0x23, 0x56, 0x62, 0x6B, 0xE9};
            u32 head = 0;
            if (jo >= 3 && !memcmp(&a.rom[jo - 3], pa, 4)) head = jo - 3;
            else if (jo >= 5 && (!memcmp(&a.rom[jo - 5], pb, 6) || !memcmp(&a.rom[jo - 5], pc_, 6))) head = jo - 5;
            else {
                // the same, with a few instructions that keep hl between "ld l,a" and "jp hl"
                // (e.g. "ld a,[hl+] / ld h,[hl] / ld l,a / ld a,[de] / inc de / jp hl")
                u32 q = jo;
                for (int g = 0; g < 4 && !head && q > 3; g++) {
                    u32 prev = 0;
                    for (int k = 1; k <= 3; k++) if (q >= (u32)k && (a.fl[q - k] & AF_START) && (a.rom[q - k] == 0xCB ? 2 : op_info(a.rom[q - k]).len) == k) { prev = q - k; break; }
                    if (!prev) break;
                    if (a.rom[prev] == 0x6F && prev >= 2 && a.rom[prev - 1] == 0x66 && a.rom[prev - 2] == 0x2A) { head = prev - 2; break; }
                    const int w = written_regs(a.rom, prev);
                    const OpKind kd = op_info(a.rom[prev]).kind;
                    if (w < 0 || (w & ((1 << 4) | (1 << 5))) || kd != OK_NORMAL || a.rom[prev] == 0x22 || a.rom[prev] == 0x32) break;
                    q = prev;
                }
                if (!head) continue;
            }
            // nearest "ld hl, nn" decoded shortly before
            int tbl = -1;
            for (u32 p = head; p + 24 > head && p > 0; p--) {
                if ((a.fl[p - 1] & AF_START) && a.rom[p - 1] == 0x21) { tbl = a.rom[p] | (a.rom[p + 1] << 8); break; }
                if (head - p > 24) break;
            }
            if (tbl < 0) continue;
            int bank = Analysis::bank_of(jo);
            u32 t0;
            if (tbl < 0x4000) t0 = (u32)tbl;
            else if (tbl < 0x8000 && bank != 0) t0 = Analysis::off_of(bank, (u16)tbl);
            else continue;
            JumpTable jt; jt.call_off = jo; jt.table_off = t0;
            u32 bank_end = (t0 / 0x4000 + 1) * 0x4000;
            for (u32 p = t0; p + 1 < bank_end && p + 1 < n && jt.targets.size() < 64; p += 2) {
                if (!jt.targets.empty() && (a.fl[p] & AF_START)) break;          // ran into code
                if (trace && ((*trace)[p] || (*trace)[p + 1])) break;
                u16 w = (u16)(a.rom[p] | (a.rom[p + 1] << 8));
                u32 to;
                if (w >= 0x0100 && w < 0x4000) to = w;
                else if (w >= 0x4000 && w < 0x8000 && bank != 0) to = Analysis::off_of(bank, w);
                else if (w >= 0x4000 && w < 0x8000 && a.info.mbc == 0 && n >= 0x8000) to = Analysis::off_of(1, w);
                else break;
                if (to >= n || op_info(a.rom[to]).kind == OK_ILLEGAL || a.rom[to] == 0xFF) break;
                jt.targets.push_back(to);
                words[p] = to;
            }
            if (jt.targets.size() < 2) { for (size_t k = 0; k < jt.targets.size(); k++) words.erase(t0 + 2 * (u32)k); continue; }
            found.push_back(jt);
            for (u32 t : jt.targets) if (!more.count(t)) { more.insert(t); changed = true; }
        }
        a.jump_tables = found;
        a.table_words = words;
        if (!changed) break;
    }
    // table bytes must not be shown as code that the descent may have decoded in an earlier pass
    // traced count
    for (size_t i = 0; i < n; i++) if ((a.fl[i] & AF_TRACED) && (a.fl[i] & AF_START)) a.n_traced++;

    // RAM code snippets. Code copied to RAM can change over time (several variants at the
    // same address), so instructions are grouped into byte-consistent snippets in the order
    // they were first executed. Each generated instruction re-checks its bytes at run time.
    if (ram && !ram->empty()) {
        std::vector<RamSnippet>& sn = a.ram_code;
        for (const auto& e : *ram) {
            int len = e.bytes[0] == 0xCB ? 2 : op_info(e.bytes[0]).len;
            int pick = -1;
            for (int k = (int)sn.size() - 1; k >= 0 && k >= (int)sn.size() - 8; k--) {
                RamSnippet& s = sn[k];
                int lo = s.addr, hi = s.addr + (int)s.bytes.size();
                if (e.pc + len < lo - 32 || e.pc > hi + 32) continue;
                bool ok = true;
                for (int i = 0; i < len && ok; i++) {
                    int rel = e.pc + i - lo;
                    if (rel >= 0 && rel < (int)s.bytes.size() && s.mask[rel] && s.bytes[rel] != e.bytes[i]) ok = false;
                }
                for (u16 st : s.starts) if (st == e.pc) ok = false;
                if (ok) { pick = k; break; }
            }
            if (pick < 0) { RamSnippet ns; ns.addr = e.pc; sn.push_back(ns); pick = (int)sn.size() - 1; }
            RamSnippet& s = sn[pick];
            if (e.pc < s.addr) {
                size_t shift = s.addr - e.pc;
                s.bytes.insert(s.bytes.begin(), shift, 0); s.mask.insert(s.mask.begin(), shift, 0);
                s.addr = e.pc;
            }
            size_t rel = e.pc - s.addr;
            if (s.bytes.size() < rel + len) { s.bytes.resize(rel + len, 0); s.mask.resize(rel + len, 0); }
            for (int i = 0; i < len; i++) { s.bytes[rel + i] = e.bytes[i]; s.mask[rel + i] = 1; }
            s.starts.push_back(e.pc);
        }
        for (auto& s : sn) std::sort(s.starts.begin(), s.starts.end());
    }

    // functions
    std::vector<u32> starts;
    for (size_t i = 0; i < n; i++) if ((a.fl[i] & AF_START) && (a.fl[i] & (AF_CALL | AF_ENTRY))) starts.push_back((u32)i);
    for (size_t k = 0; k < starts.size(); k++) {
        Function fn;
        fn.off = starts[k]; fn.bank = Analysis::bank_of(fn.off); fn.addr = Analysis::addr_of(fn.off);
        u32 end = (k + 1 < starts.size()) ? starts[k + 1] : (u32)n;
        u32 bank_end = (fn.off / 0x4000 + 1) * 0x4000;
        if (end > bank_end) end = bank_end;
        for (u32 o = fn.off; o < end; o++) if (a.fl[o] & AF_START) { fn.n_instr++; if (a.fl[o] & AF_SOUND) fn.sound = true; if (a.fl[o] & AF_TRACED) fn.traced = true; }
        char nm[32];
        if (a.fl[fn.off] & AF_ENTRY) {
            if (fn.off == 0x100) snprintf(nm, sizeof nm, "Inicio");
            else if (fn.off == 0x40) snprintf(nm, sizeof nm, "Int_VBlank");
            else if (fn.off == 0x48) snprintf(nm, sizeof nm, "Int_LCD");
            else if (fn.off == 0x50) snprintf(nm, sizeof nm, "Int_Timer");
            else if (fn.off == 0x58) snprintf(nm, sizeof nm, "Int_Serial");
            else if (fn.off == 0x60) snprintf(nm, sizeof nm, "Int_Joypad");
            else snprintf(nm, sizeof nm, "Entrada_%02X_%04X", fn.bank, fn.addr);
        } else snprintf(nm, sizeof nm, "%s_%02X_%04X", fn.sound ? "Som" : "Func", fn.bank, fn.addr);
        fn.name = nm;
        auto sy = a.sym.rom.find(fn.off);
        if (sy != a.sym.rom.end()) fn.name = sy->second;
        a.funcs.push_back(fn);
    }
}

bool run_discovery(const std::vector<u8>& rom, int frames, DiscoveryResult& out, std::string& err, std::function<bool(int)> progress, bool sgb, int explore) {
    auto mp = std::make_unique<Machine>();      // large: keep it off the worker thread's stack
    Machine& m = *mp;
    m.sgb_mode = sgb;
    if (!m.load_rom(rom, err)) return false;
    out.trace.assign(m.cart.rom.size(), 0);
    out.ram.clear();
    m.trace_bitmap = &out.trace;
    m.ram_trace = &out.ram;
    m.apu->start_vgm();
    u64 last_snapshot_gen = 0;
    auto vram_snap = [&]() {
        if (m.vram_write_gen == last_snapshot_gen || out.vram_snapshots.size() >= 24) return;
        last_snapshot_gen = m.vram_write_gen;
        std::vector<u8> s(0x4000 + 128 + 3);
        memcpy(&s[0], m.vram[0], 0x2000); memcpy(&s[0x2000], m.vram[1], 0x2000);
        memcpy(&s[0x4000], m.bgpal, 64); memcpy(&s[0x4040], m.objpal, 64);
        s[0x4080] = m.io[0x47]; s[0x4081] = m.io[0x48]; s[0x4082] = m.cgb ? 1 : 0;
        out.vram_snapshots.push_back(s);
    };
    auto covered = [&]() { size_t n = 0; for (u8 v : out.trace) n += v; return n + out.ram.size(); };
    // moments worth exploring from: the start of new screens / new code
    struct Seed { std::vector<u8> state; int tries = 0, wins = 0; };
    std::vector<Seed> seeds;
    size_t cov = 0;
    for (int f = 0; f < frames; f++) {
        m.buttons = autoplay_buttons((u64)f);
        m.run_frame(nullptr);
        m.apu->out.clear();
        if (f == 60 * 60) out.vgm = m.apu->stop_vgm(m.cart.info.title);   // first minute of audio as a music capture
        if (f % 120 == 119) vram_snap();
        if (explore > 0 && f % 300 == 299) {          // remember moments where new code was reached
            size_t c = covered();
            if (c > cov && seeds.size() < 48) { seeds.push_back({m.save_state()}); }
            cov = c;
        }
        if (progress && f % 60 == 0 && !progress(f)) { m.trace_bitmap = nullptr; m.ram_trace = nullptr; return true; }
        out.frames = f + 1;
    }
    if (m.apu->vgm_rec) out.vgm = m.apu->stop_vgm(m.cart.info.title);
    out.code_bytes_linear = covered();
    // ---- exploration: replay from saved moments with different button styles; keep the moments
    // that keep finding new code (a simple coverage-guided search, like a fuzzer)
    if (explore > 0) {
        if (seeds.empty()) seeds.push_back({m.save_state()});
        u32 rng = 0x9E3779B9u ^ (u32)rom.size();
        auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
        int done = 0;
        cov = covered();
        while (done < explore) {
            // choose a seed: mostly the productive ones, sometimes any
            // mostly keep going from where the last attempt ended (games need time to progress);
            // sometimes go back to a moment that was productive before and try another way
            size_t si = seeds.size() - 1;
            if (rnd() % 10 < 3) {
                double best = -1;
                for (size_t k = 0; k < seeds.size(); k++) {
                    double sc = (seeds[k].wins + 1.0) / (seeds[k].tries + 2.0) + (rnd() % 100) / 300.0;
                    if (sc > best) { best = sc; si = k; }
                }
            }
            Seed& sd = seeds[si];
            sd.tries++;
            if (!m.load_state(sd.state)) { seeds.erase(seeds.begin() + si); if (seeds.empty()) break; continue; }
            int style = rnd() % 5, len = 600 + rnd() % 900;
            u8 held = 0; int hold_left = 0;
            bool found = false;
            for (int f = 0; f < len && done < explore; f++, done++) {
                if (hold_left-- <= 0) {
                    static const u8 dirs[5] = {0, BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN};
                    held = dirs[rnd() % 5];
                    switch (style) {
                    case 0: if (rnd() % 3 == 0) held |= BTN_A; break;                        // walking around
                    case 1: held = (rnd() % 2 ? BTN_A : 0) | (rnd() % 6 == 0 ? BTN_START : 0); break;   // menus / dialogue
                    case 2: held |= (rnd() % 2 ? BTN_A : BTN_B); break;                      // action
                    default: held |= (u8)(rnd() & (BTN_A | BTN_B | BTN_SELECT | BTN_START)); break;      // anything
                    }
                    hold_left = 4 + rnd() % 40;
                }
                if (style == 4) m.buttons = autoplay_buttons((u64)(f + rnd() % 3), 7 + (u32)out.explore_runs);   // the usual automatic play, another seed
                else m.buttons = (style == 1 && (f & 8)) ? (u8)(held & ~BTN_A) : held;   // menus need A to be released between presses
                m.run_frame(nullptr);
                m.apu->out.clear();
                if (f % 120 == 119) vram_snap();
                if (f % 60 == 59) {
                    size_t c = covered();
                    if (c > cov) {
                        cov = c; found = true;
                        if (seeds.size() >= 64) { size_t w = 0; for (size_t k = 1; k + 8 < seeds.size(); k++) if (seeds[k].wins < seeds[w].wins) w = k; seeds.erase(seeds.begin() + w); }
                        seeds.push_back({m.save_state()});
                    }
                }
                if (progress && done % 60 == 0 && !progress(frames + done)) { explore = done; break; }
            }
            out.explore_runs++;
            if (found) { seeds[std::min(si, seeds.size() - 1)].wins++; out.explore_useful++; }
            // the end of this attempt becomes the newest place to continue from
            if (seeds.size() >= 64) seeds.erase(seeds.begin());
            seeds.push_back({m.save_state()});
        }
        out.explore_frames = done;
    }
    out.code_bytes_total = covered();
    m.trace_bitmap = nullptr; m.ram_trace = nullptr;
    return true;
}

} // namespace nb
