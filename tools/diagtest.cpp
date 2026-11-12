// Widescreen test with diag_test.gb: diagonal scrolling where the game writes whole map rows.
// Every pixel shown at the sides of the wide picture (not hidden as stale) is compared with the real world.
// usage: diagtest diag_test.gb <memory 0/1> [rewind]   (rewind: goes back 1 s in time every 2 s, like holding Backspace)
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
static long errs[4][8], erry[8];
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    m.widescreen = true; m.wide_edge = 1; m.wide_memory = atoi(argv[2]) != 0;
    if (!m.load_rom(rom, err)) { printf("%s\n", err.c_str()); return 1; }
    long shown = 0, good = 0, hidden = 0;
    const bool rewind = argc > 3 && !strcmp(argv[3], "rewind");
    std::vector<std::vector<u8>> states;
    int camx = 0, camy = 0, lastx = -1, lasty = -1;
    for (int fr = 0; fr < 900; fr++) {
        if (rewind) {
            states.push_back(m.save_state());
            if (states.size() > 120) states.erase(states.begin());
            if (fr % 120 == 119 && states.size() >= 60) {          // back 60 frames, keeping the remembered scenery
                m.load_state(states[states.size() - 60], true);
                states.resize(states.size() - 60);
            }
        }
        m.run_frame(nullptr); m.apu->out.clear();
        const int sx = m.wline_scx[60], sy = m.wline_scy[60];      // the scroll this picture was drawn with
        if (lastx >= 0) { camx += (s8)(u8)(sx - lastx); camy += (s8)(u8)(sy - lasty); }
        lastx = sx; lasty = sy;
        if (fr < 60 || fr % 3) continue;
        for (int y = 0; y < 144; y++)
            for (int i = 0; i < Machine::WIDE_W; i++) {
                if (i >= Machine::WIDE_X0 && i < Machine::WIDE_X0 + 160) continue;
                const u8 fl = m.wflags[y * Machine::WIDE_W + i];
                if (fl & 3) continue;
                if ((fl & (4 | 16)) && !(fl & 8)) { hidden++; continue; }   // hidden as stale: not shown as scenery
                // (each line uses the scroll it was drawn with: the game changes it while the first lines are drawn)
                const int wx = camx + (s8)(u8)(m.wline_scx[y] - sx) + i - Machine::WIDE_X0, wy = camy + (s8)(u8)(m.wline_scy[y] - sy) + y;
                const int cx = wx >> 3, cy = wy >> 3;                       // (arithmetic shift: floor)
                const int t = ((cx + 2 * cy) & 255) % 15 + 1;
                const u8* d = &m.vram[0][t * 16 + (wy & 7) * 2];
                const int b = 7 - (wx & 7);
                const int ci = (((d[1] >> b) & 1) << 1) | ((d[0] >> b) & 1);
                const u32 want = m.dmg_palette[(m.io[0x47] >> (ci * 2)) & 3];
                const bool ok = m.fbw[y * Machine::WIDE_W + i] == want;
                shown++; good += ok;
                if (!ok && getenv("DIAG_ONE") && !(fl & 8) && i >= 208) {
                    static int shownd = 0;
                    if (shownd++ < 6) {
                        const int px = (i - Machine::WIDE_X0 + sx) & 255, py = (y + sy) & 255, mi = (py >> 3) * 32 + (px >> 3);
                        printf("fr=%d x=%d y=%d mi=%d(r%d c%d) map_tile=%d want_tile=%d W=(%d,%d) exp=(%d,%d) tf=%u scene=%u camx=%d wx=%d\n", fr, i - 48, y, mi, mi >> 5, mi & 31,
                               m.vram[0][0x1800 + mi], t, m.map_tile_wx[0][mi], m.map_tile_wy[0][mi], m.line_cam_x, m.line_cam_y, m.map_tile_frame[0][mi], m.scene_start, camx, m.world_x);
                    }
                }
                if (!ok) { int k = (fl & 8 ? 2 : 0) + (i >= 48); errs[k][(i < 48 ? i : i - 208) / 6]++; erry[y / 18]++; }
            }
    }
    if (getenv("DIAG_DEBUG")) {
        for (int q = 0; q < 4; q++) { printf("%s %s:", q & 2 ? "memoria" : "mapa", q & 1 ? "dir" : "esq"); for (int z = 0; z < 8; z++) printf(" %ld", errs[q][z]); printf("\n"); }
        printf("por faixa de linhas:"); for (int z = 0; z < 8; z++) printf(" %ld", erry[z]); printf("\n");
    }
    printf("%smemory=%d shown=%ld correct=%ld (%.2f%%) hidden=%ld\n", rewind ? "rewind " : "", (int)m.wide_memory, shown, good, shown ? 100.0 * good / shown : 0.0, hidden);
    return 0;
}
