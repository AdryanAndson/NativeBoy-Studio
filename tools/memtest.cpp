// Scenery memory tests.
//   memtest sprites sprite_test.gb   objects outside the screen: a walking one is shown, hidden ones are not
//   memtest scout rom                look-ahead: what is predicted beyond the wide picture vs what appears later
//   memtest persist rom              remembered places saved and loaded in a new session
#include "../core/gb.h"
#include "../core/apu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
using namespace gb;
static std::vector<u8> rom;
static std::unique_ptr<Machine> boot(bool extend) {
    auto m = std::make_unique<Machine>(); std::string err;
    if (!m->load_rom(rom, err)) { fprintf(stderr, "%s\n", err.c_str()); exit(1); }
    m->widescreen = true; m->wide_edge = 1; m->wide_memory = true; m->wide_extend = extend;
    return m;
}
static void frame(Machine& m) { m.run_frame(nullptr); m.apu->out.clear(); }
static size_t X(int x, int y) { return (size_t)(y + Machine::XY0) * Machine::XW + x + Machine::XX0; }
// compares remembered/predicted pixels with what the game really shows once they are on screen
// (well inside the screen and away from where the test ROMs wrap their map, where they draw wrong tiles for a moment)
// Ground truth for scroll_test / reloc_test: world column c always uses tile (c mod 15) + 1 (tiles at $8000)
static u32 truth(const Machine& m, int wx, int wy) {
    int c = (int)(((wx >> 3) % 15 + 15) % 15) + 1, tx = wx & 7, ty = wy & 7;
    const u8* d = &m.vram[0][c * 16 + ty * 2];
    int ci = (((d[1] >> (7 - tx)) & 1) << 1) | ((d[0] >> (7 - tx)) & 1);
    return m.dmg_palette[(m.io[0x47] >> (ci * 2)) & 3];
}
// share of the pixels shown outside the original screen (marked as known) that are correct;
// frames where the screen itself is not the street (house, transitions) are skipped
static bool check_truth(const Machine& m, long& good, long& shown) {
    int ok = 0;
    for (int y = 0; y < 128; y += 4) for (int x = 0; x < 160; x += 4) ok += m.fb[y * 160 + x] == truth(m, m.world_x + x, m.world_y + y);
    if (ok < 32 * 40 * 95 / 100) return false;
    for (int y = 0; y < 128; y++) for (int x = -Machine::XX0; x < Machine::XW - Machine::XX0; x++) {
        if (x >= 0 && x < 160) continue;
        if (!m.xknown[X(x, y)] || m.world_x + x < 0) continue;      // (nothing is defined before the start of the level)
        bool ok = m.fbx[X(x, y)] == truth(m, m.world_x + x, m.world_y + y);
        shown++; good += ok;
        if (!ok && getenv("MEMTEST_DEBUG")) {
            static long reg[6]; static int last = -1;
            int r = x < -Machine::WIDE_X0 ? 0 : x < 0 ? 1 : x < 160 + Machine::WIDE_X0 ? 2 : 3;
            reg[r]++;
            if ((int)m.frames != last && m.frames % 50 == 0) { last = (int)m.frames; printf("  quadro %llu erros: ultra_esq=%ld wide_esq=%ld wide_dir=%ld ultra_dir=%ld (scx=%d)\n", (unsigned long long)m.frames, reg[0], reg[1], reg[2], reg[3], m.io[0x43]); }
        }
    }
    return true;
}
static u32 g_cuts = 0;    // scene when the prediction was taken: other scenes number their world differently
static void compare(Machine& m, std::map<std::pair<int, int>, u32>& pred, int& good, int& hits) {
    if (m.wide_cuts != g_cuts) return;
    for (int x = 20; x <= 150; x++) {
        if (m.io[0x43] + x >= 248) break;
        for (int y = 0; y < 128; y++) {
            auto it = pred.find({m.world_x + x, m.world_y + y});
            if (it == pred.end()) continue;
            bool ok = m.fb[y * 160 + x] == it->second;
            hits++; good += ok;
            if (!ok && getenv("MEMTEST_DEBUG") && y == 40) printf("errado: mundo x=%d tela x=%d quadro=%llu\n", m.world_x + x, x, (unsigned long long)m.frames);
            pred.erase(it);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[2], std::ios::binary);
    rom.assign(std::istreambuf_iterator<char>(f), {});
    std::string mode = argv[1];
    if (mode == "sprites") {
        auto mp = boot(true); Machine& m = *mp;
        for (int i = 0; i < 200; i++) frame(m);
        const u32 obj = m.dmg_obj0[3];
        // sprite 0 at screen (224,56): beyond the 256-wide picture; sprite 2 hidden at screen (192,80); sprite 1 parked
        bool walk = m.fbx[X(227, 59)] == obj && m.xknown[X(227, 59)];
        bool hid = m.fbx[X(195, 83)] != obj && m.fbw[83 * Machine::WIDE_W + Machine::WIDE_X0 + 195] != obj;
        bool park = m.fbx[X(-5, 2)] != obj;
        printf("andando=%d escondido_por_salto=%d estacionado=%d\n", walk, hid, park);
        return walk && hid && park ? 0 : 1;
    }
    if (mode == "scout") {
        int horizon_hits[2] = {0, 0}, good[2] = {0, 0}, known[2] = {0, 0}, total[2] = {0, 0};
        for (int use = 0; use < 2; use++) {
            auto mp = boot(true); Machine& m = *mp;
            WideScout sc;
            std::map<std::pair<int, int>, u32> pred;           // world pixel -> predicted colour
            for (int fr = 1; fr <= 420; fr++) {
                frame(m);
                if (use) sc.step(m, 2);
                if (fr == 150) {
                    g_cuts = m.wide_cuts;
                    for (int y = 0; y < 128; y++) for (int x = 160 + Machine::WIDE_X0; x < Machine::XW - Machine::XX0; x++) {
                        total[use]++;
                        if (!m.xknown[X(x, y)]) continue;
                        known[use]++;
                        pred[{m.world_x + x, m.world_y + y}] = m.fbx[X(x, y)];
                    }
                }
                if (fr > 150) compare(m, pred, good[use], horizon_hits[use]);
            }
        }
        printf("sem_olhar_adiante: conhecido=%d%%  com_olhar_adiante: conhecido=%d%% acerto=%d/%d\n",
               known[0] * 100 / std::max(1, total[0]), known[1] * 100 / std::max(1, total[1]), good[1], horizon_hits[1]);
        return 0;
    }
    if (mode == "verdade") {        // everything shown beyond the screen, with look-ahead and with memory from a past session
        std::vector<u8> saved;
        { auto mp = boot(true); Machine& m = *mp; for (int i = 0; i < 330; i++) frame(m); saved = m.wide_export(); }
        const char* names[3] = {"simples", "olhar_adiante", "memoria_salva"};
        for (int v = 0; v < 3; v++) {
            auto mp = boot(true); Machine& m = *mp;
            if (v == 2) m.wide_import(saved);
            WideScout sc;
            long good = 0, shown = 0; int frames_ok = 0;
            for (int fr = 1; fr <= 330; fr++) {
                frame(m);
                if (v == 1) sc.step(m, 2);
                if (fr >= 100 && fr % 5 == 0 && check_truth(m, good, shown)) frames_ok++;
            }
            printf("%s: mostrados=%ld corretos=%ld (%.2f%%) quadros=%d\n", names[v], shown, good, shown ? 100.0 * good / shown : 0.0, frames_ok);
        }
        return 0;
    }
    if (mode == "persist") {
        std::vector<u8> saved;
        { auto mp = boot(true); Machine& m = *mp; for (int i = 0; i < 300; i++) frame(m); saved = m.wide_export(); }
        int res[2];
        for (int use = 0; use < 2; use++) {
            auto mp = boot(true); Machine& m = *mp;
            if (use && !m.wide_import(saved)) { printf("import falhou\n"); return 1; }
            for (int i = 0; i < 60; i++) frame(m);
            int k = 0, t = 0;
            std::map<std::pair<int, int>, u32> pred;
            for (int y = 0; y < 128; y++) for (int x = 160 + Machine::WIDE_X0; x < Machine::XW - Machine::XX0; x++) {
                t++;
                if (!m.xknown[X(x, y)]) continue;
                k++; pred[{m.world_x + x, m.world_y + y}] = m.fbx[X(x, y)];
            }
            res[use] = k * 100 / t;
            g_cuts = m.wide_cuts;
            if (use) {
                int good = 0, hits = 0;
                for (int fr = 61; fr <= 330; fr++) { frame(m); compare(m, pred, good, hits); }
                printf("lugares_reconhecidos=%u acerto=%d/%d ", m.reloc_count, good, hits);
            }
        }
        printf("bytes=%zu ultrawide_direita_conhecida: nova_sessao=%d%% com_memoria_salva=%d%%\n", saved.size(), res[0], res[1]);
        return 0;
    }
    return 2;
}
