#pragma once
#include "minic/Lexer.h"
#include "minic/AST.h"
#include "minic/Token.h"

// ─────────────────────────────────────────────────────────────────────────────
// Parser.h — Recursive-descent parser.
//
// DESIGN: One private method per grammar rule. The parser holds ONE token of
// lookahead (current_) and calls lexer_.nextToken() to consume it. This is
// LL(1) recursive descent.
//
// For expressions, precedence is encoded *structurally* — each level is its
// own method (parseOrExpr → parseAndExpr → parseCmpExpr → ...). This is more
// verbose than a Pratt parser table, but trivially readable and debuggable.
//
// LEFT RECURSION: Grammar rules like AddExpr = MulExpr { ("+" | "-") MulExpr }
// are inherently left-associative. We implement them with a WHILE LOOP, not
// left-recursion (which would infinite-loop in a recursive-descent parser).
//
// 2-TOKEN LOOKAHEAD: The one ambiguous point is assignment vs. expression-stmt
// when the current token is IDENT. We peek at the *next* token via lexer_.peek()
// to distinguish `x = ...` (assignment) from `x(...)` or `x + ...` (expr).
//
// SIMPLIFICATION 🏷️: Clang's parser recovers from errors and keeps parsing.
// Ours throws std::runtime_error on the first syntax error (panic mode).
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

class Parser {
public:
    explicit Parser(Lexer lex);

    // Entry point: parse the whole program.
    Program parseProgram();

private:
    Lexer lexer_;
    Token current_; // one-token lookahead

    // ── Token management ────────────────────────────────────────────────────
    Token advance();
    Token expect(TokenKind k, const char* ctx);
    bool  check(TokenKind k) const;
    bool  match(TokenKind k);

    // ── Grammar rule methods ─────────────────────────────────────────────────
    std::unique_ptr<FuncDecl>  parseFuncDecl();
    std::unique_ptr<BlockStmt> parseBlock();

    StmtPtr parseStmt();       // dispatch to one of the below
    StmtPtr parseVarDecl();    // "int" IDENT "=" Expr ";"
    StmtPtr parseAssignStmt(); // IDENT "=" Expr ";"
    StmtPtr parseIfStmt();     // "if" "(" Expr ")" Block [ "else" Block ]
    StmtPtr parseWhileStmt();  // "while" "(" Expr ")" Block
    StmtPtr parseReturnStmt(); // "return" Expr ";"
    StmtPtr parsePrintStmt();  // "print" "(" Expr ")" ";"
    StmtPtr parseExprStmt();   // Expr ";"

    // ── Expression precedence ladder (lowest → highest) ──────────────────────
    ExprPtr parseExpr();     // = parseOrExpr()
    ExprPtr parseOrExpr();   // { "||" }
    ExprPtr parseAndExpr();  // { "&&" }
    ExprPtr parseCmpExpr();  // optional comparison
    ExprPtr parseAddExpr();  // { "+" | "-" }
    ExprPtr parseMulExpr();  // { "*" | "/" | "%" }
    ExprPtr parseUnary();    // optional "-" or "!"
    ExprPtr parsePrimary();  // literal | var | call | "(" expr ")"

    void error(const std::string& msg) const;
};

} // namespace minic
