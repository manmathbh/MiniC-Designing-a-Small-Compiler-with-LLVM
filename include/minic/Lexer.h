#pragma once
#include "minic/Token.h"
#include <optional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Lexer.h — Hand-written lexer (no lex/flex).
//
// DESIGN: The lexer holds a const reference to the source string and a cursor
// (index into it). `nextToken()` advances the cursor and returns one Token.
// This is a "pull" / "lazy" interface — the parser asks for tokens one at a
// time, which is more memory-efficient than eagerly tokenising the whole file.
//
// peek() uses a std::optional buffer: it calls scan() once and caches the
// result. The next call to nextToken() drains that cache. This gives exactly
// 1 token of lookahead without duplicating scanner logic.
//
// SIMPLIFICATION 🏷️: We lex from a fully-loaded string. A production lexer
// (like clang's) operates on a memory-mapped SourceManager buffer and handles
// trigraphs, universal-character-names, and line splicing before lexing.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

class Lexer {
public:
    explicit Lexer(std::string src);

    // Consume and return the next token.
    Token nextToken();

    // Return the next token WITHOUT consuming it (1-token lookahead).
    Token peek();

private:
    std::string         src_;
    std::size_t         pos_  = 0;
    int                 line_ = 1;
    std::optional<Token> peeked_; // peek() buffer

    // Core scanner — called by nextToken() and peek()
    Token scan();

    // Character-level helpers
    bool  atEnd()    const;
    char  current()  const;
    char  advance();
    bool  matchChar(char expected); // consume if next char == expected

    // Token constructors
    void  skipWhitespaceAndComments();
    Token lexNumber();
    Token lexIdentOrKeyword();
    Token makeToken(TokenKind k)  const;
    Token errorToken(char c);
};

} // namespace minic
