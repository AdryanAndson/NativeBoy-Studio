// HD pack authoring test: collect tiles while playing, write the models and the sheet (smoothed),
// "redraw" three cells of the sheet, import it back and load the pack.
// usage: hdtest rom folder
#include "../core/gb.h"
#include "../core/apu.h"
#include "../core/autoplay.h"
#include "../common/video.h"
#include "../common/platform.h"
#include <cstdio>
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
    m.track_pixels = true;
    TileDumper d;
    for (int i = 0; i < 1500; i++) { m.buttons = autoplay_buttons((u64)i); m.run_frame(nullptr); m.apu->out.clear(); d.collect(m); }
    const std::string dir = argv[2];
    int n = d.write(dir, 4, err, true);
    int obj = 0; for (auto& kv : d.meta) obj += kv.second.obj;
    std::vector<uint32_t> sheet; int w = 0, h = 0;
    bool sheet_ok = load_png(plat::join(dir, "folha_hd.png"), sheet, w, h) && w == 16 * 32 && h == ((n + 15) / 16) * 32;
    // smoothing really rounds: the models have more colours than the 4 of the tile
    int many = 0;
    for (auto& kv : d.tiles) { auto big = hd_smooth_tile(kv.second.data(), 4); std::vector<uint32_t> c(big); std::sort(c.begin(), c.end()); if (std::unique(c.begin(), c.end()) - c.begin() > 4) many++; }
    // redraw 3 cells
    for (int k = 0; k < 3; k++) for (int y = 4; y < 28; y++) for (int x = 4; x < 28; x++) sheet[(size_t)y * w + k * 32 + x] = 0xFFFF0000u;
    save_png(plat::join(dir, "folha_hd.png"), sheet.data(), w, h);
    int changed = hd_import_sheet(dir, err);
    int total = 0, redrawn = 0; hd_pack_stats(dir, total, redrawn);
    HdPack pack; bool loaded = pack.load(dir, err);
    int red = 0; for (auto& im : pack.images) if (im[16 * 32 + 16] == 0xFFFF0000u) red++;
    printf("tiles=%d personagens=%d folha=%dx%d suavizados=%d importados=%d total=%d redesenhados=%d pacote=%d imagens_vermelhas=%d\n",
           n, obj, w, h, many, changed, total, redrawn, loaded ? (int)pack.images.size() : -1, red);
    bool ok = n > 20 && sheet_ok && many > n / 4 && changed == 3 && total == n && redrawn == 3 && loaded && (int)pack.images.size() == n && red == 3;
    puts(ok ? "HD OK" : "HD FAIL");
    return ok ? 0 : 1;
}
