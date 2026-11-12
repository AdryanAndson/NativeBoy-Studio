// Input lag test: frames between pressing A and seeing the screen change, with run-ahead 0, 1 and 2.
// Also checks that run-ahead does not change the game itself (state identical to a run without it)
// and that the scenery memory is untouched by the extra frames.
// usage: lagtest lag_test.gb [rom_for_state_check]
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
static std::vector<u8> load(const char* f) { std::ifstream i(f, std::ios::binary); return std::vector<u8>((std::istreambuf_iterator<char>(i)), {}); }
static std::unique_ptr<Machine> boot(const std::vector<u8>& rom, bool wide) {
    auto m = std::make_unique<Machine>(); std::string e;
    if (!m->load_rom(rom, e)) { fprintf(stderr, "%s\n", e.c_str()); exit(1); }
    m->widescreen = wide; m->wide_edge = 1; m->wide_memory = wide; m->wide_extend = wide;
    return m;
}
// one displayed frame, as the player does it
static void step(Machine& m, Machine::RunAhead& ra, int n, u8 buttons) {
    m.ahead_end(ra);
    if ((buttons & ~m.buttons)) { m.iflag |= IRQ_JOYPAD; m.force_event(); }
    m.buttons = buttons;
    m.run_frame(nullptr); m.apu->out.clear();
    m.ahead_begin(ra, n, nullptr, [] {});
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    auto rom = load(argv[1]);
    int lag[3];
    for (int n = 0; n <= 2; n++) {
        auto mp = boot(rom, false); Machine& m = *mp; Machine::RunAhead ra;
        for (int i = 0; i < 30; i++) step(m, ra, n, 0);
        lag[n] = -1;
        for (int i = 0; i < 10; i++) {
            step(m, ra, n, BTN_A);                 // pressed from this displayed frame on
            u32 c = m.fb[72 * 160 + 80];
            int lum = (int)(((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) / 10;
            if (lum < 80) { lag[n] = i; break; }      // the screen turned dark
        }
    }
    printf("atraso_em_quadros: sem=%d run_ahead_1=%d run_ahead_2=%d\n", lag[0], lag[1], lag[2]);
    if (argc > 2) {
        // the game must be exactly the same with and without run-ahead (state after 600 frames, random-ish input)
        auto rom2 = load(argv[2]);
        auto a = boot(rom2, true), b = boot(rom2, true); Machine::RunAhead ra;
        u32 seed = 1;
        for (int i = 0; i < 600; i++) {
            seed = seed * 1103515245 + 12345; u8 btn = (i % 40 < 20) ? (u8)((seed >> 16) & 0xFF) : 0;
            if ((btn & ~a->buttons)) { a->iflag |= IRQ_JOYPAD; a->force_event(); }
            a->buttons = btn; a->run_frame(nullptr); a->apu->out.clear();
            step(*b, ra, 2, btn);
        }
        b->ahead_end(ra);
        // game part of the state byte for byte (the sound chip's block holds unused padding bytes that
        // differ between any two machines), then the sound itself: the next second of audio must be identical
        auto sa = a->save_state(), sb = b->save_state();
        const size_t game = sa.size() - offsetof(Apu, out);
        bool same = sa.size() == sb.size() && std::equal(sa.begin(), sa.begin() + game, sb.begin());
        std::vector<float> oa, ob;
        for (int i = 0; i < 60; i++) {
            a->run_frame(nullptr); oa.insert(oa.end(), a->apu->out.begin(), a->apu->out.end()); a->apu->out.clear();
            b->run_frame(nullptr); ob.insert(ob.end(), b->apu->out.begin(), b->apu->out.end()); b->apu->out.clear();
        }
        bool sound = oa == ob && !oa.empty();
        same = same && sound;
        if (getenv("LAG_DEBUG")) printf("estado_do_jogo_igual=%d som_igual=%d (%zu amostras)\n", (int)std::equal(sa.begin(), sa.begin() + game, sb.begin()), sound, oa.size());
        printf("jogo_identico=%d memoria_cenario: sem=%zu com=%zu camera_igual=%d\n", same, a->world.size(), b->world.size(), a->world_x == b->world_x && a->world_y == b->world_y);
    }
    return 0;
}
