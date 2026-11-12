// Static recompiler back end: SM83 -> C++.
#pragma once
#include "analysis.h"
#include <map>

namespace nb {

struct Hook { u32 off; std::string func; };

struct CodegenOptions {
    std::string game_name = "Game";
    std::vector<Hook> hooks;          // user hooks (MODIFICAR JOGO)
    bool hooks_file = false;          // project has mods/hooks.cpp
    bool sgb = false;                 // game converted with Super Game Boy features
};

struct CodegenResult {
    std::vector<std::string> files;   // generated .cpp files (relative to out dir)
    int n_instr = 0;
    int n_pages = 0;
    int n_ram_instr = 0;
    int n_ram_snippets = 0;
};

// Writes C++ into out_dir (must exist). Also copies rom.bin and rom_data.S.
bool generate_cpp(const Analysis& a, const std::string& out_dir, const CodegenOptions& opt, CodegenResult& res, std::string& err);

} // namespace nb
