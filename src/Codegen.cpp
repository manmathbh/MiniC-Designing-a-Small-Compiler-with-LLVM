#include "minic/Codegen.h"
#include "minic/AST.h"

// LLVM IR-building headers — only included here, not in Codegen.h.
// This keeps compilation times down for translation units that only include
// Codegen.h (e.g., main.cpp doesn't need all of LLVM's type hierarchy).
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <string>
#include <stdexcept>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Codegen.cpp — LLVM IR generation.
//
// STRATEGY: "alloca + mem2reg"
//   Every local variable → one alloca instruction in the entry basic block.
//   Reads  → load from that alloca.
//   Writes → store into that alloca.
//   After codegen, running LLVM's mem2reg pass (PromoteMemToReg) converts
//   all alloca/load/store sequences into SSA virtual registers, inserting
//   phi nodes at control-flow join points automatically.
//
// WHY THIS WAY AND NOT DIRECT SSA?
//   Writing SSA directly requires computing dominance frontiers (Cytron 1991)
//   to know WHERE to place phi nodes. That's a full algorithm on its own.
//   The alloca trick produces valid (but un-optimised) IR immediately, then
//   mem2reg does the SSA conversion. THIS IS EXACTLY WHAT CLANG DOES internally.
//   (See clang/lib/CodeGen/CGDecl.cpp: EmitAutoVarDecl uses CreateAlloca.)
//
// REPRESENTATION:
//   MiniC's only type is `int` → LLVM `i64` (64-bit signed integer).
//   Comparison results are `i1` (1-bit bool), zero-extended to `i64` for use
//   in arithmetic. This matches C's implicit bool-to-int conversion.
//
// SIMPLIFICATION 🏷️: && and || evaluate BOTH sides (no short-circuit).
//   Short-circuit requires branching into new basic blocks — identical to
//   if/else lowering. Omitted here for brevity; flag this in an interview.
//
// SIMPLIFICATION 🏷️: Only int/i64. A real codegen must handle struct layout,
//   ABI calling conventions (System V AMD64 ABI for x86-64), float types, etc.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {
namespace {

// ── CodegenImpl: all state for one compilation run ───────────────────────────
struct CodegenImpl {
    llvm::LLVMContext                                ctx;
    std::unique_ptr<llvm::Module>                    mod;
    std::unique_ptr<llvm::IRBuilder<>>               b;   // current insert point

    // Maps function name → llvm::Function* (built in two passes)
    std::unordered_map<std::string, llvm::Function*> funcMap;

    // Per-function: maps variable name → its alloca (stack slot)
    std::unordered_map<std::string, llvm::AllocaInst*> locals;

    // The function currently being compiled (needed by createEntryAlloca)
    llvm::Function* curFn = nullptr;

    // ── Type shorthands ──────────────────────────────────────────────────
    llvm::Type* i64Ty()  { return llvm::Type::getInt64Ty(ctx); }
    llvm::Type* i1Ty()   { return llvm::Type::getInt1Ty(ctx); }
    llvm::Type* voidTy() { return llvm::Type::getVoidTy(ctx); }

    // ── Constructor ─────────────────────────────────────────────────────
    CodegenImpl() {
        mod = std::make_unique<llvm::Module>("minic", ctx);
        b   = std::make_unique<llvm::IRBuilder<>>(ctx);
    }

    // ── Create alloca at the ENTRY block of curFn ────────────────────────
    //
    // KEY DESIGN POINT: mem2reg only promotes allocas that are in the entry
    // basic block of the function (it skips allocas in loops/branches because
    // their lifetime analysis is more complex). So ALL allocas, including those
    // for variables declared inside an if-branch or while-loop, must be placed
    // in the function's entry block.
    //
    // We do this by temporarily creating a second IRBuilder positioned at the
    // very START of the entry block, independent of where we currently are.
    llvm::AllocaInst* createEntryAlloca(llvm::StringRef name) {
        llvm::IRBuilder<> tmp(
            &curFn->getEntryBlock(),
            curFn->getEntryBlock().begin()); // insert before first instruction
        return tmp.CreateAlloca(i64Ty(), /*ArraySize=*/nullptr, name);
    }

    // ── Is the current basic block already terminated? ───────────────────
    //
    // A basic block is terminated once it has a ret, br, or similar instruction.
    // Attempting to insert instructions after a terminator causes LLVM to assert.
    // We guard every emitStmt call with this check to handle dead code after
    // return statements (e.g., `return 0; print(1);` — the print is unreachable).
    bool isTerminated() const {
        auto* bb = b->GetInsertBlock();
        return bb && bb->getTerminator() != nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 1: Declare all functions in the module BEFORE emitting any bodies.
    //
    // This is what allows mutual recursion: when we emit the body of `even()`,
    // `odd()` is already declared in the module so we can emit a call to it.
    // ─────────────────────────────────────────────────────────────────────────
    void declareAll(const Program& prog) {
        // Declare the external runtime function minic_print(i64) -> void.
        // The actual definition lives in runtime/minic_runtime.c, compiled
        // separately and linked by the driver.
        auto* printTy = llvm::FunctionType::get(voidTy(), {i64Ty()}, false);
        auto* printFn = llvm::Function::Create(
            printTy, llvm::Function::ExternalLinkage, "minic_print", mod.get());
        funcMap["__builtin_print"] = printFn; // internal key for print keyword

        // Declare each user function: int f(int, int, ...) -> int
        for (auto& f : prog.funcs) {
            std::vector<llvm::Type*> paramTys(f->params.size(), i64Ty());
            auto* fnTy = llvm::FunctionType::get(i64Ty(), paramTys, false);
            auto* fn   = llvm::Function::Create(
                fnTy, llvm::Function::ExternalLinkage, f->name, mod.get());
            // Name the arguments — makes the IR much more readable.
            std::size_t i = 0;
            for (auto& arg : fn->args()) arg.setName(f->params[i++]);
            funcMap[f->name] = fn;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Pass 2: Emit each function body.
    // ─────────────────────────────────────────────────────────────────────────
    void emitFunc(const FuncDecl& f) {
        curFn = funcMap.at(f.name);
        locals.clear();

        // Create the entry basic block (first BB of every LLVM function).
        auto* entryBB = llvm::BasicBlock::Create(ctx, "entry", curFn);
        b->SetInsertPoint(entryBB);

        // For each parameter: alloca + store.
        // Treating params as alloca'd locals lets mem2reg handle them the same
        // way as local variables — clean, uniform handling.
        std::size_t i = 0;
        for (auto& arg : curFn->args()) {
            auto* alloca = createEntryAlloca(f.params[i++]);
            b->CreateStore(&arg, alloca);
            locals[arg.getName().str()] = alloca;
        }

        // Emit the function body (always a BlockStmt).
        emitStmt(*f.body);

        // If the function fell off the end without a return, emit ret 0.
        // This matches C's UB-but-common "falling off int main()" behavior.
        if (!isTerminated()) {
            b->CreateRet(llvm::ConstantInt::get(i64Ty(), 0));
        }

        // Verify the generated function. Catches malformed IR (e.g. missing
        // terminator, wrong types) before they become cryptic linker errors.
        std::string err;
        llvm::raw_string_ostream errStream(err);
        if (llvm::verifyFunction(*curFn, &errStream)) {
            errStream.flush();
            throw std::runtime_error(
                "Codegen: LLVM IR verify failed for '" + f.name + "':\n" + err);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Statement emitters
    // ─────────────────────────────────────────────────────────────────────────
    void emitStmt(const Stmt& s) {
        if (isTerminated()) return; // dead code — skip silently

        if (auto* vs = dynamic_cast<const VarDeclStmt*>(&s)) {
            // Evaluate initialiser first, so "int x = x;" is an error at sema
            // (x isn't declared yet). Then alloca + store.
            llvm::Value* init   = emitExpr(*vs->init);
            auto*        alloca = createEntryAlloca(vs->name);
            b->CreateStore(init, alloca);
            locals[vs->name] = alloca;
            return;
        }

        if (auto* as = dynamic_cast<const AssignStmt*>(&s)) {
            llvm::Value*       val    = emitExpr(*as->value);
            llvm::AllocaInst*  alloca = locals.at(as->name);
            b->CreateStore(val, alloca);
            return;
        }

        if (auto* bs = dynamic_cast<const BlockStmt*>(&s)) {
            for (auto& stmt : bs->stmts) {
                emitStmt(*stmt);
                if (isTerminated()) break; // stop at first return/br
            }
            return;
        }

        if (auto* ifs = dynamic_cast<const IfStmt*>(&s)) {
            emitIf(*ifs);
            return;
        }

        if (auto* ws = dynamic_cast<const WhileStmt*>(&s)) {
            emitWhile(*ws);
            return;
        }

        if (auto* rs = dynamic_cast<const ReturnStmt*>(&s)) {
            llvm::Value* val = emitExpr(*rs->value);
            b->CreateRet(val);
            // NOTE: after CreateRet, isTerminated() = true, so subsequent
            // statements in this block will be skipped.
            return;
        }

        if (auto* ps = dynamic_cast<const PrintStmt*>(&s)) {
            llvm::Value* val = emitExpr(*ps->value);
            b->CreateCall(funcMap.at("__builtin_print"), {val});
            return;
        }

        if (auto* es = dynamic_cast<const ExprStmt*>(&s)) {
            emitExpr(*es->expr); // result intentionally discarded
            return;
        }

        throw std::runtime_error("Codegen: unhandled statement type (internal error)");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // If/else — emitted as a diamond in the CFG:
    //
    //        [current block]
    //         /            \
    //     [then_bb]      [else_bb]
    //         \            /
    //        [merge_bb]
    //
    // If there is no else clause, else_bb IS merge_bb (one fewer block).
    // After both branches, mem2reg inserts phi nodes at merge_bb to merge
    // values that were assigned in either branch.
    // ─────────────────────────────────────────────────────────────────────────
    void emitIf(const IfStmt& ifs) {
        llvm::Value* condVal  = emitExpr(*ifs.cond);
        // Condition is i64; convert to i1 for the conditional branch.
        llvm::Value* condBool = b->CreateICmpNE(
            condVal, llvm::ConstantInt::get(i64Ty(), 0), "if.cond");

        auto* thenBB  = llvm::BasicBlock::Create(ctx, "if.then",  curFn);
        auto* mergeBB = llvm::BasicBlock::Create(ctx, "if.merge", curFn);
        auto* elseBB  = ifs.elseBranch
                      ? llvm::BasicBlock::Create(ctx, "if.else", curFn)
                      : mergeBB;

        b->CreateCondBr(condBool, thenBB, elseBB);

        // Emit then-branch
        b->SetInsertPoint(thenBB);
        emitStmt(*ifs.thenBranch);
        if (!isTerminated()) b->CreateBr(mergeBB);

        // Emit else-branch (if present)
        if (ifs.elseBranch) {
            b->SetInsertPoint(elseBB);
            emitStmt(*ifs.elseBranch);
            if (!isTerminated()) b->CreateBr(mergeBB);
        }

        b->SetInsertPoint(mergeBB);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // While loop — emitted as a loop in the CFG:
    //
    //   [current block] → [cond_bb]
    //                          |
    //              true /      \ false
    //           [body_bb]   [after_bb]
    //               |
    //           (back edge) → [cond_bb]
    //
    // This is a natural loop with cond_bb as the loop header.
    // LLVM's loop analysis passes will recognise this structure.
    //
    // WHAT IS A BASIC BLOCK?
    //   A straight-line sequence of instructions with exactly one entry (the
    //   label) and one exit (the terminator: br, ret, etc.). No jumps into the
    //   middle of a block, no jumps out except at the end. The three BBs here
    //   (cond, body, after) each satisfy this invariant.
    // ─────────────────────────────────────────────────────────────────────────
    void emitWhile(const WhileStmt& ws) {
        auto* condBB  = llvm::BasicBlock::Create(ctx, "while.cond", curFn);
        auto* bodyBB  = llvm::BasicBlock::Create(ctx, "while.body", curFn);
        auto* afterBB = llvm::BasicBlock::Create(ctx, "while.after", curFn);

        b->CreateBr(condBB); // fall into the condition block

        // ── Condition block ──
        b->SetInsertPoint(condBB);
        llvm::Value* condVal  = emitExpr(*ws.cond);
        llvm::Value* condBool = b->CreateICmpNE(
            condVal, llvm::ConstantInt::get(i64Ty(), 0), "while.cond.bool");
        b->CreateCondBr(condBool, bodyBB, afterBB);

        // ── Body block ──
        b->SetInsertPoint(bodyBB);
        emitStmt(*ws.body);
        if (!isTerminated()) b->CreateBr(condBB); // back edge

        // ── Continue after loop ──
        b->SetInsertPoint(afterBB);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Expression emitter — returns an llvm::Value* (i64 in all cases).
    // ─────────────────────────────────────────────────────────────────────────
    llvm::Value* emitExpr(const Expr& e) {

        // Integer literal → constant i64
        if (auto* lit = dynamic_cast<const IntLitExpr*>(&e)) {
            return llvm::ConstantInt::get(i64Ty(), lit->value, /*isSigned=*/true);
        }

        // Variable reference → load from its alloca
        if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
            auto it = locals.find(ve->name);
            if (it == locals.end())
                throw std::runtime_error("Codegen: variable '" + ve->name + "' not in locals (internal error)");
            return b->CreateLoad(i64Ty(), it->second, ve->name);
        }

        // Unary operator
        if (auto* ue = dynamic_cast<const UnaryExpr*>(&e)) {
            llvm::Value* operand = emitExpr(*ue->operand);
            if (ue->op == '-') {
                return b->CreateNeg(operand, "neg");
            }
            if (ue->op == '!') {
                // !x = (x == 0) → i1, then zext to i64
                llvm::Value* isZero = b->CreateICmpEQ(
                    operand, llvm::ConstantInt::get(i64Ty(), 0), "lnot");
                return b->CreateZExt(isZero, i64Ty(), "lnot.ext");
            }
            throw std::runtime_error("Codegen: unknown unary op");
        }

        // Binary operator
        if (auto* be = dynamic_cast<const BinaryExpr*>(&e)) {
            return emitBinary(*be);
        }

        // Function call
        if (auto* ce = dynamic_cast<const CallExpr*>(&e)) {
            auto it = funcMap.find(ce->callee);
            if (it == funcMap.end())
                throw std::runtime_error("Codegen: undeclared function '" + ce->callee + "' (should have been caught by sema)");
            std::vector<llvm::Value*> args;
            args.reserve(ce->args.size());
            for (auto& arg : ce->args) args.push_back(emitExpr(*arg));
            return b->CreateCall(it->second, args, ce->callee + ".ret");
        }

        throw std::runtime_error("Codegen: unknown expression type (internal error)");
    }

    llvm::Value* emitBinary(const BinaryExpr& be) {
        const std::string& op = be.op;

        // ── Arithmetic ────────────────────────────────────────────────────
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
            llvm::Value* lhs = emitExpr(*be.lhs);
            llvm::Value* rhs = emitExpr(*be.rhs);
            if (op == "+") return b->CreateAdd(lhs,  rhs, "add");
            if (op == "-") return b->CreateSub(lhs,  rhs, "sub");
            if (op == "*") return b->CreateMul(lhs,  rhs, "mul");
            if (op == "/") return b->CreateSDiv(lhs, rhs, "div"); // signed divide
            if (op == "%") return b->CreateSRem(lhs, rhs, "rem"); // signed remainder
        }

        // ── Comparison — result is i1, zero-extended to i64 ──────────────
        // WHY ZEXT? Comparison yields i1 (1 bit). MiniC treats int as i64.
        // zext i1 → i64 gives 0 or 1, matching C's implicit bool conversion.
        if (op == "==" || op == "!=" || op == "<" ||
            op == "<=" || op == ">"  || op == ">=") {
            llvm::Value* lhs = emitExpr(*be.lhs);
            llvm::Value* rhs = emitExpr(*be.rhs);
            llvm::Value* cmp;
            if      (op == "==") cmp = b->CreateICmpEQ (lhs, rhs, "eq");
            else if (op == "!=") cmp = b->CreateICmpNE (lhs, rhs, "ne");
            else if (op == "<")  cmp = b->CreateICmpSLT(lhs, rhs, "lt");
            else if (op == "<=") cmp = b->CreateICmpSLE(lhs, rhs, "le");
            else if (op == ">")  cmp = b->CreateICmpSGT(lhs, rhs, "gt");
            else                 cmp = b->CreateICmpSGE(lhs, rhs, "ge");
            return b->CreateZExt(cmp, i64Ty(), "cmp.ext");
        }

        // ── Logical AND / OR (EAGER — no short-circuit) ───────────────────
        // SIMPLIFICATION 🏷️: Both sides always evaluated.
        // Short-circuit needs the same if/else BB structure as IfStmt.
        if (op == "&&") {
            llvm::Value* lv  = emitExpr(*be.lhs);
            llvm::Value* rv  = emitExpr(*be.rhs);
            llvm::Value* lb  = b->CreateICmpNE(lv, llvm::ConstantInt::get(i64Ty(), 0), "land.lhs");
            llvm::Value* rb  = b->CreateICmpNE(rv, llvm::ConstantInt::get(i64Ty(), 0), "land.rhs");
            llvm::Value* res = b->CreateAnd(lb, rb, "land");
            return b->CreateZExt(res, i64Ty(), "land.ext");
        }
        if (op == "||") {
            llvm::Value* lv  = emitExpr(*be.lhs);
            llvm::Value* rv  = emitExpr(*be.rhs);
            llvm::Value* lb  = b->CreateICmpNE(lv, llvm::ConstantInt::get(i64Ty(), 0), "lor.lhs");
            llvm::Value* rb  = b->CreateICmpNE(rv, llvm::ConstantInt::get(i64Ty(), 0), "lor.rhs");
            llvm::Value* res = b->CreateOr(lb, rb, "lor");
            return b->CreateZExt(res, i64Ty(), "lor.ext");
        }

        throw std::runtime_error("Codegen: unknown binary op '" + op + "'");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Top-level: generate IR for the whole program, return it as a string.
    // ─────────────────────────────────────────────────────────────────────────
    std::string run(const Program& prog) {
        declareAll(prog);
        for (auto& f : prog.funcs) emitFunc(*f);

        // Stringify the module: module_.print() writes LLVM IR text format.
        std::string ir;
        llvm::raw_string_ostream os(ir);
        mod->print(os, /*AssemblyAnnotationWriter=*/nullptr);
        os.flush();
        return ir;
    }
};

} // anonymous namespace

// ── Public entry point ────────────────────────────────────────────────────────
std::string Codegen::generate(const Program& prog) {
    CodegenImpl impl;
    return impl.run(prog);
}

} // namespace minic
