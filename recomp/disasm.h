// RGBDS-compatible disassembly. The output re-assembles to a byte-identical ROM
// (verified by tests), so it doubles as an editable source project.
#pragma once
#include "analysis.h"

namespace nb {

// label_for names jump/call targets; var_for (optional) names memory operands ($8000+ and I/O registers)
std::string format_instr(const u8* b, u16 addr, const std::function<std::string(u16)>& label_for,
                         const std::function<std::string(u16)>& var_for = nullptr);
std::string var_name(const Analysis& a, u16 addr);   // symbol, hardware register or ""
std::string label_name(const Analysis& a, u32 off);   // "" if no label
// Writes disasm/bank_XX.asm, disasm/main.asm, disasm/build_rom.bat|.sh
bool write_disassembly(const Analysis& a, const std::string& dir, std::string& err);
// Disassembles one function/region for on-screen display.
std::vector<std::string> disasm_lines(const Analysis& a, u32 off, int max_lines);
// Readable C-like translation of one function (for reading, not compiling) and of the whole ROM.
std::vector<std::string> pseudo_c_lines(const Analysis& a, size_t func_index, int max_lines);
bool write_pseudo_c(const Analysis& a, const std::string& path, std::string& err);

} // namespace nb
