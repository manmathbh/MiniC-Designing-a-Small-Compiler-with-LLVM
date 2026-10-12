#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

// ─────────────────────────────────────────────────────────────────────────────
// StrengthReduction.cpp — Out-of-tree LLVM pass (New Pass Manager).
//
// WHAT IT DOES (milestone 7):
//   Walk every instruction in a function. For each multiply instruction
//   (BinaryOperator with opcode Mul) where one operand is a constant power
//   of two, replace it with a left-shift (Shl). Similarly for UDiv by a
//   power of two → logical right-shift (LShr).
//
//   x * 4  →  x << 2
//   x / 8  →  x >> 3  (logical, unsigned interpretation)
//
// WHY THIS IS INTERESTING FOR CPU COMPILERS:
//   On AArch64 (Grace CPU), an integer multiply has latency ~3 cycles while
//   a shift has latency 1 cycle. This transformation is profitable whenever
//   the multiplier is a compile-time constant power of two.
//
// HOW THE NEW PASS MANAGER WORKS:
//   We register a FunctionPass (runs per function) via PassPluginLibraryInfo.
//   The `opt` tool (or our driver) calls PassBuilder::registerPipelineParsingCallback
//   to make the pass available as "strength-reduction" in a pipeline string.
//
// SIMPLIFICATION 🏷️: A production implementation must also check that the
//   division is unsigned (or that the value is non-negative) before converting
//   signed division to logical shift — signed right-shift of negative numbers
//   is arithmetic, not logical. We mark division transforms as TODO until
//   we add that check.
// ─────────────────────────────────────────────────────────────────────────────

using namespace llvm;

namespace {

struct StrengthReductionPass : PassInfoMixin<StrengthReductionPass> {

    PreservedAnalyses run(Function& F, FunctionAnalysisManager& /*AM*/) {
        // Stub — real implementation in Milestone 7.
        // Returns all preserved for now (we changed nothing).
        (void)F;
        return PreservedAnalyses::all();
    }

    // Required by PassInfoMixin to indicate this pass does NOT modify the CFG.
    static bool isRequired() { return false; }
};

} // anonymous namespace

// ── Plugin registration ────────────────────────────────────────────────────
// This is the C ABI entry point that `opt -load-pass-plugin=...` calls.
// It must have external linkage and this exact name.
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "StrengthReduction",
        "0.1",
        [](PassBuilder& PB) {
            // Register under the name "strength-reduction" so we can use it as:
            //   opt -load-pass-plugin=./StrengthReduction.so \
            //       -passes="strength-reduction" input.ll -o output.ll
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager& FPM,
                   ArrayRef<PassBuilder::PipelineElement>) -> bool {
                    if (Name == "strength-reduction") {
                        FPM.addPass(StrengthReductionPass{});
                        return true;
                    }
                    return false;
                });
        }
    };
}
