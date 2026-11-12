// Headless test runner: runs a ROM in the interpreter and prints serial output
// usage: nbtest rom.gb [frames] [--png out.ppm] [--hash]
#include "../core/gb.h"
#include "../core/autoplay.h"
#include "../core/apu.h"
#include "../core/sgb.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <iterator>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    Machine m; std::string err;
    for (int i = 2; i < argc; i++) if (std::string(argv[i]) == "--sgb") m.sgb_mode = true;
    for (int i = 2; i < argc; i++) if (std::string(argv[i]) == "--wide" && i + 1 < argc) { m.widescreen = true; m.wide_edge = (u8)atoi(argv[++i]); }
    if (!m.load_rom(rom, err)) { printf("ERR %s\n", err.c_str()); return 1; }
    int frames = argc > 2 ? atoi(argv[2]) : 3000;
    const char* ppm = nullptr;
    bool autoin = false, every = false;
    for (int i = 3; i < argc; i++) { if (std::string(argv[i]) == "--auto") autoin = true; if (std::string(argv[i]) == "--every") every = true; }
    for (int i = 3; i < argc; i++) if (std::string(argv[i]) == "--ppm" && i + 1 < argc) ppm = argv[++i];
    unsigned long long acc = 0;
    for (int i = 3; i < argc; i++) if (std::string(argv[i]) == "--gray") {   // test-suite palette
        const u32 g[4] = {0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000};
        memcpy(m.dmg_palette, g, 16); memcpy(m.dmg_obj0, g, 16); memcpy(m.dmg_obj1, g, 16); m.color_correct = false; }
    for (int i = 0; i < frames; i++) {
        if (autoin) m.buttons = autoplay_buttons((u64)i);
        m.run_frame(nullptr);
        m.apu->out.clear();
        if (every) acc = acc * 1099511628211ULL ^ m.state_hash();
        if (m.serial_out.find("Passed") != std::string::npos || m.serial_out.find("Failed") != std::string::npos) {
            if (m.serial_out.find("tests") == std::string::npos || m.serial_out.find("Passed all") != std::string::npos || m.serial_out.find("Failed") != std::string::npos) { }
        }
    }
    printf("%s\n", m.serial_out.c_str());
    // newer Blargg tests report through cartridge RAM: $A001-$A003 = DE B0 61, $A000 = result, text at $A004
    if (m.cart.ram.size() > 8 && m.cart.ram[1] == 0xDE && m.cart.ram[2] == 0xB0 && m.cart.ram[3] == 0x61) {
        std::string t;
        for (size_t i = 4; i < m.cart.ram.size() && m.cart.ram[i]; i++) t += (char)m.cart.ram[i];
        printf("%s\nRAMRESULT=%d %s\n", t.c_str(), m.cart.ram[0], m.cart.ram[0] == 0 ? "Passed" : "Failed");
    }
    printf("frames=%llu hash=%016llx every=%016llx cycles=%llu\n", (unsigned long long)m.frames, (unsigned long long)m.state_hash(), acc, (unsigned long long)m.cycles);
    if (ppm) {
        std::vector<u32> img(m.fb, m.fb + 160 * 144);
        int W = 160, H = 144;
        if (m.widescreen) { img.assign(m.fbw, m.fbw + Machine::WIDE_W * 144); W = Machine::WIDE_W; }
        if (m.sgb) { img.assign(256 * 224, 0); m.sgb->compose(m, img.data()); W = 256; H = 224; }
        FILE* o = fopen(ppm, "wb"); fprintf(o, "P6 %d %d 255\n", W, H);
        for (int i = 0; i < W * H; i++) { u32 c = img[i]; fputc((c >> 16) & 255, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
        fclose(o);
    }
}
