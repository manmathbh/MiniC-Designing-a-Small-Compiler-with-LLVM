#include "minic/Sema.h"
#include "minic/AST.h"
#include <stdexcept>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Sema.cpp — Semantic analysis pass.
//
// TWO-PASS DESIGN:
//   Pass 1: Walk all FuncDecl nodes, record (name → arity) in funcs_.
//           This is done BEFORE checking any function body, so mutual
//           recursion (f calls g, g calls f) works without forward declarations.
//   Pass 2: Check each function body with a scope stack.
//
// SCOPE STACK:
//   scopes_ is a vector of maps. The BACK of the vector is the innermost scope.
//   pushScope() adds a new empty map; popScope() removes the last one.
//   Variable lookup (checkDeclared) searches from back to front — inner scopes
//   shadow outer scopes, matching C semantics.
//
//   Nesting for a function:
//     [function scope: params]        ← pushed by checkFunc
//       [block scope: local vars]     ← pushed by checkStmt(BlockStmt)
//         [inner block: if/while body]
//
//   This means params are visible anywhere inside the function, and inner
//   blocks shadow outer blocks correctly.
//
// SIMPLIFICATION 🏷️: We track only name existence, not type. A real semantic
//   pass would annotate every AST node with its type and check type
//   compatibility at every operator and call site.
//
// SIMPLIFICATION 🏷️: We use dynamic_cast for dispatch — readable but O(n)
//   in the number of node types. A production compiler would use a Visitor
//   pattern or a vtable-based accept() to achieve O(1) dispatch.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string lineStr(int line) {
    return " (line " + std::to_string(line) + ")";
}

// ── Public entry point ────────────────────────────────────────────────────────

void Sema::check(const Program& prog) {
    // ── Pass 1: collect all function signatures ──────────────────────────────
    for (auto& f : prog.funcs) {
        if (funcs_.count(f->name)) {
            throw std::runtime_error(
                "Sema: function '" + f->name + "' redeclared" + lineStr(f->line));
        }
        funcs_[f->name] = {f->name, static_cast<int>(f->params.size())};
    }

    // ── Pass 2: check each function body ────────────────────────────────────
    for (auto& f : prog.funcs) {
        checkFunc(*f);
    }
}

// ── Scope management ─────────────────────────────────────────────────────────

void Sema::pushScope() {
    scopes_.emplace_back(); // push an empty map
}

void Sema::popScope() {
    scopes_.pop_back();
}

// Declare `name` in the CURRENT (innermost) scope.
// Throws if the name is already in the same scope (redeclaration).
// NOTE: Redeclaring in an INNER scope is fine — that's shadowing.
void Sema::declare(const std::string& name, int line) {
    auto& current = scopes_.back();
    if (current.count(name)) {
        throw std::runtime_error(
            "Sema: variable '" + name + "' redeclared in the same scope" + lineStr(line));
    }
    current[name] = true;
}

// Check that `name` is visible in any enclosing scope (inner → outer).
void Sema::checkDeclared(const std::string& name, int line) const {
    for (int i = static_cast<int>(scopes_.size()) - 1; i >= 0; --i) {
        if (scopes_[i].count(name)) return; // found
    }
    throw std::runtime_error(
        "Sema: use of undeclared variable '" + name + "'" + lineStr(line));
}

// ── Function checker ──────────────────────────────────────────────────────────

void Sema::checkFunc(const FuncDecl& f) {
    pushScope(); // function-level scope: holds parameters

    // Parameters are declared in the function scope.
    // We use f.line for the declaration line — no per-param line in the AST.
    for (auto& p : f.params) {
        declare(p, f.line);
    }

    // The body is always a BlockStmt. We call checkStmt which will pushScope/
    // popScope again for the block itself — parameters are still visible because
    // checkDeclared searches all enclosing scopes outward.
    checkStmt(*f.body);

    popScope();
}

// ── Statement checker ─────────────────────────────────────────────────────────

void Sema::checkStmt(const Stmt& s) {

    // VarDeclStmt: check the initialiser FIRST (so "int x = x;" is an error
    // if x wasn't declared previously), then declare the new name.
    if (auto* vs = dynamic_cast<const VarDeclStmt*>(&s)) {
        checkExpr(*vs->init);       // init is checked before x is visible
        declare(vs->name, vs->line); // now x enters scope
        return;
    }

    // AssignStmt: variable must already be declared.
    if (auto* as = dynamic_cast<const AssignStmt*>(&s)) {
        checkDeclared(as->name, as->line);
        checkExpr(*as->value);
        return;
    }

    // BlockStmt: each block gets its own scope.
    if (auto* bs = dynamic_cast<const BlockStmt*>(&s)) {
        pushScope();
        for (auto& stmt : bs->stmts) {
            checkStmt(*stmt);
        }
        popScope();
        return;
    }

    // IfStmt: condition + both branches.
    if (auto* ifs = dynamic_cast<const IfStmt*>(&s)) {
        checkExpr(*ifs->cond);
        checkStmt(*ifs->thenBranch);
        if (ifs->elseBranch) checkStmt(*ifs->elseBranch);
        return;
    }

    // WhileStmt: condition + body.
    if (auto* ws = dynamic_cast<const WhileStmt*>(&s)) {
        checkExpr(*ws->cond);
        checkStmt(*ws->body);
        return;
    }

    // ReturnStmt: check return value expression.
    if (auto* rs = dynamic_cast<const ReturnStmt*>(&s)) {
        checkExpr(*rs->value);
        return;
    }

    // PrintStmt: check argument expression.
    if (auto* ps = dynamic_cast<const PrintStmt*>(&s)) {
        checkExpr(*ps->value);
        return;
    }

    // ExprStmt: check the expression.
    if (auto* es = dynamic_cast<const ExprStmt*>(&s)) {
        checkExpr(*es->expr);
        return;
    }

    // Should never reach here if all Stmt subclasses are handled above.
    throw std::runtime_error("Sema: unknown statement type (internal error)");
}

// ── Expression checker ────────────────────────────────────────────────────────

void Sema::checkExpr(const Expr& e) {

    // Integer literal: always valid.
    if (dynamic_cast<const IntLitExpr*>(&e)) {
        return;
    }

    // Variable reference: must be declared.
    if (auto* ve = dynamic_cast<const VarExpr*>(&e)) {
        checkDeclared(ve->name, ve->line);
        return;
    }

    // Binary expression: check both operands.
    if (auto* be = dynamic_cast<const BinaryExpr*>(&e)) {
        checkExpr(*be->lhs);
        checkExpr(*be->rhs);
        return;
    }

    // Unary expression: check operand.
    if (auto* ue = dynamic_cast<const UnaryExpr*>(&e)) {
        checkExpr(*ue->operand);
        return;
    }

    // Function call: check function exists, check arity, check each argument.
    if (auto* ce = dynamic_cast<const CallExpr*>(&e)) {
        auto it = funcs_.find(ce->callee);
        if (it == funcs_.end()) {
            throw std::runtime_error(
                "Sema: call to undeclared function '" + ce->callee + "'" + lineStr(ce->line));
        }
        int expected = it->second.arity;
        int got      = static_cast<int>(ce->args.size());
        if (got != expected) {
            throw std::runtime_error(
                "Sema: function '" + ce->callee + "' expects " +
                std::to_string(expected) + " argument(s) but got " +
                std::to_string(got) + lineStr(ce->line));
        }
        for (auto& arg : ce->args) {
            checkExpr(*arg);
        }
        return;
    }

    throw std::runtime_error("Sema: unknown expression type (internal error)");
}

} // namespace minic
