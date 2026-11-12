// Developer CLI (host tests): discovery + analysis + C++ generation + disassembly
// usage: nbcli convert rom.gb outdir [frames]
#include "../recomp/analysis.h"
#include "../recomp/codegen.h"
#include "../recomp/disasm.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
using namespace nb;

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: nbcli convert rom outdir [frames] [entries]\n"); return 2; }
    std::ifstream f(argv[2], std::ios::binary);
    std::vector<u8> rom((std::istreambuf_iterator<char>(f)), {});
    std::string out = argv[3];
    int frames = argc > 4 ? atoi(argv[4]) : 3600;
    mkdir(out.c_str(), 0755);
    mkdir((out + "/src").c_str(), 0755);
    mkdir((out + "/disasm").c_str(), 0755);
    DiscoveryResult d; std::string err;
    bool sgb = getenv("NB_SGB") != nullptr;
    int explore = getenv("NB_EXPLORE") ? atoi(getenv("NB_EXPLORE")) : 0;
    if (frames > 0 && !run_discovery(rom, frames, d, err, nullptr, sgb, explore)) { fprintf(stderr, "discovery: %s\n", err.c_str()); return 1; }
    if (frames > 0) printf("coverage_linear=%zu coverage_total=%zu explore_runs=%d useful=%d\n", d.code_bytes_linear, d.code_bytes_total, d.explore_runs, d.explore_useful);
    Analysis a;
    a.rom = rom;
    a.rom.resize(((rom.size() + 0x3FFF) / 0x4000) * 0x4000, 0xFF);
    if (argc > 5) {   // entries file: lines "BB:AAAA" (missing-code log)
        std::ifstream ef(argv[5]); std::string line;
        while (std::getline(ef, line)) { unsigned bk, ad; if (sscanf(line.c_str(), " %x:%x", &bk, &ad) == 2) a.extra_entries.insert(Analysis::off_of(bk, ad)); }
        printf("extra entries=%zu\n", a.extra_entries.size());
    }
    {   // symbol file: NB_SYM or <rom>.sym next to the ROM
        std::string sp = getenv("NB_SYM") ? getenv("NB_SYM") : std::string(argv[2]);
        if (!getenv("NB_SYM")) { size_t dot = sp.rfind('.'); sp = (dot == std::string::npos ? sp : sp.substr(0, dot)) + ".sym"; }
        std::ifstream sf(sp); 
        if (sf) { std::string t((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>()); printf("symbols=%d\n", load_sym_file(t, a.rom.size(), a.sym)); }
    }
    analyze(a, frames > 0 ? &d.trace : nullptr, frames > 0 ? &d.ram : nullptr);
    printf("jump_tables=%zu\n", a.jump_tables.size());
    printf("title=%s mbc=%s instr=%d traced=%d funcs=%zu indirect=%zu far_unresolved=%zu ram_snippets=%zu overlap=%d\n",
           a.info.title.c_str(), a.info.mbc_name.c_str(), a.n_instr, a.n_traced, a.funcs.size(), a.indirect.size(),
           a.unresolved_far.size(), a.ram_code.size(), a.n_overlap);
    CodegenOptions opt; opt.game_name = a.info.title; opt.sgb = sgb;
    CodegenResult r;
    if (!generate_cpp(a, out + "/src", opt, r, err)) { fprintf(stderr, "codegen: %s\n", err.c_str()); return 1; }
    printf("generated files=%zu pages=%d instr=%d ram_instr=%d\n", r.files.size(), r.n_pages, r.n_instr, r.n_ram_instr);
    if (!write_disassembly(a, out + "/disasm", err)) { fprintf(stderr, "disasm: %s\n", err.c_str()); return 1; }
    if (!write_pseudo_c(a, out + "/disasm/pseudocodigo.c", err)) { fprintf(stderr, "pseudo-c: %s\n", err.c_str()); return 1; }
    return 0;
}
