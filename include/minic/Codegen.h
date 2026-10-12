#pragma once
#include "minic/AST.h"

// LLVM headers are only included in Codegen.cpp — this header stays clean.

// ─────────────────────────────────────────────────────────────────────────────
// Codegen.h — LLVM IR generation.
//
// DESIGN: We use the "alloca + mem2reg" strategy:
//   1. For every local variable we emit an `alloca` instruction in the
//      function entry block (stack slot).
//   2. Reads become `load`, writes become `store`.
//   3. After codegen, running LLVM's mem2reg pass promotes all alloca'd
//      variables into SSA virtual registers, inserting phi nodes at join
//      points automatically.
//
// WHY THIS APPROACH?
//   Writing SSA form directly requires computing dominance frontiers and
//   placing phi nodes by hand — that's hundreds of extra lines and a whole
//   algorithm (Cytron et al. 1991). The alloca trick lets us generate correct
//   (but non-optimized) IR first, and then let mem2reg do the SSA conversion.
//   This is EXACTLY what Clang's CodeGen does internally.
//
// SIMPLIFICATION 🏷️: We only handle int (→ i64 in IR). A real codegen must
// handle struct layout, ABI calling conventions, and aggregate types.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

// Stub — implementation filled in Milestone 5.
class Codegen {
public:
    // Will be fully implemented in milestone 5.
    // Returns a string containing the emitted LLVM IR (.ll text).
    std::string generate(const Program& prog);
};

} // namespace minic
