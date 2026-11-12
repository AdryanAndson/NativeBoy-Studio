// Code discovery for SM83 ROMs: recursive descent from known entry points,
// plus entry points observed while running the game (trace-guided discovery).
#pragma once
#include "../core/gb.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nb {
using namespace gb;

enum : u8 {
    AF_START = 1,       // an instruction starts here
    AF_CALL = 2,        // call / rst target
    AF_JUMP = 4,        // jump target
    AF_ENTRY = 8,       // entry point (reset / interrupt vector / user)
    AF_TRACED = 16,     // executed during a discovery session
    AF_BODY = 32,       // byte covered by some decoded instruction
    AF_SOUND = 64,      // instruction writes to audio registers
};

struct RamSnippet {     // code executed from RAM, with the bytes observed
    u16 addr;
    std::vector<u8> bytes;
    std::vector<u16> starts;   // observed instruction starts
    std::vector<u8> mask;      // 1 = byte belongs to an observed instruction
};

struct Function {
    u32 off;            // ROM offset
    int bank;
    u16 addr;
    int n_instr = 0;
    int callers = 0;
    bool sound = false;
    bool traced = false;
    std::string name;
};

// Names imported from an RGBDS/no$gmb/BGB symbol file (".sym": "BB:AAAA Name" per line)
struct Symbols {
    std::map<u32, std::string> rom;     // ROM offset -> label
    std::map<u16, std::string> ram;     // address >= $8000 -> variable name
    bool empty() const { return rom.empty() && ram.empty(); }
};
// Parses a .sym file; names are converted to valid RGBDS identifiers. Returns the number of names read.
int load_sym_file(const std::string& text, size_t rom_size, Symbols& out);
std::string hw_register_name(u16 addr);          // "rLCDC", "rLY"... or ""

struct JumpTable {      // table of 16-bit code pointers that follows a call to a dispatcher routine
    u32 call_off;       // the "rst"/"call" that consumes the table
    u32 table_off;      // first byte of the table
    std::vector<u32> targets;
};

struct Analysis {
    std::vector<u8> rom;
    RomInfo info;
    std::vector<u8> fl;                 // AF_* per ROM offset
    std::vector<u32> indirect;          // offsets of "jp hl"
    std::vector<std::pair<u32, u16>> unresolved_far;  // (from offset, target addr) bank unknown
    std::vector<u32> illegal;           // offsets of illegal opcodes reached
    std::vector<RamSnippet> ram_code;
    std::vector<Function> funcs;
    int n_instr = 0, n_traced = 0, n_overlap = 0;
    std::set<u32> extra_entries;        // ROM offsets added by the user / missing-code logs
    Symbols sym;
    std::vector<JumpTable> jump_tables; // found by analyze()
    std::map<u32, u32> table_words;     // ROM offset of a pointer inside a jump table -> target offset
    std::set<u32> dispatchers;          // routines that read a pointer table placed after the call

    static u32 off_of(int bank, u16 addr) { return addr < 0x4000 ? addr : (u32)bank * 0x4000 + (addr - 0x4000); }
    static int bank_of(u32 off) { return off < 0x4000 ? 0 : (int)(off / 0x4000); }
    static u16 addr_of(u32 off) { return off < 0x4000 ? (u16)off : (u16)(0x4000 + (off % 0x4000)); }
    int instr_len(u32 off) const;
};

// trace: per ROM offset, 1 if executed (may be null). ram: RAM instructions seen.
void analyze(Analysis& a, const std::vector<u8>* trace, const std::vector<Machine::RamExec>* ram);

// helper: runs the interpreter with automatic input to discover code
struct DiscoveryResult {
    std::vector<u8> trace;
    std::vector<Machine::RamExec> ram;
    std::vector<std::vector<u8>> vram_snapshots;  // raw VRAM (0x4000 bytes: bank0+bank1) + palettes
    std::vector<u8> vgm;
    int frames = 0;
    // automatic exploration (coverage-guided): extra frames played from saved moments with varied input
    int explore_frames = 0, explore_runs = 0, explore_useful = 0;
    size_t code_bytes_linear = 0, code_bytes_total = 0;   // ROM bytes executed before / after exploring
};
// frames: linear automatic play. explore: extra frame budget for exploration (0 = off).
// progress(f) receives frames done so far (linear + exploration); return false to cancel.
bool run_discovery(const std::vector<u8>& rom, int frames, DiscoveryResult& out, std::string& err,
                   std::function<bool(int)> progress = nullptr, bool sgb = false, int explore = 0);

std::string hex4(unsigned v);
std::string hex2(unsigned v);

} // namespace nb
