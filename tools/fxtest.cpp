// Renders one frame of a ROM through the video stage: fxtest rom frames filter out.png [shadow] [vivid] [vignette] [buttons_every]
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/video.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    VideoStage vs;
    vs.fx.shadow = argc > 5 && atoi(argv[5]); vs.fx.vivid = argc > 6 ? atoi(argv[6]) : 0; vs.fx.vignette = argc > 7 && atoi(argv[7]);
    int every = argc > 8 ? atoi(argv[8]) : 0;
    m.track_pixels = vs.fx.shadow;
    apply_dmg_palette(m, "cor_verde_vermelho");
    int frames = atoi(argv[2]);
    for (int i = 0; i < frames; i++) { m.buttons = (every && (i / every) % 2) ? BTN_START | BTN_A : 0; m.run_frame(nullptr); m.apu->out.clear(); }
    const uint32_t* px = vs.process(m, filter_from_id(argv[3]), nullptr);
    return save_png(argv[4], px, vs.w, vs.h) ? 0 : 1;
}
