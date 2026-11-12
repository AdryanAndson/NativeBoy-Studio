// Debugger test: breakpoint stops before the instruction, single steps advance, continue finishes.
// usage: dbgtest jt_test.gb <hex address of StateE>
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
    u16 bpa = (u16)strtol(argv[2], nullptr, 16);
    std::vector<u8> bp(65536, 0); bp[bpa] = 1;
    m.dbg_bp = &bp;
    int fr = 0;
    while (!m.dbg_hit && fr < 100) { m.run_frame(nullptr); m.apu->out.clear(); fr++; }
    bool stop_ok = m.dbg_hit && m.pc == bpa && m.serial_out == "ABCD";
    u16 p0 = m.pc; m.step_one(); u16 p1 = m.pc; m.step_one(); u16 p2 = m.pc;
    bool step_ok = p1 == p0 + 3 && p2 == p1 + 1;     // ld a,[nn] (3 bytes) then inc a (1 byte)
    m.dbg_hit = false; m.dbg_skip = true;
    for (int i = 0; i < 60; i++) { m.run_frame(nullptr); m.apu->out.clear(); if (m.dbg_hit) break; }
    bool end_ok = !m.dbg_hit && m.serial_out == "ABCDEOK";
    printf("stop=%d (pc=%04X serial=%s) step=%d (%04X %04X %04X) end=%d serial=%s\n", stop_ok, p0, "", step_ok, p0, p1, p2, end_ok, m.serial_out.c_str());
    puts(stop_ok && step_ok && end_ok ? "DEBUG OK" : "DEBUG FAIL");
    return stop_ok && step_ok && end_ok ? 0 : 1;
}
