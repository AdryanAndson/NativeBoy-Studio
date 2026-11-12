// Link cable test: two emulated Game Boys in one process, connected through TCP on localhost.
// usage: linktest link_test.gb [port]
#include "../core/gb.h"
#include "../core/apu.h"
#include "../common/link.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
using namespace gb;

static void run(Machine* m, int frames) {
    for (int f = 0; f < frames && m->wram[0][0x10] != 1; f++) { m->run_frame(nullptr); m->apu->out.clear(); }
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int port = argc > 2 ? atoi(argv[2]) : 15738;
    Machine a, b; std::string err;
    if (!a.load_rom(rom, err) || !b.load_rom(rom, err)) { printf("ERR %s\n", err.c_str()); return 1; }
    a.buttons = BTN_A;                  // a drives the clock
    NetLink la, lb;
    if (!lb.host(port, err)) { printf("host: %s\n", err.c_str()); return 1; }
    if (!la.connect_to("127.0.0.1", port, err)) { printf("connect: %s\n", err.c_str()); return 1; }
    for (int i = 0; i < 100 && lb.status() != 2; i++) { lb.update(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    if (lb.status() != 2) { printf("accept failed\n"); return 1; }
    a.link = &la; b.link = &lb;
    std::thread ta(run, &a, 600), tb(run, &b, 600000);
    ta.join();
    tb.join();
    char ra[6] = {0}, rb[6] = {0};
    memcpy(ra, a.wram[0], 5); memcpy(rb, b.wram[0], 5);
    printf("master got=%s slave got=%s exchanged=%llu/%llu\n", ra, rb, la.bytes_exchanged(), lb.bytes_exchanged());
    bool ok = !strcmp(ra, "01234") && !strcmp(rb, "HELLO");
    printf("%s\n", ok ? "LINK OK" : "LINK FAIL");
    return ok ? 0 : 1;
}
