#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/raw_ostream.h"

// ─────────────────────────────────────────────────────────────────────────────
// StrengthReduction.cpp — Out-of-tree LLVM function pass (New Pass Manager).
//
// WHAT IT DOES:
//   Walks every instruction in a function. For mul/udiv by a constant power
//   of two, replaces the multiply/divide with a cheaper shift:
//
//     x *  4  →  x << 2      (mul → shl)
//     x /  8  →  x >> 3      (udiv → lshr, unsigned only — see note below)
//     4 *  x  →  x << 2      (commutative: constant on left also handled)
//
// WHY SHIFTS ARE CHEAPER (relevant to Grace CPU / Arm Neoverse):
//   On AArch64, integer multiply (MUL) has a throughput of 1 per cycle but
//   a latency of 3 cycles. SHIFT (LSL/LSR) has latency of 1 cycle and
//   throughput of 2 per cycle. For compile-time-constant power-of-two
//   multipliers this transformation is always profitable.
//
// WHY ONLY UDIV (unsigned divide), NOT SDIV (signed divide)?
//   For unsigned division:  x / 8 == x >> 3  for ALL x  ✓
//   For signed division:    (-1) / 8 == 0     but (-1) >> 3 == -1  ✗
//   Arithmetic right shift (ashr) rounds toward -infinity; C's signed division
//   rounds toward zero. They agree only for non-negative x. Proving x ≥ 0
//   requires a range analysis (ValueTracking/computeKnownBits). We conservatively
//   skip sdiv. Flag this in an interview as the key correctness hazard.
//
// HOW THE NEW PASS MANAGER WORKS:
//   1. This file is compiled as a SHARED MODULE (plugin), no libLLVM linkage.
//   2. `opt -load-pass-plugin=./StrengthReduction.so` calls llvmGetPassPluginInfo.
//   3. That function registers "strength-reduction" with PassBuilder.
//   4. `opt -passes="strength-reduction"` then finds and runs our pass.
//
// SIMPLIFICATION 🏷️: We iterate directly over the instruction list (no
//   worklist). A production strength-reduction pass would use a worklist to
//   re-examine instructions made newly profitable by earlier substitutions.
//   For our single-pass toy this is fine — MiniC doesn't generate chained
//   multiplies that need multiple rounds.
// ─────────────────────────────────────────────────────────────────────────────

using namespace llvm;

namespace {

struct StrengthReductionPass : PassInfoMixin<StrengthReductionPass> {

    PreservedAnalyses run(Function& F, FunctionAnalysisManager& /*AM*/) {
        bool changed = false;

        for (BasicBlock& BB : F) {
            // Collect instructions to replace. We cannot modify the list while
            // iterating over it (eraseFromParent invalidates the iterator).
            SmallVector<BinaryOperator*, 8> toReplace;

            for (Instruction& I : BB) {
                if (auto* binOp = dyn_cast<BinaryOperator>(&I)) {
                    if (isCandidateMul(*binOp) || isCandidateUDiv(*binOp)) {
                        toReplace.push_back(binOp);
                    }
                }
            }

            for (BinaryOperator* binOp : toReplace) {
                if (tryReplaceMul(binOp) || tryReplaceUDiv(binOp)) {
                    changed = true;
                }
            }
        }

        // If we changed anything, we invalidated instruction-level analyses
        // (e.g. AliasAnalysis, ScalarEvolution). Signal that to the manager.
        // If unchanged, tell the manager everything is preserved.
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }

    // ── Is this a mul by a constant power of two? ─────────────────────────
    static bool isCandidateMul(const BinaryOperator& I) {
        if (I.getOpcode() != Instruction::Mul) return false;
        // Check RHS constant (most common form: x * 4)
        if (auto* ci = dyn_cast<ConstantInt>(I.getOperand(1)))
            if (ci->getValue().isPowerOf2()) return true;
        // Check LHS constant (commutative: 4 * x)
        if (auto* ci = dyn_cast<ConstantInt>(I.getOperand(0)))
            if (ci->getValue().isPowerOf2()) return true;
        return false;
    }

    // ── Is this a udiv by a constant power of two (> 1)? ─────────────────
    static bool isCandidateUDiv(const BinaryOperator& I) {
        if (I.getOpcode() != Instruction::UDiv) return false;
        if (auto* ci = dyn_cast<ConstantInt>(I.getOperand(1))) {
            auto v = ci->getValue();
            return v.isPowerOf2() && !v.isOne(); // /1 → no change worth making
        }
        return false;
    }

    // ── Replace mul by power-of-two with shl ─────────────────────────────
    // Returns true if a replacement was made.
    static bool tryReplaceMul(BinaryOperator* I) {
        if (I->getOpcode() != Instruction::Mul) return false;

        Value* base = nullptr;
        unsigned shiftAmt = 0;

        // Prefer RHS constant (canonical form), fall back to LHS.
        if (auto* ci = dyn_cast<ConstantInt>(I->getOperand(1))) {
            if (ci->getValue().isPowerOf2()) {
                base     = I->getOperand(0);
                shiftAmt = ci->getValue().logBase2();
            }
        }
        if (!base) {
            if (auto* ci = dyn_cast<ConstantInt>(I->getOperand(0))) {
                if (ci->getValue().isPowerOf2()) {
                    base     = I->getOperand(1);
                    shiftAmt = ci->getValue().logBase2();
                }
            }
        }
        if (!base) return false;

        // Build the replacement: shl base, shiftAmt
        IRBuilder<> builder(I); // inserts before I
        Value* shift = builder.CreateShl(
            base,
            ConstantInt::get(I->getType(), shiftAmt),
            I->getName() + ".shl");

        // Replace all uses of the mul result with the shift result.
        // This also handles the case where the mul feeds into another inst.
        I->replaceAllUsesWith(shift);
        I->eraseFromParent(); // remove the now-unused mul

        return true;
    }

    // ── Replace udiv by power-of-two with lshr ────────────────────────────
    static bool tryReplaceUDiv(BinaryOperator* I) {
        if (I->getOpcode() != Instruction::UDiv) return false;

        auto* ci = dyn_cast<ConstantInt>(I->getOperand(1));
        if (!ci || !ci->getValue().isPowerOf2() || ci->getValue().isOne())
            return false;

        unsigned shiftAmt = ci->getValue().logBase2();

        IRBuilder<> builder(I);
        Value* shift = builder.CreateLShr(
            I->getOperand(0),
            ConstantInt::get(I->getType(), shiftAmt),
            I->getName() + ".lshr");

        I->replaceAllUsesWith(shift);
        I->eraseFromParent();
        return true;
    }

    // Required by PassInfoMixin — false means the pass is not required to run.
    static bool isRequired() { return false; }
};

} // anonymous namespace

// ── Plugin registration ────────────────────────────────────────────────────────
// llvmGetPassPluginInfo is the C-ABI entry point that `opt -load-pass-plugin=`
// looks for by name. Must have external linkage and LLVM_ATTRIBUTE_WEAK so it
// can coexist with other plugins without ODR conflicts.
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION,
        "StrengthReduction",
        "0.1",
        [](PassBuilder& PB) {
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
