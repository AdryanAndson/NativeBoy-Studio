// Recording test: writes a GIF and an AVI (MJPEG + PCM) of 300 frames of a ROM.
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/recorder.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
using namespace gb;
int main(int argc, char** argv) {
    if (argc < 4) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    auto mp = std::make_unique<Machine>(); Machine& m = *mp; std::string err;
    if (!m.load_rom(rom, err)) return 1;
    m.apu->set_rate(48000);
    GifRecorder gif; VideoRecorder vid;
    gif.start(argv[2], 320, 288); vid.start(argv[3], 480, 432, 48000);
    for (int i = 0; i < 300; i++) {
        m.buttons = (i / 30) % 2 ? BTN_START : 0;
        m.run_frame(nullptr);
        gif.add_frame(m.fb, 160, 144, 2);
        vid.add_frame(m.fb, 160, 144, 3);
        vid.add_audio(m.apu->out.data(), m.apu->out.size());
        m.apu->out.clear();
    }
    gif.stop(); vid.stop();
    printf("ok\n");
}
