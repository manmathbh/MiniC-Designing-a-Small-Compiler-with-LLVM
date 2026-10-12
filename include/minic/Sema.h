#pragma once
#include "minic/AST.h"
#include <unordered_map>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Sema.h — Semantic analysis (type checking / name resolution).
//
// DESIGN: A simple two-pass approach:
//   Pass 1 — collect all function signatures (so mutual recursion works).
//   Pass 2 — walk each function body, maintaining a scope stack.
//
// The scope stack is a vector of maps: innermost scope at the back.
// Variable lookup walks from back to front (inner scope shadows outer).
//
// SIMPLIFICATION 🏷️: We have exactly one type (int). A real type checker
// must unify types, handle implicit conversions, and track qualifiers.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

struct FuncSig {
    std::string name;
    int arity; // number of parameters
};

class Sema {
public:
    // Throws std::runtime_error on any semantic error.
    void check(const Program& prog);

private:
    // Function signatures collected in pass 1.
    std::unordered_map<std::string, FuncSig> funcs_;

    // Scope stack: each scope maps name → true (we only track existence, not type).
    std::vector<std::unordered_map<std::string, bool>> scopes_;

    void pushScope();
    void popScope();
    void declare(const std::string& name, int line);
    void checkDeclared(const std::string& name, int line) const;

    void checkFunc(const FuncDecl& f);
    void checkStmt(const Stmt& s);
    void checkExpr(const Expr& e);
};

} // namespace minic
