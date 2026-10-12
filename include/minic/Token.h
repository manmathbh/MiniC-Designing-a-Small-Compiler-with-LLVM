#pragma once
#include <string>
#include <variant>

// ─────────────────────────────────────────────────────────────────────────────
// Token.h — The atomic unit produced by the Lexer.
//
// DESIGN: We use an enum class for the tag (type-safe, no implicit int casts)
// and a std::variant for the value (no union footguns, no heap allocation for
// small payloads). INT_LIT carries its integer value; IDENT carries the name
// string; everything else carries nothing (std::monostate).
//
// SIMPLIFICATION 🏷️: A production lexer (e.g., clang::Lexer) also records
// the source location (file, line, column) in every token for rich diagnostics.
// We store only line number to keep the struct small.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

enum class TokenKind {
    // Literals & identifiers
    INT_LIT, IDENT,

    // Keywords
    KW_INT, KW_IF, KW_ELSE, KW_WHILE, KW_RETURN, KW_PRINT,

    // Arithmetic operators
    PLUS, MINUS, STAR, SLASH, PERCENT,

    // Comparison operators
    EQ_EQ, BANG_EQ, LT, LT_EQ, GT, GT_EQ,

    // Logical operators
    AMP_AMP, PIPE_PIPE, BANG,

    // Assignment
    EQ,

    // Delimiters
    LPAREN, RPAREN, LBRACE, RBRACE, COMMA, SEMICOLON,

    // Sentinels
    END_OF_FILE, UNKNOWN
};

// Human-readable name — useful for error messages and AST dumps.
const char* tokenKindName(TokenKind k);

struct Token {
    TokenKind kind;
    int       line;  // 1-indexed source line (for error messages)

    // Value payload: int literal value, identifier string, or nothing.
    std::variant<std::monostate, long long, std::string> value;

    // Convenience accessors
    long long       intVal()  const { return std::get<long long>(value); }
    const std::string& name() const { return std::get<std::string>(value); }

    bool is(TokenKind k)    const { return kind == k; }
    bool isNot(TokenKind k) const { return kind != k; }
};

} // namespace minic
