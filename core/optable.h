// Single source of truth for SM83 opcodes: length, timing, disassembly text and
// C++ semantics. The fallback interpreter is generated from this table at build
// time (tools/gen_interp.cpp) and the static recompiler emits the same C++ text,
// so native code and emulation cannot drift apart.
#pragma once
#include <string>
#include <cstdint>

namespace gb {

enum OpKind {
    OK_NORMAL, OK_JP, OK_JP_CC, OK_JR, OK_JR_CC, OK_CALL, OK_CALL_CC,
    OK_RET, OK_RET_CC, OK_RETI, OK_RST, OK_JP_HL, OK_HALT, OK_STOP,
    OK_EI, OK_DI, OK_ILLEGAL
};

struct OpInfo {
    std::string mnem;        // RGBDS syntax; placeholders {n8} {n16} {e8} {a8} {tgt}
    int len = 1;
    int cycles = 4;          // T-cycles (branch not taken)
    int cycles_taken = 4;    // T-cycles when branch taken
    OpKind kind = OK_NORMAL;
    std::string cond;        // C++ condition for conditional branches
    std::string code;        // C++ statements (uses m, n8, n16, e8)
    int rst = 0;
    bool mem_write = false;  // may write memory (used to detect bank switching)
    // M-cycle accurate memory timing: 'pre' cycles elapse before the instruction's data
    // access (it happens on its last M-cycle, like on the hardware) and 'post' after it.
    // Read-modify-write instructions contain an extra internal m.tick(4) between read and write.
    int pre = 0, post = 0;
};

const OpInfo& op_info(uint8_t op);
const OpInfo& cb_info(uint8_t op);

} // namespace gb
