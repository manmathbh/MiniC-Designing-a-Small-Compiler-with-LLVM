// tests/test_parser.cpp — standalone parser unit tests (no LLVM needed).
//
// Build:
//   clang++ -std=c++17 -I../include test_parser.cpp \
//       ../src/Token.cpp ../src/Lexer.cpp ../src/Parser.cpp ../src/AST.cpp \
//       -o test_parser && ./test_parser
//
// Strategy: parse a string → check the shape of the resulting AST.
// We cast nodes with dynamic_cast (RTTI is on for our test binary — only
// the pass plugin needs -fno-rtti).

#include "minic/Lexer.h"
#include "minic/Parser.h"
#include "minic/AST.h"
#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace minic;

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

static Program parse(const std::string& src) {
    Lexer  lx(src);
    Parser p(std::move(lx));
    return p.parseProgram();
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_empty_function() {
    auto prog = parse("int main() { return 0; }");
    EXPECT(prog.funcs.size() == 1,           "one function");
    EXPECT(prog.funcs[0]->name == "main",    "function name is main");
    EXPECT(prog.funcs[0]->params.empty(),    "no params");
    EXPECT(prog.funcs[0]->body->stmts.size() == 1, "one stmt in body");
    auto* ret = dynamic_cast<ReturnStmt*>(prog.funcs[0]->body->stmts[0].get());
    EXPECT(ret != nullptr, "stmt is ReturnStmt");
    auto* lit = dynamic_cast<IntLitExpr*>(ret->value.get());
    EXPECT(lit != nullptr && lit->value == 0, "return value is 0");
}

static void test_var_decl() {
    auto prog = parse("int f() { int x = 42; return x; }");
    auto& stmts = prog.funcs[0]->body->stmts;
    EXPECT(stmts.size() == 2, "two stmts");
    auto* vd = dynamic_cast<VarDeclStmt*>(stmts[0].get());
    EXPECT(vd != nullptr,        "first stmt is VarDeclStmt");
    EXPECT(vd->name == "x",      "variable name is x");
    auto* lit = dynamic_cast<IntLitExpr*>(vd->init.get());
    EXPECT(lit != nullptr && lit->value == 42, "init is 42");
}

static void test_assignment() {
    auto prog = parse("int f() { int x = 0; x = 5; return x; }");
    auto& stmts = prog.funcs[0]->body->stmts;
    EXPECT(stmts.size() == 3, "three stmts");
    auto* as = dynamic_cast<AssignStmt*>(stmts[1].get());
    EXPECT(as != nullptr,     "second stmt is AssignStmt");
    EXPECT(as->name == "x",   "assign target is x");
    auto* lit = dynamic_cast<IntLitExpr*>(as->value.get());
    EXPECT(lit != nullptr && lit->value == 5, "assigned value is 5");
}

static void test_binary_expr_precedence() {
    // "1 + 2 * 3" should parse as "1 + (2 * 3)", i.e. the root is '+',
    // and the right child of '+' is the '*' node.
    auto prog = parse("int f() { return 1 + 2 * 3; }");
    auto* ret = dynamic_cast<ReturnStmt*>(prog.funcs[0]->body->stmts[0].get());
    auto* add = dynamic_cast<BinaryExpr*>(ret->value.get());
    EXPECT(add != nullptr && add->op == "+", "root is +");
    auto* mul = dynamic_cast<BinaryExpr*>(add->rhs.get());
    EXPECT(mul != nullptr && mul->op == "*", "rhs of + is *");
}

static void test_left_associativity() {
    // "1 - 2 - 3" → "(1-2)-3": root is '-', its LEFT child is also '-'.
    auto prog = parse("int f() { return 1 - 2 - 3; }");
    auto* ret = dynamic_cast<ReturnStmt*>(prog.funcs[0]->body->stmts[0].get());
    auto* sub1 = dynamic_cast<BinaryExpr*>(ret->value.get());
    EXPECT(sub1 != nullptr && sub1->op == "-", "root is -");
    auto* sub2 = dynamic_cast<BinaryExpr*>(sub1->lhs.get());
    EXPECT(sub2 != nullptr && sub2->op == "-", "lhs of root is also -");
    auto* lit3 = dynamic_cast<IntLitExpr*>(sub1->rhs.get());
    EXPECT(lit3 != nullptr && lit3->value == 3, "rhs is 3");
}

static void test_if_else() {
    auto prog = parse("int f() { if (1) { return 1; } else { return 0; } }");
    auto* ifs = dynamic_cast<IfStmt*>(prog.funcs[0]->body->stmts[0].get());
    EXPECT(ifs != nullptr,              "stmt is IfStmt");
    EXPECT(ifs->elseBranch != nullptr,  "has else branch");
}

static void test_while() {
    auto prog = parse("int f() { int i = 0; while (i < 10) { i = i + 1; } return i; }");
    auto& stmts = prog.funcs[0]->body->stmts;
    EXPECT(stmts.size() == 3, "3 stmts (decl, while, return)");
    auto* wh = dynamic_cast<WhileStmt*>(stmts[1].get());
    EXPECT(wh != nullptr, "second stmt is WhileStmt");
    auto* cond = dynamic_cast<BinaryExpr*>(wh->cond.get());
    EXPECT(cond != nullptr && cond->op == "<", "while cond is <");
}

static void test_function_with_params() {
    auto prog = parse("int add(int a, int b) { return a + b; }");
    auto& f = prog.funcs[0];
    EXPECT(f->params.size() == 2, "two params");
    EXPECT(f->params[0] == "a",   "first param is a");
    EXPECT(f->params[1] == "b",   "second param is b");
}

static void test_function_call() {
    auto prog = parse("int f() { return g(1, 2); }");
    auto* ret  = dynamic_cast<ReturnStmt*>(prog.funcs[0]->body->stmts[0].get());
    auto* call = dynamic_cast<CallExpr*>(ret->value.get());
    EXPECT(call != nullptr,          "expr is CallExpr");
    EXPECT(call->callee == "g",      "callee is g");
    EXPECT(call->args.size() == 2,   "two args");
}

static void test_print_stmt() {
    auto prog = parse("int f() { print(99); return 0; }");
    auto* pr = dynamic_cast<PrintStmt*>(prog.funcs[0]->body->stmts[0].get());
    EXPECT(pr != nullptr, "stmt is PrintStmt");
    auto* lit = dynamic_cast<IntLitExpr*>(pr->value.get());
    EXPECT(lit != nullptr && lit->value == 99, "print arg is 99");
}

static void test_unary_minus() {
    auto prog = parse("int f() { return -5; }");
    auto* ret = dynamic_cast<ReturnStmt*>(prog.funcs[0]->body->stmts[0].get());
    auto* un  = dynamic_cast<UnaryExpr*>(ret->value.get());
    EXPECT(un != nullptr && un->op == '-', "UnaryExpr with op=-");
    auto* lit = dynamic_cast<IntLitExpr*>(un->operand.get());
    EXPECT(lit != nullptr && lit->value == 5, "operand is 5");
}

static void test_multiple_functions() {
    auto prog = parse("int a() { return 1; } int b() { return 2; }");
    EXPECT(prog.funcs.size() == 2, "two functions");
    EXPECT(prog.funcs[0]->name == "a", "first is a");
    EXPECT(prog.funcs[1]->name == "b", "second is b");
}

static void test_parse_error_missing_semicolon() {
    bool threw = false;
    try { parse("int f() { return 0 }"); } catch (const std::runtime_error&) { threw = true; }
    EXPECT(threw, "error: missing semicolon");
}

static void test_parse_error_bad_expr() {
    bool threw = false;
    try { parse("int f() { return ; }"); } catch (const std::runtime_error&) { threw = true; }
    EXPECT(threw, "error: empty return expr");
}

// ── AST dump smoke test (just shouldn't crash) ────────────────────────────────
static void test_ast_dump() {
    auto prog = parse("int fib(int n) { if (n <= 1) { return n; } return fib(n-1) + fib(n-2); }");
    // Redirect stdout to /dev/null equivalent (just capture it)
    std::streambuf* old = std::cout.rdbuf();
    std::ostringstream devNull;
    std::cout.rdbuf(devNull.rdbuf());
    prog.dump();
    std::cout.rdbuf(old);
    EXPECT(true, "dump did not crash");
    // Verify it produced some output
    EXPECT(!devNull.str().empty(), "dump produced output");
}

int main() {
    test_empty_function();
    test_var_decl();
    test_assignment();
    test_binary_expr_precedence();
    test_left_associativity();
    test_if_else();
    test_while();
    test_function_with_params();
    test_function_call();
    test_print_stmt();
    test_unary_minus();
    test_multiple_functions();
    test_parse_error_missing_semicolon();
    test_parse_error_bad_expr();
    test_ast_dump();

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
