#include "apu.h"
#include <cmath>
#include <cstring>

static const double NB_PI = 3.14159265358979323846;

namespace gb {

static const u8 DUTY[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 0, 0, 1}, {1, 0, 0, 0, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 0}};
static const u8 READ_MASK[0x30] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const int NOISE_DIV[8] = {8, 16, 32, 48, 64, 80, 96, 112};

// ---------------------------------------------------------------- band-limited steps
enum { PHASES = 64, TAPS = 16 };
static float g_kernel[PHASES][TAPS];
static bool g_kernel_ready = false;
static void build_kernel() {
    if (g_kernel_ready) return;
    const double cut = 0.92;   // fraction of Nyquist
    for (int p = 0; p < PHASES; p++) {
        double frac = (double)p / PHASES, sum = 0;
        double h[TAPS];
        for (int k = 0; k < TAPS; k++) {
            double x = (k - (TAPS / 2 - 1)) - frac;              // distance from the step position
            double sinc = x == 0 ? 1.0 : sin(NB_PI * cut * x) / (NB_PI * cut * x);
            double w = 0.5 + 0.5 * cos(NB_PI * x / (TAPS / 2));   // Hann window
            if (fabs(x) >= TAPS / 2) w = 0;
            h[k] = sinc * w;
            sum += h[k];
        }
        for (int k = 0; k < TAPS; k++) g_kernel[p][k] = (float)(h[k] / sum);
    }
    g_kernel_ready = true;
}

Apu::Apu() { build_kernel(); reset(); }

void Apu::reset() {
    sq[0] = Square(); sq[1] = Square(); wv = Wave(); ns = Noise();
    memset(regs, 0, sizeof regs);
    sw_en = false; sw_neg_used = false; on = false; fs_timer = 8192; fs_step = 0;
    sq[0].timer = sq[1].timer = 8192; wv.timer = 4096; ns.timer = 8;
    clock = 0;
    for (int i = 0; i < 4; i++) level[i] = 0;
    outL = outR = 0; integL = integR = 0; capL = capR = 0;
    bufL.clear(); bufR.clear(); buf_base = 0;
    static const u8 wave_init[16] = {0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C, 0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA};
    memcpy(regs + 0x20, wave_init, 16);
}

void Apu::set_rate(int rate) {
    flush();
    sample_rate = rate;
    cyc_per_sample = 4194304.0 / rate;
    bufL.clear(); bufR.clear();
    buf_base = (u64)(clock / cyc_per_sample);
    integL = outL; integR = outR;
}

void Apu::set_volume(float v) { volume = v; remix(clock); }
void Apu::set_mute(int ch, bool m) { mute[ch] = m; remix(clock); }

// ---------------------------------------------------------------- mixing
int Apu::calc_level(int ch) const {
    switch (ch) {
    case 0: case 1: { const Square& s = sq[ch]; return (s.en && s.dac && DUTY[s.duty][s.pos]) ? s.vol : 0; }
    case 2: return (wv.en && wv.dac && wv.vol_code) ? (wv.sample >> (wv.vol_code - 1)) : 0;
    default: return (ns.en && ns.dac && !(ns.lfsr & 1)) ? ns.vol : 0;
    }
}

void Apu::mix_gains(double g[4][2]) const {
    u8 nr51 = regs[0x15], nr50 = regs[0x14];
    double gl = (((nr50 >> 4) & 7) + 1) / 8.0 * 0.25 / 15.0 * volume;
    double gr = ((nr50 & 7) + 1) / 8.0 * 0.25 / 15.0 * volume;
    for (int c = 0; c < 4; c++) {
        bool m = mute[c] || !on;
        g[c][0] = (!m && (nr51 & (0x10 << c))) ? gl : 0;
        g[c][1] = (!m && (nr51 & (1 << c))) ? gr : 0;
    }
}

void Apu::remix(u64 t) {
    double g[4][2];
    mix_gains(g);
    double l = 0, r = 0;
    for (int c = 0; c < 4; c++) { l += level[c] * g[c][0]; r += level[c] * g[c][1]; }
    if (l != outL || r != outR) { add_delta(t, l - outL, r - outR); outL = l; outR = r; }
}

void Apu::update_level(int ch, u64 t) {
    int nl = calc_level(ch);
    if (nl != level[ch]) { level[ch] = nl; remix(t); }
}
void Apu::update_all(u64 t) {
    for (int c = 0; c < 4; c++) level[c] = calc_level(c);
    remix(t);
}

void Apu::add_delta(u64 t, double dl, double dr) {
    double s = t / cyc_per_sample;
    u64 i = (u64)s;
    int ph = (int)((s - (double)i) * PHASES);
    if (ph >= PHASES) ph = PHASES - 1;
    long long start = (long long)i - (TAPS / 2 - 1) - (long long)buf_base;
    if (start < 0) {        // event older than what was already output: apply immediately
        integL += dl; integR += dr;
        return;
    }
    size_t need = (size_t)start + TAPS;
    if (bufL.size() < need) { bufL.resize(need, 0.f); bufR.resize(need, 0.f); }
    const float* k = g_kernel[ph];
    for (int j = 0; j < TAPS; j++) { bufL[start + j] += (float)(dl * k[j]); bufR[start + j] += (float)(dr * k[j]); }
}

void Apu::flush() {
    // samples before 'ready' can no longer receive contributions from future events
    long long ready = (long long)(clock / cyc_per_sample) - TAPS;
    long long n = ready - (long long)buf_base;
    if (n <= 0) return;
    size_t avail = bufL.size();
    for (long long j = 0; j < n; j++) {
        if ((size_t)j < avail) { integL += bufL[j]; integR += bufR[j]; }
        double ol = integL - capL, orr = integR - capR;      // DC-blocking high-pass, like the console's output capacitor
        capL = integL - ol * 0.996; capR = integR - orr * 0.996;
        if (out.size() < 48000 * 2) { out.push_back((float)ol); out.push_back((float)orr); }
    }
    if ((size_t)n >= avail) { bufL.clear(); bufR.clear(); }
    else { bufL.erase(bufL.begin(), bufL.begin() + n); bufR.erase(bufR.begin(), bufR.begin() + n); }
    buf_base += (u64)n;
}

// ---------------------------------------------------------------- frame sequencer
void Apu::clock_length() {
    for (int i = 0; i < 2; i++) if (sq[i].len_en && sq[i].len > 0 && --sq[i].len == 0) sq[i].en = false;
    if (wv.len_en && wv.len > 0 && --wv.len == 0) wv.en = false;
    if (ns.len_en && ns.len > 0 && --ns.len == 0) ns.en = false;
}
void Apu::clock_env() {
    for (int i = 0; i < 2; i++) {
        Square& s = sq[i];
        if (s.env_per && --s.env_timer <= 0) {
            s.env_timer = s.env_per;
            if (s.env_dir && s.vol < 15) s.vol++;
            else if (!s.env_dir && s.vol > 0) s.vol--;
        }
    }
    if (ns.env_per && --ns.env_timer <= 0) {
        ns.env_timer = ns.env_per;
        if (ns.env_dir && ns.vol < 15) ns.vol++;
        else if (!ns.env_dir && ns.vol > 0) ns.vol--;
    }
}
int Apu::sweep_calc() {
    int d = sw_shadow >> sw_shift;
    int nf;
    if (sw_dir) { nf = sw_shadow - d; sw_neg_used = true; }
    else nf = sw_shadow + d;
    if (nf > 2047) sq[0].en = false;
    return nf;
}
void Apu::clock_sweep() {
    if (--sw_timer <= 0) {
        sw_timer = sw_per ? sw_per : 8;
        if (sw_en && sw_per) {
            int nf = sweep_calc();
            if (nf <= 2047 && sw_shift) {
                sw_shadow = nf; sq[0].freq = nf;
                regs[0x03] = nf & 0xFF; regs[0x04] = (regs[0x04] & 0xF8) | ((nf >> 8) & 7);
                sweep_calc();
            }
        }
    }
}

void Apu::trigger(int ch) {
    bool extra = (fs_step & 1) != 0;   // next sequencer step does not clock length
    if (ch < 2) {
        Square& s = sq[ch];
        s.en = s.dac;
        if (s.len == 0) { s.len = 64; if (s.len_en && extra) s.len--; }
        s.timer = (2048 - s.freq) * 4;
        s.vol = s.vol_init; s.env_timer = s.env_per;
        if (ch == 0) {
            sw_shadow = s.freq; sw_timer = sw_per ? sw_per : 8;
            sw_en = sw_per || sw_shift;
            sw_neg_used = false;
            if (sw_shift) sweep_calc();
        }
    } else if (ch == 2) {
        wv.en = wv.dac;
        if (wv.len == 0) { wv.len = 256; if (wv.len_en && extra) wv.len--; }
        wv.timer = (2048 - wv.freq) * 2 + 6;
        wv.pos = 0;
    } else {
        ns.en = ns.dac;
        if (ns.len == 0) { ns.len = 64; if (ns.len_en && extra) ns.len--; }
        ns.vol = ns.vol_init; ns.env_timer = ns.env_per;
        ns.lfsr = 0x7FFF;
        ns.timer = NOISE_DIV[ns.div_code] << ns.shift;
    }
}

// runs the channel timers for n cycles (no frame-sequencer event inside)
void Apu::run(int n) {
    for (int c = 0; c < 2; c++) {
        Square& s = sq[c];
        int rem = n;
        u64 tt = clock;
        while (s.timer <= rem) {
            rem -= s.timer; tt += (u64)s.timer;
            s.pos = (s.pos + 1) & 7;
            s.timer = (2048 - s.freq) * 4;
            update_level(c, tt);
        }
        s.timer -= rem;
    }
    {
        int rem = n;
        u64 tt = clock;
        while (wv.timer <= rem) {
            rem -= wv.timer; tt += (u64)wv.timer;
            wv.pos = (wv.pos + 1) & 31;
            u8 byte = regs[0x20 + wv.pos / 2];
            wv.sample = (wv.pos & 1) ? (byte & 0xF) : (byte >> 4);
            wv.timer = (2048 - wv.freq) * 2;
            update_level(2, tt);
        }
        wv.timer -= rem;
    }
    {
        int rem = n;
        u64 tt = clock;
        int period = NOISE_DIV[ns.div_code] << ns.shift;
        while (ns.timer <= rem) {
            rem -= ns.timer; tt += (u64)ns.timer;
            ns.timer = period;
            if (ns.shift < 14) {
                u16 x = (ns.lfsr ^ (ns.lfsr >> 1)) & 1;
                ns.lfsr = (u16)((ns.lfsr >> 1) | (x << 14));
                if (ns.w7) ns.lfsr = (u16)((ns.lfsr & ~0x40) | (x << 6));
                update_level(3, tt);
            }
        }
        ns.timer -= rem;
    }
    clock += (u64)n;
}

void Apu::tick(int t) {
    if (vgm_rec) vgm_clock += (u64)t;
    while (t > 0) {
        int n = t < fs_timer ? t : fs_timer;
        run(n);
        t -= n;
        fs_timer -= n;
        if (fs_timer <= 0) {
            fs_timer += 8192;
            if (on) {
                switch (fs_step) {
                case 0: case 4: clock_length(); break;
                case 2: case 6: clock_length(); clock_sweep(); break;
                case 7: clock_env(); break;
                }
            }
            fs_step = (fs_step + 1) & 7;
            update_all(clock);
        }
    }
    flush();
}

u8 Apu::read(u16 addr) {
    int i = addr - 0xFF10;
    if (i < 0 || i >= 0x30) return 0xFF;
    if (i == 0x16) {
        return (u8)(0x70 | (on ? 0x80 : 0) | (sq[0].en ? 1 : 0) | (sq[1].en ? 2 : 0) | (wv.en ? 4 : 0) | (ns.en ? 8 : 0));
    }
    if (i >= 0x20) return regs[i];
    return regs[i] | READ_MASK[i];
}

void Apu::vgm_wait() {
    double target = vgm_clock * 44100.0 / 4194304.0;
    int n = (int)(target - vgm_samples_written);
    while (n > 0) {
        int w = n > 65535 ? 65535 : n;
        vgm_cmds.push_back(0x61); vgm_cmds.push_back(w & 0xFF); vgm_cmds.push_back(w >> 8);
        n -= w; vgm_samples_written += w;
    }
}

void Apu::power_off() {
    // every register is cleared (as if zero was written), but on the original Game Boy the
    // length counters keep running state
    int l0 = sq[0].len, l1 = sq[1].len, l2 = wv.len, l3 = ns.len;
    for (int k = 0; k < 0x16; k++) { regs[k] = 0; write((u16)(0xFF10 + k), 0); }
    sq[0].len = l0; sq[1].len = l1; wv.len = l2; ns.len = l3;
    sq[0].en = sq[1].en = wv.en = ns.en = false;
    on = false;
}

void Apu::write(u16 addr, u8 v) {
    int i = addr - 0xFF10;
    if (i < 0 || i >= 0x30) return;
    if (vgm_rec) { vgm_wait(); vgm_cmds.push_back(0xB3); vgm_cmds.push_back((u8)i); vgm_cmds.push_back(v); }
    if (i >= 0x20) { regs[i] = v; return; }
    if (!on && i != 0x16) {
        // while powered off only the length counters can be written (original Game Boy)
        if (i == 0x01) sq[0].len = 64 - (v & 63);
        else if (i == 0x06) sq[1].len = 64 - (v & 63);
        else if (i == 0x0B) wv.len = 256 - v;
        else if (i == 0x10) ns.len = 64 - (v & 63);
        return;
    }
    if (i != 0x16) regs[i] = v;
    bool extra = (fs_step & 1) != 0;
    auto len_write = [&](bool& len_en, int& len, bool& en, bool trig) {
        bool was = len_en;
        len_en = (v & 0x40) != 0;
        // enabling the length counter in the first half of a length period clocks it once
        if (!was && len_en && extra && len > 0) { if (--len == 0 && !trig) en = false; }
    };
    switch (i) {
    case 0x00: {
        bool old_neg = sw_dir;
        sw_per = (v >> 4) & 7; sw_dir = (v >> 3) & 1; sw_shift = v & 7;
        if (old_neg && !sw_dir && sw_neg_used) sq[0].en = false;   // leaving negate mode after using it
        break;
    }
    case 0x01: case 0x06: { int c = i == 1 ? 0 : 1; sq[c].duty = v >> 6; sq[c].len = 64 - (v & 63); break; }
    case 0x02: case 0x07: {
        int c = i == 2 ? 0 : 1;
        sq[c].vol_init = v >> 4; sq[c].env_dir = (v >> 3) & 1; sq[c].env_per = v & 7;
        sq[c].dac = (v & 0xF8) != 0; if (!sq[c].dac) sq[c].en = false; break; }
    case 0x03: case 0x08: { int c = i == 3 ? 0 : 1; sq[c].freq = (sq[c].freq & 0x700) | v; break; }
    case 0x04: case 0x09: {
        int c = i == 4 ? 0 : 1;
        sq[c].freq = (sq[c].freq & 0xFF) | ((v & 7) << 8);
        len_write(sq[c].len_en, sq[c].len, sq[c].en, v & 0x80);
        if (v & 0x80) trigger(c);
        break; }
    case 0x0A: wv.dac = (v & 0x80) != 0; if (!wv.dac) wv.en = false; break;
    case 0x0B: wv.len = 256 - v; break;
    case 0x0C: wv.vol_code = (v >> 5) & 3; break;
    case 0x0D: wv.freq = (wv.freq & 0x700) | v; break;
    case 0x0E:
        wv.freq = (wv.freq & 0xFF) | ((v & 7) << 8);
        len_write(wv.len_en, wv.len, wv.en, v & 0x80);
        if (v & 0x80) trigger(2);
        break;
    case 0x10: ns.len = 64 - (v & 63); break;
    case 0x11: ns.vol_init = v >> 4; ns.env_dir = (v >> 3) & 1; ns.env_per = v & 7; ns.dac = (v & 0xF8) != 0; if (!ns.dac) ns.en = false; break;
    case 0x12: ns.shift = v >> 4; ns.w7 = (v & 8) != 0; ns.div_code = v & 7; break;
    case 0x13:
        len_write(ns.len_en, ns.len, ns.en, v & 0x80);
        if (v & 0x80) trigger(3);
        break;
    case 0x16:
        if (!(v & 0x80) && on) power_off();
        else if ((v & 0x80) && !on) {
            on = true; fs_step = 0; fs_timer = 8192;
            sq[0].pos = sq[1].pos = 0; wv.sample = 0;
        }
        break;
    }
    update_all(clock);
}

void Apu::start_vgm() {
    vgm_cmds.clear(); vgm_clock = 0; vgm_last = 0; vgm_samples_written = 0; vgm_rec = true;
    vgm_cmds.push_back(0xB3); vgm_cmds.push_back(0x16); vgm_cmds.push_back(on ? 0x80 : 0);
    for (int i = 0; i < 0x16; i++) {
        u8 v = regs[i];
        if (i == 0x04 || i == 0x09 || i == 0x0E || i == 0x13) v &= 0x7F;   // do not retrigger
        vgm_cmds.push_back(0xB3); vgm_cmds.push_back((u8)i); vgm_cmds.push_back(v);
    }
    for (int i = 0x20; i < 0x30; i++) { vgm_cmds.push_back(0xB3); vgm_cmds.push_back((u8)i); vgm_cmds.push_back(regs[i]); }
}

static void put32(std::vector<u8>& v, size_t off, u32 x) { v[off] = x & 0xFF; v[off + 1] = (x >> 8) & 0xFF; v[off + 2] = (x >> 16) & 0xFF; v[off + 3] = x >> 24; }

std::vector<u8> Apu::stop_vgm(const std::string& title) {
    vgm_wait();
    vgm_rec = false;
    std::vector<u8> f(0x100, 0);
    memcpy(&f[0], "Vgm ", 4);
    put32(f, 0x08, 0x00000161);
    put32(f, 0x18, (u32)vgm_samples_written);
    put32(f, 0x34, 0x100 - 0x34);
    put32(f, 0x80, 4194304);
    f.insert(f.end(), vgm_cmds.begin(), vgm_cmds.end());
    f.push_back(0x66);
    u32 gd3_off = (u32)f.size();
    std::vector<u8> gd3;
    auto wstr = [&](const std::string& s) { for (char ch : s) { gd3.push_back((u8)ch); gd3.push_back(0); } gd3.push_back(0); gd3.push_back(0); };
    wstr(title); wstr(""); wstr("Game Boy"); wstr(""); wstr("Game Boy"); wstr(""); wstr(""); wstr(""); wstr(""); wstr("NativeBoy Studio"); wstr("");
    f.insert(f.end(), {'G', 'd', '3', ' ', 0, 1, 0, 0});
    u32 len = (u32)gd3.size();
    f.push_back(len & 0xFF); f.push_back((len >> 8) & 0xFF); f.push_back((len >> 16) & 0xFF); f.push_back(len >> 24);
    f.insert(f.end(), gd3.begin(), gd3.end());
    put32(f, 0x14, gd3_off - 0x14);
    put32(f, 0x04, (u32)f.size() - 4);
    return f;
}

void Apu::save(std::vector<u8>& s) const {
    const u8* p = (const u8*)this;
    size_t n = offsetof(Apu, out);
    s.insert(s.end(), p, p + n);
}
const u8* Apu::load(const u8* p) {
    size_t n = offsetof(Apu, out);
    float vol = volume; bool mu[4]; memcpy(mu, mute, sizeof mu); int rate = sample_rate;
    memcpy((void*)this, p, n);
    volume = vol; memcpy(mute, mu, sizeof mu);
    sample_rate = rate; cyc_per_sample = 4194304.0 / rate;
    bufL.clear(); bufR.clear();
    buf_base = (u64)(clock / cyc_per_sample);
    integL = outL; integR = outR;
    remix(clock);
    return p + n;
}

} // namespace gb
