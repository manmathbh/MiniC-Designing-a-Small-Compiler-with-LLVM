// tests/test_lexer.cpp — standalone lexer unit test (no LLVM needed).
//
// Build separately:
//   clang++ -std=c++17 -I../include test_lexer.cpp ../src/Lexer.cpp ../src/Token.cpp -o test_lexer
//   ./test_lexer
//
// For each test case we lex a string and check:
//   1. The sequence of token kinds is correct.
//   2. INT_LIT values and IDENT names are correct.
//   3. Error cases throw as expected.
//
// This is a minimal "roll-your-own" test harness — no gtest needed.
// In a real project you'd use gtest or Catch2.

#include "minic/Lexer.h"
#include "minic/Token.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace minic;

// Lex entire input, return all tokens (including EOF).
static std::vector<Token> lexAll(const std::string& src) {
    Lexer lx(src);
    std::vector<Token> toks;
    while (true) {
        Token t = lx.nextToken();
        toks.push_back(t);
        if (t.kind == TokenKind::END_OF_FILE) break;
    }
    return toks;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static int passed = 0, failed = 0;

#define EXPECT(cond, msg)                                             \
    do {                                                              \
        if (!(cond)) {                                                \
            std::cerr << "FAIL [" << msg << "]: " #cond "\n";        \
            ++failed;                                                 \
        } else {                                                      \
            ++passed;                                                 \
        }                                                             \
    } while (false)

// ── Test cases ────────────────────────────────────────────────────────────────

static void test_empty() {
    auto toks = lexAll("");
    EXPECT(toks.size() == 1, "empty: only EOF");
    EXPECT(toks[0].kind == TokenKind::END_OF_FILE, "empty: is EOF");
}

static void test_integer_literal() {
    auto toks = lexAll("123");
    EXPECT(toks.size() == 2, "intlit: count");
    EXPECT(toks[0].kind == TokenKind::INT_LIT, "intlit: kind");
    EXPECT(toks[0].intVal() == 123, "intlit: value");
}

static void test_keywords() {
    auto toks = lexAll("int if else while return print");
    // 6 keywords + EOF
    EXPECT(toks.size() == 7, "keywords: count");
    EXPECT(toks[0].kind == TokenKind::KW_INT,    "keyword int");
    EXPECT(toks[1].kind == TokenKind::KW_IF,     "keyword if");
    EXPECT(toks[2].kind == TokenKind::KW_ELSE,   "keyword else");
    EXPECT(toks[3].kind == TokenKind::KW_WHILE,  "keyword while");
    EXPECT(toks[4].kind == TokenKind::KW_RETURN, "keyword return");
    EXPECT(toks[5].kind == TokenKind::KW_PRINT,  "keyword print");
}

static void test_identifier() {
    auto toks = lexAll("foo _bar x1");
    EXPECT(toks.size() == 4, "ident: count");
    EXPECT(toks[0].kind == TokenKind::IDENT, "ident: kind");
    EXPECT(toks[0].name() == "foo",          "ident: name foo");
    EXPECT(toks[1].name() == "_bar",         "ident: name _bar");
    EXPECT(toks[2].name() == "x1",           "ident: name x1");
}

static void test_operators() {
    auto toks = lexAll("+ - * / % == != < <= > >= && || ! =");
    std::vector<TokenKind> expected = {
        TokenKind::PLUS, TokenKind::MINUS, TokenKind::STAR,
        TokenKind::SLASH, TokenKind::PERCENT,
        TokenKind::EQ_EQ, TokenKind::BANG_EQ,
        TokenKind::LT, TokenKind::LT_EQ,
        TokenKind::GT, TokenKind::GT_EQ,
        TokenKind::AMP_AMP, TokenKind::PIPE_PIPE,
        TokenKind::BANG, TokenKind::EQ,
        TokenKind::END_OF_FILE
    };
    EXPECT(toks.size() == expected.size(), "ops: count");
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT(toks[i].kind == expected[i], "ops: token " + std::to_string(i));
}

static void test_line_comment() {
    auto toks = lexAll("42 // this is a comment\n99");
    EXPECT(toks.size() == 3, "comment: count");
    EXPECT(toks[0].intVal() == 42, "comment: first token");
    EXPECT(toks[1].intVal() == 99, "comment: second token");
    EXPECT(toks[1].line == 2,      "comment: line tracking");
}

static void test_multiline_line_numbers() {
    auto toks = lexAll("a\nb\nc");
    EXPECT(toks[0].line == 1, "lines: a on 1");
    EXPECT(toks[1].line == 2, "lines: b on 2");
    EXPECT(toks[2].line == 3, "lines: c on 3");
}

static void test_peek_does_not_consume() {
    Lexer lx("10 20");
    Token p1 = lx.peek();
    Token p2 = lx.peek();          // peek again — same token
    Token n  = lx.nextToken();     // now consume
    EXPECT(p1.intVal() == 10, "peek: value");
    EXPECT(p2.intVal() == 10, "peek: idempotent");
    EXPECT(n.intVal()  == 10, "peek: nextToken same as peek");
    Token n2 = lx.nextToken();
    EXPECT(n2.intVal() == 20, "peek: advance after consume");
}

static void test_simple_expression() {
    // Lex a realistic snippet
    auto toks = lexAll("int x = 2 + 3;");
    std::vector<TokenKind> expected = {
        TokenKind::KW_INT, TokenKind::IDENT, TokenKind::EQ,
        TokenKind::INT_LIT, TokenKind::PLUS, TokenKind::INT_LIT,
        TokenKind::SEMICOLON, TokenKind::END_OF_FILE
    };
    EXPECT(toks.size() == expected.size(), "expr: count");
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT(toks[i].kind == expected[i], "expr: token " + std::to_string(i));
    EXPECT(toks[1].name() == "x", "expr: ident name");
}

static void test_error_unknown_char() {
    bool threw = false;
    try { lexAll("@"); } catch (const std::runtime_error&) { threw = true; }
    EXPECT(threw, "error: unknown char @");
}

static void test_error_single_ampersand() {
    bool threw = false;
    try { lexAll("&"); } catch (const std::runtime_error&) { threw = true; }
    EXPECT(threw, "error: single &");
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    test_empty();
    test_integer_literal();
    test_keywords();
    test_identifier();
    test_operators();
    test_line_comment();
    test_multiline_line_numbers();
    test_peek_does_not_consume();
    test_simple_expression();
    test_error_unknown_char();
    test_error_single_ampersand();

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
