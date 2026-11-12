// Fallback / emulation interpreter. Its body is GENERATED from core/optable.cpp
// by tools/gen_interp.cpp so that it uses the exact same instruction semantics as
// the C++ produced by the static recompiler.
#include "gb.h"
#include "cpu_ops.h"

namespace gb {
#include "interp_gen.inc"
}
