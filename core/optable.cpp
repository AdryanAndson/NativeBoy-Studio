#include "optable.h"
#include <cstdio>

namespace gb {

static OpInfo g_ops[256];
static OpInfo g_cb[256];
static bool g_built = false;

static const char* R8N[8] = {"b", "c", "d", "e", "h", "l", "[hl]", "a"};
static std::string r8_get(int r) {
    static const char* s[8] = {"m.b", "m.c", "m.d", "m.e", "m.h", "m.l", "rd(m, HL)", "m.a"};
    return s[r];
}
static std::string r8_set(int r, const std::string& v) {
    if (r == 6) return "wr(m, HL, (u8)(" + v + "));";
    static const char* s[8] = {"m.b", "m.c", "m.d", "m.e", "m.h", "m.l", "", "m.a"};
    return std::string(s[r]) + " = (u8)(" + v + ");";
}
static const char* RR_N[4] = {"bc", "de", "hl", "sp"};
static std::string rr_get(int i) {
    static const char* s[4] = {"BC", "DE", "HL", "m.sp"};
    return s[i];
}
static std::string rr_set(int i, const std::string& v) {
    if (i == 3) return "m.sp = (u16)(" + v + ");";
    static const char* s[3] = {"SET_BC", "SET_DE", "SET_HL"};
    return std::string(s[i]) + "(" + v + ");";
}
static const char* CCN[4] = {"nz", "z", "nc", "c"};
static const char* CCX[4] = {"!(m.f & FZ)", "(m.f & FZ)", "!(m.f & FC)", "(m.f & FC)"};
static const char* ALUN[8] = {"add", "adc", "sub", "sbc", "and", "xor", "or", "cp"};
static const char* ALUF[8] = {"alu_add", "alu_adc", "alu_sub", "alu_sbc", "alu_and", "alu_xor", "alu_or", "alu_cp"};
static const char* ALUA[8] = {"add a, ", "adc a, ", "sub a, ", "sbc a, ", "and a, ", "xor a, ", "or a, ", "cp a, "};

static OpInfo mk(const std::string& mn, int len, int cyc, const std::string& code, bool memw = false) {
    OpInfo o; o.mnem = mn; o.len = len; o.cycles = cyc; o.cycles_taken = cyc; o.code = code; o.mem_write = memw;
    return o;
}

static void build() {
    if (g_built) return;
    g_built = true;
    for (int i = 0; i < 256; i++) { g_ops[i] = mk("db ${op}", 1, 4, ""); g_ops[i].kind = OK_ILLEGAL; }

    g_ops[0x00] = mk("nop", 1, 4, "");
    for (int i = 0; i < 4; i++) {
        g_ops[0x01 + 16 * i] = mk(std::string("ld ") + RR_N[i] + ", {n16}", 3, 12, rr_set(i, "n16"));
        g_ops[0x03 + 16 * i] = mk(std::string("inc ") + RR_N[i], 1, 8, rr_set(i, rr_get(i) + " + 1"));
        g_ops[0x0B + 16 * i] = mk(std::string("dec ") + RR_N[i], 1, 8, rr_set(i, rr_get(i) + " - 1"));
        g_ops[0x09 + 16 * i] = mk(std::string("add hl, ") + RR_N[i], 1, 8, "add_hl(m, " + rr_get(i) + ");");
    }
    g_ops[0x02] = mk("ld [bc], a", 1, 8, "wr(m, BC, m.a);", true);
    g_ops[0x12] = mk("ld [de], a", 1, 8, "wr(m, DE, m.a);", true);
    g_ops[0x22] = mk("ld [hl+], a", 1, 8, "{ u16 _a = HL; wr(m, _a, m.a); SET_HL(_a + 1); }", true);
    g_ops[0x32] = mk("ld [hl-], a", 1, 8, "{ u16 _a = HL; wr(m, _a, m.a); SET_HL(_a - 1); }", true);
    g_ops[0x0A] = mk("ld a, [bc]", 1, 8, "m.a = rd(m, BC);");
    g_ops[0x1A] = mk("ld a, [de]", 1, 8, "m.a = rd(m, DE);");
    g_ops[0x2A] = mk("ld a, [hl+]", 1, 8, "{ u16 _a = HL; m.a = rd(m, _a); SET_HL(_a + 1); }");
    g_ops[0x3A] = mk("ld a, [hl-]", 1, 8, "{ u16 _a = HL; m.a = rd(m, _a); SET_HL(_a - 1); }");
    for (int r = 0; r < 8; r++) {
        bool mhl = r == 6;
        if (mhl) {
            g_ops[0x04 + 8 * r] = mk("inc [hl]", 1, 12, "{ u16 _a = HL; wr(m, _a, inc8(m, rd(m, _a))); }", true);
            g_ops[0x05 + 8 * r] = mk("dec [hl]", 1, 12, "{ u16 _a = HL; wr(m, _a, dec8(m, rd(m, _a))); }", true);
        } else {
            g_ops[0x04 + 8 * r] = mk(std::string("inc ") + R8N[r], 1, 4, r8_set(r, "inc8(m, " + r8_get(r) + ")"));
            g_ops[0x05 + 8 * r] = mk(std::string("dec ") + R8N[r], 1, 4, r8_set(r, "dec8(m, " + r8_get(r) + ")"));
        }
        g_ops[0x06 + 8 * r] = mk(std::string("ld ") + R8N[r] + ", {n8}", 2, mhl ? 12 : 8, r8_set(r, "n8"), mhl);
    }
    g_ops[0x07] = mk("rlca", 1, 4, "op_rlca(m);");
    g_ops[0x0F] = mk("rrca", 1, 4, "op_rrca(m);");
    g_ops[0x17] = mk("rla", 1, 4, "op_rla(m);");
    g_ops[0x1F] = mk("rra", 1, 4, "op_rra(m);");
    g_ops[0x27] = mk("daa", 1, 4, "op_daa(m);");
    g_ops[0x2F] = mk("cpl", 1, 4, "m.a = (u8)~m.a; m.f |= FN | FH;");
    g_ops[0x37] = mk("scf", 1, 4, "m.f = (m.f & FZ) | FC;");
    g_ops[0x3F] = mk("ccf", 1, 4, "m.f = (m.f & FZ) | ((m.f & FC) ? 0 : FC);");
    g_ops[0x08] = mk("ld [{n16}], sp", 3, 20, "wr16(m, n16, m.sp);", true);

    { OpInfo o = mk("stop", 2, 4, "op_stop(m);"); o.kind = OK_STOP; g_ops[0x10] = o; }
    { OpInfo o = mk("jr {tgt}", 2, 12, ""); o.kind = OK_JR; g_ops[0x18] = o; }
    for (int i = 0; i < 4; i++) {
        OpInfo o = mk(std::string("jr ") + CCN[i] + ", {tgt}", 2, 8, ""); o.cycles_taken = 12; o.kind = OK_JR_CC; o.cond = CCX[i];
        g_ops[0x20 + 8 * i] = o;
        OpInfo r = mk(std::string("ret ") + CCN[i], 1, 8, ""); r.cycles_taken = 20; r.kind = OK_RET_CC; r.cond = CCX[i];
        g_ops[0xC0 + 8 * i] = r;
        OpInfo j = mk(std::string("jp ") + CCN[i] + ", {tgt}", 3, 12, ""); j.cycles_taken = 16; j.kind = OK_JP_CC; j.cond = CCX[i];
        g_ops[0xC2 + 8 * i] = j;
        OpInfo c = mk(std::string("call ") + CCN[i] + ", {tgt}", 3, 12, ""); c.cycles_taken = 24; c.kind = OK_CALL_CC; c.cond = CCX[i]; c.mem_write = true;
        g_ops[0xC4 + 8 * i] = c;
    }
    for (int d = 0; d < 8; d++)
        for (int s = 0; s < 8; s++) {
            int op = 0x40 + d * 8 + s;
            if (op == 0x76) continue;
            int cyc = (d == 6 || s == 6) ? 8 : 4;
            g_ops[op] = mk(std::string("ld ") + R8N[d] + ", " + R8N[s], 1, cyc, r8_set(d, r8_get(s)), d == 6);
        }
    { OpInfo o = mk("halt", 1, 4, ""); o.kind = OK_HALT; g_ops[0x76] = o; }
    for (int k = 0; k < 8; k++) {
        for (int s = 0; s < 8; s++)
            g_ops[0x80 + 8 * k + s] = mk(std::string(ALUA[k]) + R8N[s], 1, s == 6 ? 8 : 4, std::string(ALUF[k]) + "(m, " + r8_get(s) + ");");
        g_ops[0xC6 + 8 * k] = mk(std::string(ALUA[k]) + "{n8}", 2, 8, std::string(ALUF[k]) + "(m, n8);");
        OpInfo r = mk("rst ${rst}", 1, 16, ""); r.kind = OK_RST; r.rst = k * 8; r.mem_write = true;
        char buf[32]; snprintf(buf, sizeof buf, "rst $%02X", k * 8); r.mnem = buf;
        g_ops[0xC7 + 8 * k] = r;
    }
    (void)ALUN;
    { OpInfo o = mk("ret", 1, 16, ""); o.kind = OK_RET; g_ops[0xC9] = o; }
    { OpInfo o = mk("reti", 1, 16, ""); o.kind = OK_RETI; g_ops[0xD9] = o; }
    { OpInfo o = mk("jp {tgt}", 3, 16, ""); o.kind = OK_JP; g_ops[0xC3] = o; }
    { OpInfo o = mk("call {tgt}", 3, 24, ""); o.kind = OK_CALL; o.mem_write = true; g_ops[0xCD] = o; }
    { OpInfo o = mk("jp hl", 1, 4, ""); o.kind = OK_JP_HL; g_ops[0xE9] = o; }
    { OpInfo o = mk("di", 1, 4, "m.ime = false; m.ei_delay = 0;"); o.kind = OK_DI; g_ops[0xF3] = o; }
    { OpInfo o = mk("ei", 1, 4, "if (!m.ime && !m.ei_delay) { m.ei_delay = 2; m.next_event = 0; }"); o.kind = OK_EI; g_ops[0xFB] = o; }
    g_ops[0xC1] = mk("pop bc", 1, 12, "SET_BC(pop16(m));");
    g_ops[0xD1] = mk("pop de", 1, 12, "SET_DE(pop16(m));");
    g_ops[0xE1] = mk("pop hl", 1, 12, "SET_HL(pop16(m));");
    g_ops[0xF1] = mk("pop af", 1, 12, "SET_AF(pop16(m));");
    g_ops[0xC5] = mk("push bc", 1, 16, "push16(m, BC);", true);
    g_ops[0xD5] = mk("push de", 1, 16, "push16(m, DE);", true);
    g_ops[0xE5] = mk("push hl", 1, 16, "push16(m, HL);", true);
    g_ops[0xF5] = mk("push af", 1, 16, "push16(m, AF);", true);
    g_ops[0xE0] = mk("ldh [{a8}], a", 2, 12, "wr(m, (u16)(0xFF00 | n8), m.a);", true);
    g_ops[0xF0] = mk("ldh a, [{a8}]", 2, 12, "m.a = rd(m, (u16)(0xFF00 | n8));");
    g_ops[0xE2] = mk("ldh [c], a", 1, 8, "wr(m, (u16)(0xFF00 | m.c), m.a);", true);
    g_ops[0xF2] = mk("ldh a, [c]", 1, 8, "m.a = rd(m, (u16)(0xFF00 | m.c));");
    g_ops[0xE8] = mk("add sp, {e8}", 2, 16, "m.sp = sp_plus(m, e8);");
    g_ops[0xF8] = mk("ld hl, sp {spe}", 2, 12, "SET_HL(sp_plus(m, e8));");
    g_ops[0xF9] = mk("ld sp, hl", 1, 8, "m.sp = HL;");
    g_ops[0xEA] = mk("ld [{n16}], a", 3, 16, "wr(m, n16, m.a);", true);
    g_ops[0xFA] = mk("ld a, [{n16}]", 3, 16, "m.a = rd(m, n16);");
    g_ops[0xCB] = mk("prefix cb", 2, 8, "");

    // ---- CB prefix ----
    static const char* ROTN[8] = {"rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl"};
    static const char* ROTF[8] = {"cb_rlc", "cb_rrc", "cb_rl", "cb_rr", "cb_sla", "cb_sra", "cb_swap", "cb_srl"};
    for (int op = 0; op < 256; op++) {
        int r = op & 7, y = (op >> 3) & 7, x = op >> 6;
        bool mhl = r == 6;
        OpInfo o;
        o.len = 2;
        char bit[4]; snprintf(bit, sizeof bit, "%d", y);
        if (x == 0) {
            o.mnem = std::string(ROTN[y]) + " " + R8N[r];
            o.cycles = mhl ? 16 : 8;
            o.code = mhl ? std::string("{ u16 _a = HL; wr(m, _a, ") + ROTF[y] + "(m, rd(m, _a))); }"
                         : r8_set(r, std::string(ROTF[y]) + "(m, " + r8_get(r) + ")");
            o.mem_write = mhl;
        } else if (x == 1) {
            o.mnem = std::string("bit ") + bit + ", " + R8N[r];
            o.cycles = mhl ? 12 : 8;
            o.code = std::string("cb_bit(m, ") + bit + ", " + r8_get(r) + ");";
        } else if (x == 2) {
            o.mnem = std::string("res ") + bit + ", " + R8N[r];
            o.cycles = mhl ? 16 : 8;
            char mask[16]; snprintf(mask, sizeof mask, "0x%02X", (~(1 << y)) & 0xFF);
            o.code = mhl ? std::string("{ u16 _a = HL; wr(m, _a, rd(m, _a) & ") + mask + "); }"
                         : r8_set(r, r8_get(r) + " & " + mask);
            o.mem_write = mhl;
        } else {
            o.mnem = std::string("set ") + bit + ", " + R8N[r];
            o.cycles = mhl ? 16 : 8;
            char mask[16]; snprintf(mask, sizeof mask, "0x%02X", 1 << y);
            o.code = mhl ? std::string("{ u16 _a = HL; wr(m, _a, rd(m, _a) | ") + mask + "); }"
                         : r8_set(r, r8_get(r) + " | " + mask);
            o.mem_write = mhl;
        }
        o.cycles_taken = o.cycles;
        g_cb[op] = o;
    }
}

static void set_timing(OpInfo& o, bool cb) {
    o.post = o.cycles;
    if (o.kind != OK_NORMAL || o.code.empty()) return;
    bool r = o.code.find("rd(m,") != std::string::npos, w = o.code.find("wr(m,") != std::string::npos;
    bool stack = o.code.find("push16") != std::string::npos || o.code.find("pop16") != std::string::npos || o.code.find("wr16") != std::string::npos;
    if (stack || (!r && !w)) return;
    if (r && w && o.code.find("_a") != std::string::npos) {
        // read-modify-write on [hl]: read on M(n-1), write on M(n)
        size_t p = o.code.find("wr(m, _a, ");
        std::string inner = o.code.substr(p + 10);                 // "<f>(m, rd(m, _a))); }"
        size_t q = inner.find("rd(m, _a)");
        std::string expr = inner.substr(0, q) + "_v" + inner.substr(q + 9);
        expr = expr.substr(0, expr.rfind(';'));                    // "<f>(m, _v))" or "_v & mask)"
        o.code = "{ u16 _a = HL; u8 _v = rd(m, _a); m.tick(4); wr(m, _a, " + expr + "; }";
        o.pre = o.cycles - 8; o.post = 4;
    } else {
        o.pre = o.cycles - 4; o.post = 4;
    }
    (void)cb;
}

static void build_timing() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < 256; i++) { set_timing(g_ops[i], false); set_timing(g_cb[i], true); }
}

const OpInfo& op_info(uint8_t op) { build(); build_timing(); return g_ops[op]; }
const OpInfo& cb_info(uint8_t op) { build(); build_timing(); return g_cb[op]; }

} // namespace gb
