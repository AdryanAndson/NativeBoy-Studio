// Widescreen "visited places" test: after leaving a scene and coming back, the side behind the camera
// must show the street again. usage: reloctest reloc_test.gb places(0/1) [out.ppm]
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
    m.widescreen = true; m.wide_edge = 1; m.wide_memory = true; m.wide_places = atoi(argv[2]) != 0;
    std::map<int, std::vector<u32>> col0;   // scroll position -> screen column x=0 (rows 0..127), before the house
    int good = 0, total = 0;
    bool returned = false; int after = 0;
    for (int fr = 1; fr <= 400; fr++) {
        m.run_frame(nullptr); m.apu->out.clear();
        int scx = m.io[0x43];
        bool lcd = m.io[0x40] & 0x80;
        if (!returned && lcd && scx < 160 && scx > 0) {
            std::vector<u32> c(128);
            for (int y = 0; y < 128; y++) c[y] = m.fbw[y * Machine::WIDE_W + Machine::WIDE_X0];
            col0[scx] = c;
        }
        if (scx > 160) returned = true;
        if (returned && ++after == 4) {
            // 4 frames after coming back: x = -d (d = 2..40) is the street seen at scroll scx - d
            for (int d = 2; d <= 40; d++) {
                auto it = col0.find(scx - d);
                if (it == col0.end()) continue;
                for (int y = 0; y < 128; y++, total++) if (m.fbw[y * Machine::WIDE_W + Machine::WIDE_X0 - d] == it->second[y]) good++;
            }
            if (argc > 3) { FILE* o = fopen(argv[3], "wb"); fprintf(o, "P6 %d 144 255\n", Machine::WIDE_W); for (int i = 0; i < Machine::WIDE_W * 144; i++) { u32 c = m.fbw[i]; fputc((c >> 16) & 255, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); } fclose(o); }
            break;
        }
    }
    printf("places=%d recognised=%u matching=%d/%d (%.1f%%)\n", m.wide_places ? 1 : 0, m.reloc_count, good, total, total ? 100.0 * good / total : 0.0);
    return 0;
}
