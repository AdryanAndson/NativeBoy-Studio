// Soundtrack replacement test: runs music_test.gb with the pack in tests/roms/musica_test and
// checks which OGG plays when, that it is mixed in, and that the WAV recorder writes a valid file.
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/music.h"
#include "../common/platform.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp;
    std::string err;
    if (!m.load_rom(rom, err)) { printf("ERR %s\n", err.c_str()); return 1; }
    m.apu->set_rate(48000);
    MusicPack pack;
    if (!pack.load(argv[2], err)) { printf("pack: %s\n", err.c_str()); return 1; }
    MusicPlayer mus; mus.rate = 48000;
    WavRecorder wav; wav.start(argv[3], 48000);
    int last = -1; std::string seq; double mixed_energy = 0;
    for (int fr = 0; fr < 200; fr++) {
        m.run_frame(nullptr);
        int v = rd(m, (u16)pack.addr);
        if (v != last) {
            last = v;
            auto it = pack.files.find(v);
            if (it != pack.files.end()) { mus.play(plat::join(pack.dir, it->second), pack.volume); seq += it->second + ";"; }
            else { mus.stop(); seq += "-;"; }
            for (int i = 0; i < 4; i++) m.apu->set_mute(i, mus.playing() && (pack.mute_mask >> i & 1));
        }
        std::vector<float> before = m.apu->out;
        if (mus.playing()) mus.mix(m.apu->out);
        for (size_t i = 0; i < before.size(); i++) mixed_energy += std::fabs(m.apu->out[i] - before[i]);
        wav.add(m.apu->out.data(), m.apu->out.size());
        m.apu->out.clear();
    }
    wav.stop();
    std::vector<u8> w; plat::read_file(argv[3], w);
    bool wav_ok = w.size() > 44 && memcmp(w.data(), "RIFF", 4) == 0 && (w[40] | (w[41] << 8) | (w[42] << 16) | ((u32)w[43] << 24)) == w.size() - 44;
    printf("sequence=%s mixed=%.1f wav=%zu bytes %s error=%s\n", seq.c_str(), mixed_energy, w.size(), wav_ok ? "ok" : "BAD", mus.error.c_str());
    bool ok = seq == "tema1.ogg;tema2.ogg;-;" && mixed_energy > 100 && wav_ok && m.serial_out == "D";
    printf("%s\n", ok ? "MUSIC OK" : "MUSIC FAIL");
    return ok ? 0 : 1;
}
