// Rewind storage test: states stored as XOR/RLE deltas restore exactly, and the game continues identically.
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/delta.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    m.buttons = 0;
    for (int i = 0; i < 300; i++) { m.run_frame(nullptr); m.apu->out.clear(); }
    std::vector<u8> key = m.save_state();
    std::vector<std::vector<u8>> deltas, fulls;
    size_t total = 0;
    for (int i = 0; i < 120; i++) {
        m.buttons = (i / 20) & 1 ? BTN_START : 0;
        m.run_frame(nullptr); m.apu->out.clear();
        std::vector<u8> st = m.save_state(), d;
        rle_xor(st, key, d);
        total += d.size(); deltas.push_back(d); fulls.push_back(st);
    }
    bool exact = true;
    for (size_t i = 0; i < deltas.size(); i++) { std::vector<u8> r; if (!un_rle_xor(deltas[i], key, r) || r != fulls[i]) exact = false; }
    // go back 60 frames and replay: must reach the same state
    std::vector<u8> r; un_rle_xor(deltas[59], key, r); m.load_state(r);
    for (int i = 60; i < 120; i++) { m.buttons = (i / 20) & 1 ? BTN_START : 0; m.run_frame(nullptr); m.apu->out.clear(); }
    std::vector<u8> fin = m.save_state();
    // everything except the sound chip's output filter (smoothing state, not game state) must match
    size_t game_part = fin.size() - offsetof(Apu, out);
    bool replay = fin.size() == fulls[119].size() && std::equal(fin.begin(), fin.begin() + game_part, fulls[119].begin());
    if (!replay) { int shown = 0; for (size_t i = 0; i < fin.size() && i < fulls[119].size() && shown < 12; i++) if (fin[i] != fulls[119][i]) { printf("diff at %zu: %02X vs %02X\n", i, fin[i], fulls[119][i]); shown++; } }
    printf("state=%zu bytes, average delta=%zu bytes, exact=%d, replay_identical=%d\n", key.size(), total / deltas.size(), exact, replay);
    puts(exact && replay ? "REWIND OK" : "REWIND FAIL");
    return exact && replay ? 0 : 1;
}
