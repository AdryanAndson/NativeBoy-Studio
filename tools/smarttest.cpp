// Smart widescreen test: prints the visible width decision over time.
// usage: smarttest rom.gb frames [edge]   -> one line per 10 frames: frame vis narrow bad_l bad_r menu
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    m.widescreen = true; m.wide_smart = true; m.wide_edge = (u8)(argc > 3 ? atoi(argv[3]) : 1);
    int F = atoi(argv[2]);
    for (int fr = 1; fr <= F; fr++) {
        m.run_frame(nullptr); m.apu->out.clear();
        if (const char* d = getenv("SMART_DUMP")) if (fr % 2 == 0) {
            char fn[512]; snprintf(fn, sizeof fn, "%s/f%04d_%.3f.ppm", d, fr, m.wide_vis);
            FILE* o = fopen(fn, "wb"); fprintf(o, "P6 %d 144 255\n", Machine::WIDE_W);
            for (int i = 0; i < Machine::WIDE_W * 144; i++) { u32 c = m.fbw[i]; fputc((c >> 16) & 255, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
            fclose(o);
        }
        if (fr % 10 == 0) printf("%d %.2f %d %.2f %.2f %d\n", fr, m.wide_vis, m.wide_narrow ? 1 : 0, m.wide_bad_l, m.wide_bad_r, m.wide_menu ? 1 : 0);
    }
    return 0;
}
