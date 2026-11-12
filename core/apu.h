#pragma once
#include "gb.h"
#include <vector>

namespace gb {

// Game Boy APU with band-limited synthesis: every change of a channel's output level is
// inserted at its exact clock cycle as a band-limited step (windowed-sinc kernel), so
// high notes do not alias ("hiss") the way a naive sample-and-hold mixer does.
struct Apu {
    struct Square {
        bool en = false, dac = false, len_en = false;
        int duty = 0, pos = 0, freq = 0, timer = 0, len = 0;
        int vol = 0, env_dir = 0, env_per = 0, env_timer = 0, vol_init = 0;
    } sq[2];
    int sw_per = 0, sw_dir = 0, sw_shift = 0, sw_timer = 0, sw_shadow = 0;
    bool sw_en = false, sw_neg_used = false;
    struct Wave {
        bool en = false, dac = false, len_en = false;
        int freq = 0, timer = 0, pos = 0, len = 0, vol_code = 0;
        u8 sample = 0;
    } wv;
    struct Noise {
        bool en = false, dac = false, len_en = false;
        int len = 0, vol = 0, env_dir = 0, env_per = 0, env_timer = 0, vol_init = 0;
        u16 lfsr = 0x7FFF;
        int timer = 0, div_code = 0, shift = 0;
        bool w7 = false;
    } ns;
    u8 regs[0x30];
    bool on = true;
    int fs_timer = 8192, fs_step = 0;
    u64 clock = 0;               // APU cycles since reset
    int level[4] = {0, 0, 0, 0}; // current digital output of each channel (0-15)
    double outL = 0, outR = 0;   // current analog mix (what the band-limited buffer integrates to)

    // output
    int sample_rate = 48000;
    double cyc_per_sample = 4194304.0 / 48000.0;
    double capL = 0, capR = 0;
    float volume = 1.0f;
    bool mute[4] = {false, false, false, false};
    std::vector<float> out;   // interleaved stereo samples (consumer clears it)

    // band-limited step buffer
    std::vector<float> bufL, bufR;
    u64 buf_base = 0;         // absolute sample index of bufL[0]
    double integL = 0, integR = 0;

    // VGM capture (music extraction)
    bool vgm_rec = false;
    std::vector<u8> vgm_cmds;
    u64 vgm_clock = 0;        // cycles since start of capture
    u64 vgm_last = 0;         // cycle of last written command
    double vgm_samples_written = 0;

    Apu();
    void reset();
    void set_rate(int rate);
    void set_volume(float v);
    void set_mute(int ch, bool m);
    void tick(int t);
    u8 read(u16 addr);
    void write(u16 addr, u8 v);
    void start_vgm();
    std::vector<u8> stop_vgm(const std::string& title);
    void save(std::vector<u8>& s) const;
    const u8* load(const u8* p);

private:
    void run(int t);
    void clock_length();
    void clock_env();
    void clock_sweep();
    int sweep_calc();
    void trigger(int ch);
    void vgm_wait();
    int calc_level(int ch) const;
    void update_level(int ch, u64 t);
    void update_all(u64 t);
    void mix_gains(double g[4][2]) const;
    void remix(u64 t);
    void add_delta(u64 t, double dl, double dr);
    void flush();
    void power_off();
};

} // namespace gb
