#pragma once
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// AST.h — Abstract Syntax Tree node hierarchy.
//
// DESIGN: We use a classic OOP hierarchy with virtual dispatch (accept() for
// a future Visitor pattern). Each node owns its children via unique_ptr —
// ownership is always clear (parent owns child, no cycles), and there are no
// manual deletes. This mirrors how Clang's AST works conceptually, though
// Clang uses a custom allocator (ASTContext) for performance.
//
// SIMPLIFICATION 🏷️: Clang's AST is an immutable, persistent structure with
// source locations on every node. Ours is mutable and lacks source ranges,
// so error messages after parsing are line-number only.
//
// Nodes are split into two hierarchies:
//   Expr — produces a value (has a type in a real compiler)
//   Stmt — has an effect (control flow, declaration, assignment)
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

// ── Forward declarations ───────────────────────────────────────────────────
struct Expr;
struct Stmt;

// ── Expression nodes ────────────────────────────────────────────────────────
struct Expr {
    virtual ~Expr() = default;
    virtual void dump(int indent = 0) const = 0;
};

using ExprPtr = std::unique_ptr<Expr>;

struct IntLitExpr   : Expr { long long value; void dump(int i=0) const override; };
struct VarExpr      : Expr { std::string name; int line; void dump(int i=0) const override; };
struct UnaryExpr    : Expr { char op; ExprPtr operand; void dump(int i=0) const override; };
struct BinaryExpr   : Expr { std::string op; ExprPtr lhs, rhs; void dump(int i=0) const override; };
struct CallExpr     : Expr {
    std::string callee;
    int line;
    std::vector<ExprPtr> args;
    void dump(int i=0) const override;
};

// ── Statement nodes ──────────────────────────────────────────────────────────
struct Stmt {
    virtual ~Stmt() = default;
    virtual void dump(int indent = 0) const = 0;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct VarDeclStmt  : Stmt { std::string name; int line; ExprPtr init; void dump(int i=0) const override; };
struct AssignStmt   : Stmt { std::string name; int line; ExprPtr value; void dump(int i=0) const override; };
struct ReturnStmt   : Stmt { ExprPtr value; void dump(int i=0) const override; };
struct PrintStmt    : Stmt { ExprPtr value; void dump(int i=0) const override; };
struct ExprStmt     : Stmt { ExprPtr expr; void dump(int i=0) const override; };

struct BlockStmt : Stmt {
    std::vector<StmtPtr> stmts;
    void dump(int i=0) const override;
};

struct IfStmt : Stmt {
    ExprPtr cond;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // may be null
    void dump(int i=0) const override;
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    StmtPtr body;
    void dump(int i=0) const override;
};

// ── Function declaration (top-level) ────────────────────────────────────────
struct FuncDecl {
    std::string name;
    int line;
    std::vector<std::string> params;
    std::unique_ptr<BlockStmt> body;
    void dump(int indent = 0) const;
};

// ── Program = list of functions ──────────────────────────────────────────────
struct Program {
    std::vector<std::unique_ptr<FuncDecl>> funcs;
    void dump() const;
};

} // namespace minic
