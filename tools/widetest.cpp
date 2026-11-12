// Widescreen scenery-memory test: the area left of the screen must show what was on screen before.
// usage: widetest scroll_test.gb memory(0/1)
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <map>
using namespace gb;
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    m.widescreen = true; m.wide_edge = 1; m.wide_memory = atoi(argv[2]) != 0;
    std::map<int, std::vector<u32>> col0;     // frame -> screen column x=0 (rows 0..127)
    int F = 260, good = 0, total = 0;
    for (int fr = 1; fr <= F; fr++) {
        m.run_frame(nullptr); m.apu->out.clear();
        std::vector<u32> c(128);
        for (int y = 0; y < 128; y++) c[y] = m.fbw[y * Machine::WIDE_W + Machine::WIDE_X0];
        col0[fr] = c;
    }
    // at frame F, screen x = -d showed at x = 0 d frames earlier (1 pixel per frame)
    for (int d = 2; d <= 40; d++)
        for (int y = 0; y < 128; y++, total++)
            if (m.fbw[y * Machine::WIDE_W + Machine::WIDE_X0 - d] == col0[F - d][y]) good++;
    if (argc > 3) { FILE* o = fopen(argv[3], "wb"); fprintf(o, "P6 %d 144 255\n", Machine::WIDE_W); for (int i = 0; i < Machine::WIDE_W * 144; i++) { u32 c = m.fbw[i]; fputc((c >> 16) & 255, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); } fclose(o); }
    printf("memory=%d matching=%d/%d (%.1f%%)\n", m.wide_memory ? 1 : 0, good, total, 100.0 * good / total);
    return 0;
}
