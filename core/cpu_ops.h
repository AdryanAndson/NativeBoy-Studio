// SM83 instruction semantics shared by the interpreter and by recompiled code.
// Every opcode template in optable.cpp is written in terms of these helpers,
// so interpreted and native execution use exactly the same semantics.
#pragma once
#include "gb.h"

namespace gb {

#define GB_LIKELY(x) __builtin_expect(!!(x), 1)
#define GB_UNLIKELY(x) __builtin_expect(!!(x), 0)

#define BC ((u16)((m.b << 8) | m.c))
#define DE ((u16)((m.d << 8) | m.e))
#define HL ((u16)((m.h << 8) | m.l))
#define AF ((u16)((m.a << 8) | m.f))
#define SET_BC(v) do { u16 _v = (u16)(v); m.b = (u8)(_v >> 8); m.c = (u8)_v; } while (0)
#define SET_DE(v) do { u16 _v = (u16)(v); m.d = (u8)(_v >> 8); m.e = (u8)_v; } while (0)
#define SET_HL(v) do { u16 _v = (u16)(v); m.h = (u8)(_v >> 8); m.l = (u8)_v; } while (0)
#define SET_AF(v) do { u16 _v = (u16)(v); m.a = (u8)(_v >> 8); m.f = (u8)(_v & 0xF0); } while (0)
#define FLAG_Z (m.f & FZ)
#define FLAG_C (m.f & FC)

static inline void push16(Machine& m, u16 v) {
    m.sp--; wr(m, m.sp, (u8)(v >> 8));
    m.sp--; wr(m, m.sp, (u8)v);
}
static inline u16 pop16(Machine& m) {
    u8 lo = rd(m, m.sp); m.sp++;
    u8 hi = rd(m, m.sp); m.sp++;
    return (u16)(lo | (hi << 8));
}
static inline u16 rd16(Machine& m, u16 a) { return (u16)(rd(m, a) | (rd(m, (u16)(a + 1)) << 8)); }
static inline void wr16(Machine& m, u16 a, u16 v) { wr(m, a, (u8)v); wr(m, (u16)(a + 1), (u8)(v >> 8)); }

static inline void alu_add(Machine& m, u8 v) {
    unsigned r = m.a + v;
    m.f = (((u8)r == 0) ? FZ : 0) | (((m.a & 0xF) + (v & 0xF)) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0);
    m.a = (u8)r;
}
static inline void alu_adc(Machine& m, u8 v) {
    unsigned cy = (m.f & FC) ? 1 : 0;
    unsigned r = m.a + v + cy;
    m.f = (((u8)r == 0) ? FZ : 0) | (((m.a & 0xF) + (v & 0xF) + cy) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0);
    m.a = (u8)r;
}
static inline void alu_sub(Machine& m, u8 v) {
    int r = m.a - v;
    m.f = FN | (((u8)r == 0) ? FZ : 0) | (((m.a & 0xF) - (v & 0xF)) < 0 ? FH : 0) | (r < 0 ? FC : 0);
    m.a = (u8)r;
}
static inline void alu_sbc(Machine& m, u8 v) {
    int cy = (m.f & FC) ? 1 : 0;
    int r = m.a - v - cy;
    m.f = FN | (((u8)r == 0) ? FZ : 0) | (((m.a & 0xF) - (v & 0xF) - cy) < 0 ? FH : 0) | (r < 0 ? FC : 0);
    m.a = (u8)r;
}
static inline void alu_and(Machine& m, u8 v) { m.a &= v; m.f = (m.a ? 0 : FZ) | FH; }
static inline void alu_xor(Machine& m, u8 v) { m.a ^= v; m.f = (m.a ? 0 : FZ); }
static inline void alu_or(Machine& m, u8 v) { m.a |= v; m.f = (m.a ? 0 : FZ); }
static inline void alu_cp(Machine& m, u8 v) {
    int r = m.a - v;
    m.f = FN | (((u8)r == 0) ? FZ : 0) | (((m.a & 0xF) - (v & 0xF)) < 0 ? FH : 0) | (r < 0 ? FC : 0);
}
static inline u8 inc8(Machine& m, u8 v) {
    u8 r = (u8)(v + 1);
    m.f = (m.f & FC) | (r == 0 ? FZ : 0) | ((v & 0xF) == 0xF ? FH : 0);
    return r;
}
static inline u8 dec8(Machine& m, u8 v) {
    u8 r = (u8)(v - 1);
    m.f = (m.f & FC) | FN | (r == 0 ? FZ : 0) | ((v & 0xF) == 0 ? FH : 0);
    return r;
}
static inline void add_hl(Machine& m, u16 v) {
    u16 hl = HL;
    unsigned r = hl + v;
    m.f = (m.f & FZ) | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0);
    SET_HL(r);
}
static inline u16 sp_plus(Machine& m, s8 e) {
    u16 sp = m.sp;
    u8 ue = (u8)e;
    m.f = (((sp & 0xF) + (ue & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + ue) > 0xFF ? FC : 0);
    return (u16)(sp + e);
}
static inline void op_daa(Machine& m) {
    int a = m.a;
    if (!(m.f & FN)) {
        if ((m.f & FH) || (a & 0xF) > 9) a += 0x06;
        if ((m.f & FC) || a > 0x9F) a += 0x60;
    } else {
        if (m.f & FH) a = (a - 6) & 0xFF;
        if (m.f & FC) a -= 0x60;
    }
    m.f &= ~(FH | FZ);
    if (a & 0x100) m.f |= FC;
    a &= 0xFF;
    if (!a) m.f |= FZ;
    m.a = (u8)a;
}
// rotates on A (Z always cleared)
static inline void op_rlca(Machine& m) { u8 c = m.a >> 7; m.a = (u8)((m.a << 1) | c); m.f = c ? FC : 0; }
static inline void op_rrca(Machine& m) { u8 c = m.a & 1; m.a = (u8)((m.a >> 1) | (c << 7)); m.f = c ? FC : 0; }
static inline void op_rla(Machine& m) { u8 c = m.a >> 7; m.a = (u8)((m.a << 1) | ((m.f & FC) ? 1 : 0)); m.f = c ? FC : 0; }
static inline void op_rra(Machine& m) { u8 c = m.a & 1; m.a = (u8)((m.a >> 1) | ((m.f & FC) ? 0x80 : 0)); m.f = c ? FC : 0; }
// CB prefix
static inline u8 cb_rlc(Machine& m, u8 v) { u8 c = v >> 7; v = (u8)((v << 1) | c); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_rrc(Machine& m, u8 v) { u8 c = v & 1; v = (u8)((v >> 1) | (c << 7)); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_rl(Machine& m, u8 v) { u8 c = v >> 7; v = (u8)((v << 1) | ((m.f & FC) ? 1 : 0)); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_rr(Machine& m, u8 v) { u8 c = v & 1; v = (u8)((v >> 1) | ((m.f & FC) ? 0x80 : 0)); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_sla(Machine& m, u8 v) { u8 c = v >> 7; v = (u8)(v << 1); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_sra(Machine& m, u8 v) { u8 c = v & 1; v = (u8)((v >> 1) | (v & 0x80)); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline u8 cb_swap(Machine& m, u8 v) { v = (u8)((v >> 4) | (v << 4)); m.f = v ? 0 : FZ; return v; }
static inline u8 cb_srl(Machine& m, u8 v) { u8 c = v & 1; v = (u8)(v >> 1); m.f = (v ? 0 : FZ) | (c ? FC : 0); return v; }
static inline void cb_bit(Machine& m, int bit, u8 v) { m.f = (m.f & FC) | FH | ((v & (1 << bit)) ? 0 : FZ); }

// STOP: CGB speed switch when armed, otherwise treated as a low power wait.
static inline void op_stop(Machine& m) {
    m.sync();
    m.force_event();
    if (m.cgb && (m.io[0x4D] & 1)) {
        m.double_speed = !m.double_speed;
        m.io[0x4D] = (u8)(m.double_speed ? 0x80 : 0);
        m.div = 0;
    }
}

} // namespace gb
