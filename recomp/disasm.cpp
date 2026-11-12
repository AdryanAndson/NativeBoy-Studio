#include "disasm.h"
#include "../core/optable.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace nb {

// UTF-8 paths (e.g. C:\\Users\\Saúde da Família\\...) must not go through the ANSI code page
static std::filesystem::path U8(const std::string& s) { return std::filesystem::u8path(s); }

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
    return s;
}

std::string var_name(const Analysis& a, u16 addr) {
    if (addr >= 0xFF00) { std::string h = hw_register_name(addr); if (!h.empty()) return h; }
    if (addr < 0x8000) return "";
    auto it = a.sym.ram.find(addr);
    return it == a.sym.ram.end() ? "" : it->second;
}

std::string format_instr(const u8* b, u16 addr, const std::function<std::string(u16)>& label_for,
                         const std::function<std::string(u16)>& var_for) {
    u8 op = b[0];
    if (op == 0xCB) return cb_info(b[1]).mnem;
    const OpInfo& oi = op_info(op);
    std::string s = oi.mnem;
    if (oi.kind == OK_ILLEGAL) return "db $" + hex2(op);
    if (oi.kind == OK_STOP && b[1] != 0) return "db $10, $" + hex2(b[1]);
    u8 n8 = b[1];
    u16 n16 = (u16)(b[1] | (b[2] << 8));
    if (s.find("{tgt}") != std::string::npos) {
        u16 t = (oi.kind == OK_JR || oi.kind == OK_JR_CC) ? (u16)(addr + 2 + (s8)n8) : n16;
        std::string lb = label_for ? label_for(t) : "";
        s = replace_all(s, "{tgt}", lb.empty() ? "$" + hex4(t) : lb);
    }
    s = replace_all(s, "{n8}", "$" + hex2(n8));
    if (var_for && (s.find("[{n16}]") != std::string::npos || ((op & 0xCF) == 0x01 && n16 >= 0xC000))) {
        std::string v = var_for(n16);
        if (!v.empty()) s = replace_all(s, "{n16}", v);
    }
    s = replace_all(s, "{n16}", "$" + hex4(n16));
    if (var_for && s.find("{a8}") != std::string::npos) {
        std::string v = var_for((u16)(0xFF00 | n8));
        if (!v.empty()) s = replace_all(s, "{a8}", v);
    }
    s = replace_all(s, "{a8}", "$FF" + hex2(n8));
    char e[16]; snprintf(e, sizeof e, "%d", (int)(s8)n8);
    s = replace_all(s, "{e8}", e);
    char spe[16]; snprintf(spe, sizeof spe, "%s %d", (s8)n8 < 0 ? "-" : "+", (s8)n8 < 0 ? -(int)(s8)n8 : (int)(s8)n8);
    s = replace_all(s, "{spe}", spe);
    return s;
}

std::string label_name(const Analysis& a, u32 off) {
    if (off >= a.fl.size() || !(a.fl[off] & AF_START)) return "";
    auto sy = a.sym.rom.find(off);
    if (sy != a.sym.rom.end()) return sy->second;
    u8 f = a.fl[off];
    int bank = Analysis::bank_of(off);
    u16 addr = Analysis::addr_of(off);
    char b[32];
    if (off == 0x100) return "Inicio";
    static const char* ints[5] = {"Int_VBlank", "Int_LCD", "Int_Timer", "Int_Serial", "Int_Joypad"};
    if (off >= 0x40 && off <= 0x60 && (off & 7) == 0 && (f & AF_ENTRY)) return ints[(off - 0x40) / 8];
    if (f & (AF_CALL | AF_ENTRY)) { snprintf(b, sizeof b, "%s_%02X_%04X", (f & AF_SOUND) ? "Som" : "Func", bank, addr); return b; }
    if (f & AF_JUMP) { snprintf(b, sizeof b, "L_%02X_%04X", bank, addr); return b; }
    return "";
}

// choose non-overlapping instructions for listing
static std::vector<u8> choose_listing(const Analysis& a, u32 begin, u32 end) {
    std::vector<u8> chosen(end - begin, 0);   // 1 = instruction start, 2 = continuation
    for (u32 off = begin; off < end;) {
        if (a.fl[off] & AF_START) {
            int len = a.instr_len(off);
            if (off + len <= end) {
                chosen[off - begin] = 1;
                for (int i = 1; i < len; i++) chosen[off - begin + i] = 2;
                off += len;
                continue;
            }
        }
        off++;
    }
    return chosen;
}

bool write_disassembly(const Analysis& a, const std::string& dir, std::string& err) {
    size_t n = a.rom.size();
    int nbanks = (int)(n / 0x4000);
    std::vector<u8> visible(n, 0);
    std::vector<std::vector<u8>> listing(nbanks);
    for (int bk = 0; bk < nbanks; bk++) {
        listing[bk] = choose_listing(a, (u32)bk * 0x4000, (u32)(bk + 1) * 0x4000);
        for (u32 i = 0; i < 0x4000; i++) if (listing[bk][i] == 1) visible[(u32)bk * 0x4000 + i] = 1;
    }
    std::ofstream mainf(U8(dir + "/main.asm"));
    if (!mainf) { err = "Não foi possível criar " + dir + "/main.asm"; return false; }
    mainf << "; Desmontagem gerada pelo NativeBoy Studio - " << a.info.title << "\n"
          << "; Montar com RGBDS: rgbasm -o main.o main.asm && rgblink -o jogo.gb main.o && rgbfix -v jogo.gb\n"
          << "; (Sem alterações o resultado é idêntico byte a byte à ROM original.)\n\n"
          << "INCLUDE \"registradores.inc\"\n";
    {
        std::ofstream r(U8(dir + "/registradores.inc"));
        if (!r) { err = "Não foi possível criar " + dir + "/registradores.inc"; return false; }
        r << "; Registradores de hardware do Game Boy (nomes no estilo hardware.inc)\n";
        for (unsigned ad = 0xFF00; ad <= 0xFFFF; ad++) { std::string h = hw_register_name((u16)ad); if (!h.empty()) r << "DEF " << h << " EQU $" << hex4(ad) << "\n"; }
    }
    if (!a.sym.ram.empty()) {
        std::ofstream v(U8(dir + "/variaveis.inc"));
        if (!v) { err = "Não foi possível criar " + dir + "/variaveis.inc"; return false; }
        v << "; Variáveis da RAM importadas do arquivo de símbolos\n";
        for (auto& kv : a.sym.ram) v << "DEF " << kv.second << " EQU $" << hex4(kv.first) << "\n";
        mainf << "INCLUDE \"variaveis.inc\"\n";
    }
    mainf << "\n";
    auto var_for = [&](u16 ad) { return var_name(a, ad); };
    for (int bk = 0; bk < nbanks; bk++) {
        char fn[32]; snprintf(fn, sizeof fn, "bank_%02X.asm", bk);
        mainf << "INCLUDE \"" << fn << "\"\n";
        std::ofstream o(U8(dir + "/" + fn));
        if (!o) { err = "Não foi possível criar " + std::string(fn); return false; }
        if (bk == 0) o << "SECTION \"ROM Banco $00\", ROM0[$0000]\n\n";
        else o << "SECTION \"ROM Banco $" << hex2(bk) << "\", ROMX[$4000], BANK[$" << hex2(bk) << "]\n\n";
        u32 base = (u32)bk * 0x4000;
        auto label_for = [&](u16 t) -> std::string {
            u32 to;
            if (t < 0x4000) to = t;
            else if (t < 0x8000 && bk != 0) to = Analysis::off_of(bk, t);
            else return "";
            if (to >= n || !visible[to]) return "";
            return label_name(a, to);
        };
        std::vector<u8> pend;
        u32 pend_start = 0;
        auto flush = [&]() {
            for (size_t i = 0; i < pend.size(); i += 16) {
                o << "    db ";
                for (size_t j = i; j < pend.size() && j < i + 16; j++) o << (j > i ? ", " : "") << "$" << hex2(pend[j]);
                o << " ; $" << hex4(Analysis::addr_of(pend_start + (u32)i)) << "\n";
            }
            pend.clear();
        };
        for (u32 i = 0; i < 0x4000; i++) {
            u32 off = base + i;
            if (off >= n) break;
            if (listing[bk][i] == 1) {
                flush();
                std::string lb = label_name(a, off);
                if (!lb.empty()) o << "\n" << lb << ":\n";
                u8 bytes[3] = {a.rom[off], off + 1 < n ? a.rom[off + 1] : (u8)0, off + 2 < n ? a.rom[off + 2] : (u8)0};
                std::string txt = format_instr(bytes, Analysis::addr_of(off), label_for, var_for);
                o << "    " << txt;
                int pad = 32 - (int)txt.size();
                o << std::string(pad > 1 ? pad : 1, ' ') << "; $" << hex4(Analysis::addr_of(off));
                if (a.fl[off] & AF_TRACED) o << " *";
                o << "\n";
            } else if (listing[bk][i] == 0) {
                auto tw = a.table_words.find(off);
                if (tw != a.table_words.end() && i + 1 < 0x4000 && listing[bk][i + 1] == 0) {
                    u16 w = (u16)(a.rom[off] | (a.rom[off + 1] << 8));
                    std::string lb = label_for(w);
                    if (!lb.empty()) {
                        flush();
                        auto jt = std::find_if(a.jump_tables.begin(), a.jump_tables.end(), [&](const JumpTable& j) { return j.table_off == off; });
                        if (jt != a.jump_tables.end()) o << "    ; tabela de saltos (" << jt->targets.size() << " entradas)\n";
                        o << "    dw " << lb << std::string(lb.size() < 29 ? 29 - lb.size() : 1, ' ') << "; $" << hex4(Analysis::addr_of(off)) << "\n";
                        i++;
                        continue;
                    }
                }
                if (pend.empty()) pend_start = off;
                pend.push_back(a.rom[off]);
            }
        }
        flush();
    }
    {
        std::ofstream b(U8(dir + "/build_rom.bat"));
        b << "@echo off\r\nrem Monta a ROM a partir desta desmontagem (RGBDS incluso no NativeBoy Studio)\r\n"
             "set RGBDS=%~dp0..\\..\\toolchain\\rgbds\r\nif exist \"%NB_RGBDS%\" set RGBDS=%NB_RGBDS%\r\n"
             "\"%RGBDS%\\rgbasm.exe\" -o main.o main.asm || exit /b 1\r\n\"%RGBDS%\\rgblink.exe\" -o jogo_modificado.gb main.o || exit /b 1\r\n"
             "\"%RGBDS%\\rgbfix.exe\" -v jogo_modificado.gb\r\n";
        std::ofstream s(U8(dir + "/build_rom.sh"));
        s << "#!/bin/sh\nset -e\nrgbasm -o main.o main.asm\nrgblink -o jogo_modificado.gb main.o\nrgbfix -v jogo_modificado.gb\n";
    }
    return true;
}

std::vector<std::string> disasm_lines(const Analysis& a, u32 off, int max_lines) {
    std::vector<std::string> out;
    size_t n = a.rom.size();
    u32 bank_end = (off / 0x4000 + 1) * 0x4000;
    int bk = Analysis::bank_of(off);
    auto label_for = [&](u16 t) -> std::string {
        u32 to;
        if (t < 0x4000) to = t; else if (t < 0x8000 && bk != 0) to = Analysis::off_of(bk, t); else return "";
        return to < n ? label_name(a, to) : "";
    };
    int data_run = 0;
    for (u32 o = off; o < bank_end && o < n && (int)out.size() < max_lines;) {
        char line[160];
        if (a.fl[o] & AF_START) {
            std::string lb = label_name(a, o);
            if (!lb.empty() && o != off) out.push_back("");
            if (!lb.empty()) out.push_back(lb + ":");
            int len = a.instr_len(o);
            u8 bytes[3] = {a.rom[o], o + 1 < n ? a.rom[o + 1] : (u8)0, o + 2 < n ? a.rom[o + 2] : (u8)0};
            char hexb[16] = "";
            for (int i = 0; i < len; i++) snprintf(hexb + i * 3, 4, "%02X ", bytes[i]);
            snprintf(line, sizeof line, "  %02X:%04X  %-9s %s%s", bk, Analysis::addr_of(o), hexb,
                     format_instr(bytes, Analysis::addr_of(o), label_for, [&](u16 ad) { return var_name(a, ad); }).c_str(), (a.fl[o] & AF_TRACED) ? "" : "   ; (não executado na descoberta)");
            out.push_back(line);
            u8 op = a.rom[o];
            OpKind k = op_info(op).kind;
            o += len;
            data_run = 0;
            if (k == OK_RET || k == OK_RETI || k == OK_JP || k == OK_JR || k == OK_JP_HL) {
                if (!(o < n && (a.fl[o] & AF_START))) { out.push_back("  ; --- fim do bloco ---"); }
            }
        } else if (a.table_words.count(o)) {
            u32 to = a.table_words.at(o);
            std::string lb = label_name(a, to);
            snprintf(line, sizeof line, "  %02X:%04X  %02X %02X     dw %s   ; entrada da tabela de saltos", bk, Analysis::addr_of(o), a.rom[o], a.rom[o + 1],
                     lb.empty() ? ("$" + hex4(Analysis::addr_of(to))).c_str() : lb.c_str());
            out.push_back(line);
            o += 2;
            data_run = 0;
        } else {
            if (++data_run > 64) break;
            snprintf(line, sizeof line, "  %02X:%04X  %02X        db $%02X", bk, Analysis::addr_of(o), a.rom[o], a.rom[o]);
            out.push_back(line);
            o++;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Pseudo-C: a readable, line-by-line C-like translation. It is meant for understanding the game,
// not for compiling (the real compilable C++ is the recompiler output in src/).
namespace {
const char* R8[8] = {"b", "c", "d", "e", "h", "l", "mem[hl]", "a"};
const char* R16[4] = {"bc", "de", "hl", "sp"};
const char* R16S[4] = {"bc", "de", "hl", "af"};

std::string hexc(unsigned v, int digits) { char b[16]; snprintf(b, sizeof b, digits == 2 ? "0x%02X" : "0x%04X", v); return b; }

struct PcCtx {
    const Analysis& a;
    int bank;
    std::string cmp_lhs, cmp_rhs;   // from "cp": the flags describe a comparison
    std::string zsrc;               // register whose zero-ness the Z flag reflects
    std::string bit_expr;           // from "bit n,r": Z = !(r & mask)
    explicit PcCtx(const Analysis& an, int b) : a(an), bank(b) {}
    std::string var(u16 ad) const {
        std::string v = var_name(a, ad);
        return v.empty() ? "mem[" + hexc(ad, 4) + "]" : v;
    }
    std::string label(u16 t) const {
        u32 to;
        if (t < 0x4000) to = t; else if (t < 0x8000 && bank != 0) to = Analysis::off_of(bank, t);
        else if (t < 0x8000 && a.info.mbc == 0) to = Analysis::off_of(1, t);
        else { char b[32]; snprintf(b, sizeof b, "ender_%04X /* banco atual */", t); return b; }
        std::string l = to < a.rom.size() ? label_name(a, to) : "";
        if (l.empty()) { char b[32]; snprintf(b, sizeof b, "L_%02X_%04X", Analysis::bank_of(to), t); return b; }
        return l;
    }
    std::string cond(int cc) const {
        bool neg = (cc & 1) == 0;            // nz / nc
        if (!cmp_lhs.empty()) {
            if (cc == 0) return cmp_lhs + " != " + cmp_rhs;
            if (cc == 1) return cmp_lhs + " == " + cmp_rhs;
            if (cc == 2) return cmp_lhs + " >= " + cmp_rhs;
            return cmp_lhs + " < " + cmp_rhs;
        }
        if (cc < 2 && !bit_expr.empty()) return neg ? "(" + bit_expr + ")" : "!(" + bit_expr + ")";
        if (cc < 2 && !zsrc.empty()) return zsrc + (neg ? " != 0" : " == 0");
        return std::string(neg ? "!" : "") + (cc < 2 ? "zero" : "carry");
    }
    void flags_clear() { cmp_lhs.clear(); cmp_rhs.clear(); zsrc.clear(); bit_expr.clear(); }
};

std::string translate(PcCtx& c, const u8* b, u16 addr) {
    u8 op = b[0];
    u16 n16 = (u16)(b[1] | (b[2] << 8));
    u8 n8 = b[1];
    char t[160];
    // instructions that leave the flags alone keep the comparison context
    auto keep = [&](std::string s) { return s; };
    auto setz = [&](const std::string& reg, std::string s) { c.flags_clear(); c.zsrc = reg; return s; };
    auto clobber = [&](std::string s) { c.flags_clear(); return s; };
    auto writes = [&](const std::string& reg, std::string s) {   // register changed without touching flags
        if (c.zsrc == reg || c.cmp_lhs == reg) c.flags_clear();
        return s;
    };
    if (op == 0xCB) {
        u8 x = b[1]; int r = x & 7; std::string R = R8[r];
        int grp = x >> 6, y = (x >> 3) & 7;
        if (grp == 1) { c.flags_clear(); char m[32]; snprintf(m, sizeof m, "%s & 0x%02X", R.c_str(), 1 << y); c.bit_expr = m; snprintf(t, sizeof t, "/* testa o bit %d de %s */", y, R.c_str()); return t; }
        if (grp == 2) { snprintf(t, sizeof t, "%s &= ~0x%02X;   // zera o bit %d", R.c_str(), 1 << y, y); return writes(R, t); }
        if (grp == 3) { snprintf(t, sizeof t, "%s |= 0x%02X;    // liga o bit %d", R.c_str(), 1 << y, y); return writes(R, t); }
        static const char* sh[8] = {"rotate_left(%s)", "rotate_right(%s)", "rotate_left_carry(%s)", "rotate_right_carry(%s)",
                                    "%s << 1", "(s8)%s >> 1", "swap_nibbles(%s)", "%s >> 1"};
        char e[64]; snprintf(e, sizeof e, sh[y], R.c_str());
        snprintf(t, sizeof t, "%s = %s;", R.c_str(), e);
        return setz(R, t);
    }
    if (op == 0x76) return keep("halt();   // espera a próxima interrupção");
    if (op >= 0x40 && op < 0x80) {
        std::string d = R8[(op >> 3) & 7], s = R8[op & 7];
        if (d == s) return keep("/* nop */");
        return writes(d, d + " = " + s + ";");
    }
    if (op >= 0x80 && op < 0xC0) {
        std::string s = R8[op & 7];
        switch ((op >> 3) & 7) {
        case 0: return setz("a", "a += " + s + ";");
        case 1: return setz("a", "a += " + s + " + carry;");
        case 2: return setz("a", s == "a" ? "a = 0;" : "a -= " + s + ";");
        case 3: return setz("a", "a -= " + s + " + carry;");
        case 4: return setz("a", s == "a" ? "/* testa se a é zero */" : "a &= " + s + ";");
        case 5: return setz("a", s == "a" ? "a = 0;" : "a ^= " + s + ";");
        case 6: return setz("a", s == "a" ? "/* testa se a é zero */" : "a |= " + s + ";");
        default: c.flags_clear(); c.cmp_lhs = "a"; c.cmp_rhs = s; return "/* compara a com " + s + " */";
        }
    }
    int r = (op >> 3) & 7, rp = (op >> 4) & 3;
    if ((op & 0xC7) == 0x06) return writes(R8[r], std::string(R8[r]) + " = " + hexc(n8, 2) + ";");
    if ((op & 0xC7) == 0x04) return setz(R8[r], std::string(R8[r]) + "++;");
    if ((op & 0xC7) == 0x05) return setz(R8[r], std::string(R8[r]) + "--;");
    if ((op & 0xCF) == 0x01) {
        std::string v = n16 >= 0x8000 ? var_name(c.a, n16) : "";
        return writes(R16[rp], std::string(R16[rp]) + " = " + (v.empty() ? hexc(n16, 4) : "&" + v) + ";");
    }
    if ((op & 0xCF) == 0x03) return writes(R16[rp], std::string(R16[rp]) + "++;");
    if ((op & 0xCF) == 0x0B) return writes(R16[rp], std::string(R16[rp]) + "--;");
    if ((op & 0xCF) == 0x09) return clobber(std::string("hl += ") + R16[rp] + ";");
    if ((op & 0xCF) == 0xC5) return keep(std::string("push(") + R16S[rp] + ");");
    if ((op & 0xCF) == 0xC1) { if (rp == 3) c.flags_clear(); return writes(R16S[rp], std::string(R16S[rp]) + " = pop();"); }
    if ((op & 0xC7) == 0xC7) { snprintf(t, sizeof t, "%s();   // rst $%02X", c.label(op & 0x38).c_str(), op & 0x38); return clobber(t); }
    switch (op) {
    case 0x00: return keep("/* nop */");
    case 0x02: return keep("mem[bc] = a;");
    case 0x12: return keep("mem[de] = a;");
    case 0x22: return keep("mem[hl++] = a;");
    case 0x32: return keep("mem[hl--] = a;");
    case 0x0A: return writes("a", "a = mem[bc];");
    case 0x1A: return writes("a", "a = mem[de];");
    case 0x2A: return writes("a", "a = mem[hl++];");
    case 0x3A: return writes("a", "a = mem[hl--];");
    case 0x07: return clobber("a = rotate_left(a);");
    case 0x0F: return clobber("a = rotate_right(a);");
    case 0x17: return clobber("a = rotate_left_carry(a);");
    case 0x1F: return clobber("a = rotate_right_carry(a);");
    case 0x27: return setz("a", "a = bcd_adjust(a);");
    case 0x2F: return keep("a = ~a;");
    case 0x37: c.flags_clear(); return "carry = 1;";
    case 0x3F: c.flags_clear(); return "carry = !carry;";
    case 0x08: return keep(c.var(n16) + " = sp;   // 16 bits");
    case 0x10: return keep("stop();   // modo de baixo consumo / troca de velocidade (GBC)");
    case 0xF3: return keep("disable_interrupts();");
    case 0xFB: return keep("enable_interrupts();");
    case 0xC6: return setz("a", "a += " + hexc(n8, 2) + ";");
    case 0xCE: return setz("a", "a += " + hexc(n8, 2) + " + carry;");
    case 0xD6: return setz("a", "a -= " + hexc(n8, 2) + ";");
    case 0xDE: return setz("a", "a -= " + hexc(n8, 2) + " + carry;");
    case 0xE6: return setz("a", "a &= " + hexc(n8, 2) + ";");
    case 0xEE: return setz("a", "a ^= " + hexc(n8, 2) + ";");
    case 0xF6: return setz("a", "a |= " + hexc(n8, 2) + ";");
    case 0xFE: c.flags_clear(); c.cmp_lhs = "a"; c.cmp_rhs = hexc(n8, 2); return "/* compara a com " + hexc(n8, 2) + " */";
    case 0xE0: return keep(c.var((u16)(0xFF00 | n8)) + " = a;");
    case 0xF0: return writes("a", "a = " + c.var((u16)(0xFF00 | n8)) + ";");
    case 0xE2: return keep("io[c] = a;   // mem[0xFF00 + c]");
    case 0xF2: return writes("a", "a = io[c];");
    case 0xEA:
        if (n16 >= 0x2000 && n16 < 0x4000) return keep("switch_rom_bank(a);   // " + c.var(n16) + " = a");
        if (n16 < 0x2000) return keep("enable_save_ram(a);   // " + hexc(n16, 4) + " = a (0x0A liga a RAM do cartucho)");
        if (n16 >= 0x4000 && n16 < 0x6000) return keep("switch_ram_bank(a);   // " + hexc(n16, 4) + " = a");
        return keep(c.var(n16) + " = a;");
    case 0xFA: return writes("a", "a = " + c.var(n16) + ";");
    case 0xE8: return clobber("sp += " + std::to_string((s8)n8) + ";");
    case 0xF8: return clobber("hl = sp + " + std::to_string((s8)n8) + ";");
    case 0xF9: return keep("sp = hl;");
    case 0xC3: return keep("goto " + c.label(n16) + ";");
    case 0x18: return keep("goto " + c.label((u16)(addr + 2 + (s8)n8)) + ";");
    case 0xE9: return keep("goto *hl;   // salto indireto (endereço calculado)");
    case 0xCD: { std::string s = c.label(n16) + "();"; c.flags_clear(); return s; }
    case 0xC9: return keep("return;");
    case 0xD9: return keep("enable_interrupts(); return;   // reti");
    default: break;
    }
    if ((op & 0xE7) == 0xC2) return keep("if (" + c.cond((op >> 3) & 3) + ") goto " + c.label(n16) + ";");
    if ((op & 0xE7) == 0x20) return keep("if (" + c.cond((op >> 3) & 3) + ") goto " + c.label((u16)(addr + 2 + (s8)n8)) + ";");
    if ((op & 0xE7) == 0xC4) { std::string s = "if (" + c.cond((op >> 3) & 3) + ") " + c.label(n16) + "();"; c.flags_clear(); return s; }
    if ((op & 0xE7) == 0xC0) return keep("if (" + c.cond((op >> 3) & 3) + ") return;");
    snprintf(t, sizeof t, "/* byte inválido $%02X: trava o processador */", op);
    return t;
}
} // namespace

std::vector<std::string> pseudo_c_lines(const Analysis& a, size_t fi, int max_lines) {
    std::vector<std::string> out;
    if (fi >= a.funcs.size()) return out;
    const Function& f = a.funcs[fi];
    size_t n = a.rom.size();
    u32 end = fi + 1 < a.funcs.size() && a.funcs[fi + 1].off > f.off ? a.funcs[fi + 1].off : (u32)n;
    u32 bank_end = (f.off / 0x4000 + 1) * 0x4000;
    if (end > bank_end) end = bank_end;
    char hdr[200];
    snprintf(hdr, sizeof hdr, "void %s(void)   // banco %02X, endereço $%04X, %d instruções%s%s", f.name.c_str(), f.bank, f.addr, f.n_instr,
             f.sound ? ", mexe no som" : "", f.traced ? "" : ", não executada na descoberta");
    out.push_back(hdr);
    out.push_back("{");
    PcCtx c(a, f.bank);
    int data = 0, nops = 0;
    std::map<u32, const JumpTable*> tables;    // table_off -> table
    for (auto& j : a.jump_tables) tables[j.table_off] = &j;
    const JumpTable* cur_tab = nullptr; u32 tab_end = 0; int case_no = 0;
    auto flush_misc = [&]() {
        if (data) { out.push_back("    /* " + std::to_string(data) + " bytes de dados */"); data = 0; }
        if (nops) { out.push_back(nops == 1 ? "    /* nop */" : "    /* " + std::to_string(nops) + " x nop */"); nops = 0; }
    };
    for (u32 o = f.off; o < end && (int)out.size() < max_lines;) {
        if (cur_tab && o >= tab_end) { out.push_back("    }"); cur_tab = nullptr; }
        if (a.fl[o] & AF_START) {
            if (o != f.off) {
                std::string lb = label_name(a, o);
                if (!lb.empty()) { flush_misc(); out.push_back(lb + ":"); c.flags_clear(); }
            }
            if (a.rom[o] == 0x00) { if (data) flush_misc(); nops++; o++; continue; }
            flush_misc();
            u8 bytes[3] = {a.rom[o], o + 1 < n ? a.rom[o + 1] : (u8)0, o + 2 < n ? a.rom[o + 2] : (u8)0};
            int len = a.instr_len(o);
            auto tb = tables.find(o + len);
            if (tb != tables.end() && tb->second->call_off == o) {
                std::string callee = translate(c, bytes, Analysis::addr_of(o));
                size_t par = callee.find("();");
                out.push_back("    switch (a) {   // " + (par == std::string::npos ? callee : callee.substr(0, par)) + " lê a tabela abaixo");
                cur_tab = tb->second; tab_end = o + len + 2 * (u32)cur_tab->targets.size(); case_no = 0;
            } else out.push_back("    " + translate(c, bytes, Analysis::addr_of(o)));
            o += len;
        } else if (a.table_words.count(o)) {
            flush_misc();
            std::string lb = label_name(a, a.table_words.at(o));
            if (cur_tab) out.push_back("    case " + std::to_string(case_no++) + ": goto " + (lb.empty() ? "?" : lb) + ";");
            else out.push_back("    /* ponteiro: */ " + (lb.empty() ? "?" : lb));
            o += 2;
        } else { if (nops) flush_misc(); data++; o++; }
    }
    if (cur_tab) out.push_back("    }");
    flush_misc();
    out.push_back("}");
    return out;
}

bool write_pseudo_c(const Analysis& a, const std::string& path, std::string& err) {
    std::ofstream o(U8(path));
    if (!o) { err = "Não foi possível criar " + path; return false; }
    o << "/* Pseudocódigo C gerado pelo NativeBoy Studio - " << a.info.title << "\n"
         " * Tradução legível de cada função, para ESTUDO: não é para compilar.\n"
         " * O código C++ que realmente compila e roda nativo está em src/ (saída do recompilador).\n"
         " *   a, b, c, d, e, h, l  = registradores de 8 bits;  bc, de, hl, sp = pares de 16 bits\n"
         " *   mem[x] = memória;  zero / carry = flags;  rXXX = registradores de hardware\n"
         " *   goto *hl = salto para endereço calculado (geralmente tabela de saltos)\n"
         " */\n\n";
    for (size_t i = 0; i < a.funcs.size(); i++) {
        for (auto& l : pseudo_c_lines(a, i, 100000)) o << l << "\n";
        o << "\n";
    }
    return (bool)o;
}

} // namespace nb
