// Verification mode built into every exported game:
//   Game.exe --selftest N report.json
// Runs N frames twice with the same automatic input: once with the recompiled
// native code and once with the reference interpreter, compares the complete
// machine state (registers, RAM, VRAM, OAM, framebuffer) after every frame,
// and reports how much code still needed the fallback interpreter.
#include "../core/gb.h"
#include "../core/autoplay.h"
#include "../core/apu.h"
#include <cstdio>
#include <string>
#include <vector>

gb::NativeResolver* nb_game_resolver();
const unsigned char* nb_game_rom(unsigned* size);
const char* nb_game_name();
int nb_game_native_instr();
int nb_game_ram_instr();
bool nb_game_sgb();

int nb_selftest(int frames, const char* out_path) {
    using namespace gb;
    unsigned sz = 0;
    const unsigned char* data = nb_game_rom(&sz);
    std::vector<u8> rom(data, data + sz);
    Machine nat, ref;
    nat.sgb_mode = ref.sgb_mode = nb_game_sgb();
    std::string err;
    if (!nat.load_rom(rom, err) || !ref.load_rom(rom, err)) {
        FILE* o = fopen(out_path, "w");
        if (o) { fprintf(o, "{\"ok\":false,\"error\":\"%s\"}\n", err.c_str()); fclose(o); }
        return 2;
    }
    NativeResolver* res = nb_game_resolver();
    int diverged_at = -1;
    for (int f = 0; f < frames; f++) {
        u8 btn = autoplay_buttons((u64)f);
        nat.buttons = btn; ref.buttons = btn;
        nat.run_frame(res);
        ref.run_frame(nullptr);
        nat.apu->out.clear(); ref.apu->out.clear();
        if (nat.state_hash() != ref.state_hash()) { diverged_at = f; break; }
    }
    FILE* o = fopen(out_path, "w");
    if (!o) return 3;
    fprintf(o, "{\n  \"ok\": true,\n  \"game\": \"%s\",\n  \"frames\": %d,\n  \"identical\": %s,\n  \"diverged_at\": %d,\n",
            nb_game_name(), frames, diverged_at < 0 ? "true" : "false", diverged_at);
    fprintf(o, "  \"hash\": \"%016llx\",\n", (unsigned long long)nat.state_hash());
    fprintf(o, "  \"native_entries\": %llu,\n  \"fallback_rom_instr\": %llu,\n  \"fallback_ram_instr\": %llu,\n",
            (unsigned long long)nat.fb_stats.native_blocks, (unsigned long long)nat.fb_stats.rom_instr, (unsigned long long)nat.fb_stats.ram_instr);
    fprintf(o, "  \"native_instr\": %d,\n  \"native_ram_instr\": %d,\n  \"serial\": \"", nb_game_native_instr(), nb_game_ram_instr());
    for (char ch : nat.serial_out) { if (ch == '"' || ch == '\\') fputc('\\', o); if (ch == '\n') fputs("\\n", o); else if ((unsigned char)ch >= 32) fputc(ch, o); }
    fprintf(o, "\",\n  \"missing\": [");
    for (size_t i = 0; i < nat.fb_stats.missing.size(); i++)
        fprintf(o, "%s\"%02X:%04X\"", i ? ", " : "", nat.fb_stats.missing[i] >> 16, nat.fb_stats.missing[i] & 0xFFFF);
    fprintf(o, "]\n}\n");
    fclose(o);
    return diverged_at < 0 ? 0 : 1;
}
