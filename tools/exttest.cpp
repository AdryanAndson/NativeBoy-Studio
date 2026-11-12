// Extended view (ultrawide / taller picture) test.
// usage: exttest rom frames script out_prefix     script: "f:buttons,..." (hex button mask from frame f on)
// prints, for the last frame: known pixels inside the wide picture, beyond 256 columns, and in the extra rows
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
static void dump(const Machine& m, const char* fn) {
    FILE* o = fopen(fn, "wb"); fprintf(o, "P6 %d %d 255\n", Machine::XW, Machine::XH);
    for (int i = 0; i < Machine::XW * Machine::XH; i++) {
        u32 c = m.fbx[i]; int k = m.xknown[i] ? 256 : 70;
        fputc((((c >> 16) & 255) * k) >> 8, o); fputc((((c >> 8) & 255) * k) >> 8, o); fputc(((c & 255) * k) >> 8, o);
    }
    fclose(o);
}
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    m.widescreen = true; m.wide_edge = 1; m.wide_memory = true; m.wide_extend = true;
    int F = atoi(argv[2]);
    std::vector<std::pair<int, int>> sc;
    for (char* t = strtok(argv[3], ","); t; t = strtok(nullptr, ",")) { int a = 0; unsigned b = 0; if (sscanf(t, "%d:%x", &a, &b) == 2) sc.push_back({a, (int)b}); }
    struct Snap { int wx, wy; u32 row0[160], col0[128]; };
    std::vector<Snap> hist;
    for (int fr = 1; fr <= F; fr++) {
        for (auto& e : sc) if (e.first == fr) m.buttons = (u8)e.second;
        m.run_frame(nullptr); m.apu->out.clear();
        Snap sn; sn.wx = m.world_x; sn.wy = m.world_y;
        for (int x = 0; x < 160; x++) sn.row0[x] = m.fb[x];
        for (int y = 0; y < 128; y++) sn.col0[y] = m.fb[y * 160];
        hist.push_back(sn);
        if (argc > 4 && fr % 60 == 0) { char fn[512]; snprintf(fn, sizeof fn, "%s_%04d.ppm", argv[4], fr); dump(m, fn); }
    }
    int in = 0, ultra = 0, rows = 0, tin = 0, tu = 0, tr = 0;
    for (int Y = 0; Y < Machine::XH; Y++) for (int X = 0; X < Machine::XW; X++) {
        bool k = m.xknown[(size_t)Y * Machine::XW + X];
        int x = X - Machine::XX0, y = Y - Machine::XY0;
        if (y < 0 || y >= 144) { tr++; rows += k; }
        else if (x < -Machine::WIDE_X0 || x >= 160 + Machine::WIDE_X0) { tu++; ultra += k; }
        else { tin++; in += k; }
    }
    // correctness: what the extended view shows behind the camera must be what was on screen there
    int rg = 0, rt = 0, cg = 0, ct = 0;
    for (int d = 1; d <= Machine::XY0; d++)
        for (auto& h : hist) if (h.wx == m.world_x && h.wy == m.world_y - d) {
            for (int x = 0; x < 160; x++) { rt++; rg += m.fbx[(size_t)(Machine::XY0 - d) * Machine::XW + Machine::XX0 + x] == h.row0[x]; }
            break;
        }
    for (int d = Machine::WIDE_X0 + 1; d <= Machine::XX0; d++)
        for (auto& h : hist) if (h.wy == m.world_y && h.wx == m.world_x - d) {
            for (int y = 0; y < 128; y++) { ct++; cg += m.fbx[(size_t)(Machine::XY0 + y) * Machine::XW + Machine::XX0 - d] == h.col0[y]; }
            break;
        }
    printf("rows_match=%d/%d cols_match=%d/%d ", rg, rt, cg, ct);
    printf("wide=%d%% ultra=%d%% rows=%d%% top_ok=%d bot_ok=%d\n", in * 100 / tin, ultra * 100 / tu, rows * 100 / tr, (int)m.wide_top_ok, (int)m.wide_bot_ok);
    return 0;
}
