// Deterministic automatic input used by the code-discovery session and by the
// native-vs-emulation verification (both sides must press the same buttons).
#pragma once
#include "gb.h"

namespace gb {

inline u8 autoplay_buttons(u64 frame, u32 seed = 1) {
    if (frame < 90) return 0;
    u8 b = 0;
    u64 f = frame - 90;
    if (f % 150 < 6) b |= BTN_START;
    if (f % 37 < 4) b |= BTN_A;
    if (f % 211 < 5) b |= BTN_B;
    if (f % 523 < 3) b |= BTN_SELECT;
    // pseudo-random d-pad held for 24-frame slices
    u32 x = (u32)(f / 24) * 2654435761u ^ seed * 40503u;
    x ^= x >> 13; x *= 0x5bd1e995; x ^= x >> 15;
    static const u8 dirs[8] = {0, BTN_RIGHT, BTN_LEFT, BTN_UP, BTN_DOWN, BTN_RIGHT, BTN_LEFT, 0};
    b |= dirs[x & 7];
    return b;
}

} // namespace gb
