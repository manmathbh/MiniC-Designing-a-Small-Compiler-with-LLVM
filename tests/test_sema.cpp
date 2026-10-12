// tests/test_sema.cpp — Sema unit tests (no LLVM needed).
//
// Build:
//   clang++ -std=c++17 -I../include test_sema.cpp \
//       ../src/Token.cpp ../src/Lexer.cpp ../src/AST.cpp \
//       ../src/Parser.cpp ../src/Sema.cpp \
//       -o test_sema && ./test_sema

#include "minic/Lexer.h"
#include "minic/Parser.h"
#include "minic/Sema.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace minic;

static int passed = 0, failed = 0;

#define EXPECT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL [" << msg << "]: " #cond "\n";         \
            ++failed;                                                  \
        } else {                                                       \
            ++passed;                                                  \
        }                                                              \
    } while (false)

// Parse and sema-check a string. Returns true if it passes (no exception).
static bool ok(const std::string& src) {
    try {
        Lexer  lx(src);
        Parser p(std::move(lx));
        Program prog = p.parseProgram();
        Sema sema;
        sema.check(prog);
        return true;
    } catch (...) { return false; }
}

// Parse and sema-check a string. Returns the error message, or "" if no error.
static std::string errMsg(const std::string& src) {
    try {
        Lexer  lx(src);
        Parser p(std::move(lx));
        Program prog = p.parseProgram();
        Sema sema;
        sema.check(prog);
        return "";
    } catch (const std::runtime_error& e) { return e.what(); }
}

// ── VALID programs ────────────────────────────────────────────────────────────

static void test_valid_hello() {
    EXPECT(ok("int main() { print(42); return 0; }"), "valid: hello");
}

static void test_valid_var_use() {
    EXPECT(ok("int f() { int x = 1; int y = x + 1; return y; }"), "valid: var use");
}

static void test_valid_params() {
    EXPECT(ok("int add(int a, int b) { return a + b; }"), "valid: params");
}

static void test_valid_mutual_recursion() {
    // Pass 1 collects both signatures, so this is valid.
    EXPECT(ok(
        "int even(int n) { if (n == 0) { return 1; } return odd(n - 1); }"
        "int odd(int n)  { if (n == 0) { return 0; } return even(n - 1); }"
    ), "valid: mutual recursion");
}

static void test_valid_shadowing() {
    // Inner 'x' shadows outer 'x' — legal.
    EXPECT(ok(
        "int f() { int x = 1; if (x == 1) { int x = 2; print(x); } return x; }"
    ), "valid: inner shadowing");
}

static void test_valid_while_scope() {
    EXPECT(ok(
        "int f() { int i = 0; while (i < 10) { int tmp = i + 1; i = tmp; } return i; }"
    ), "valid: while scope");
}

static void test_valid_call_with_args() {
    EXPECT(ok(
        "int sq(int x) { return x * x; }"
        "int main() { print(sq(5)); return 0; }"
    ), "valid: call with args");
}

static void test_valid_zero_arg_call() {
    EXPECT(ok(
        "int answer() { return 42; }"
        "int main() { print(answer()); return 0; }"
    ), "valid: zero-arg call");
}

// ── INVALID programs — undeclared variable ────────────────────────────────────

static void test_error_undeclared_var() {
    auto msg = errMsg("int f() { return x; }");
    EXPECT(msg.find("undeclared") != std::string::npos, "error: undeclared var");
    EXPECT(msg.find("'x'") != std::string::npos,        "error: names the var");
}

static void test_error_use_before_declare() {
    // "int x = x" — x not yet visible when init is checked.
    auto msg = errMsg("int f() { int x = x + 1; return x; }");
    EXPECT(msg.find("undeclared") != std::string::npos, "error: use before declare");
}

static void test_error_out_of_scope() {
    // Variable declared inside if block is not visible after it.
    auto msg = errMsg(
        "int f() { if (1) { int x = 1; } return x; }"
    );
    EXPECT(msg.find("undeclared") != std::string::npos, "error: out of scope");
}

// ── INVALID programs — redeclaration ─────────────────────────────────────────

static void test_error_redeclare_same_scope() {
    auto msg = errMsg("int f() { int x = 1; int x = 2; return x; }");
    EXPECT(msg.find("redeclared") != std::string::npos, "error: redeclare same scope");
    EXPECT(msg.find("'x'") != std::string::npos,        "error: names the var");
}

static void test_error_redeclare_param() {
    // Two parameters with the same name — that IS an error (same scope).
    auto msg = errMsg("int f(int a, int a) { return a; }");
    EXPECT(msg.find("redeclared") != std::string::npos, "error: duplicate param name");

    // Shadowing a param with a local variable in the body block is VALID in C.
    // Our block pushes a new scope, so "int a = 1;" goes into the block scope, not
    // the function scope — this is correct shadowing, not redeclaration.
    EXPECT(ok("int f(int a) { int a = 1; return a; }"), "valid: local shadows param (different scope)");
}

static void test_error_redeclare_function() {
    auto msg = errMsg("int f() { return 0; } int f() { return 1; }");
    EXPECT(msg.find("redeclared") != std::string::npos, "error: redeclare function");
    EXPECT(msg.find("'f'") != std::string::npos,        "error: names function");
}

// ── INVALID programs — undeclared / arity errors on calls ────────────────────

static void test_error_undeclared_function() {
    auto msg = errMsg("int main() { return foo(); }");
    EXPECT(msg.find("undeclared function") != std::string::npos, "error: undeclared fn");
    EXPECT(msg.find("'foo'") != std::string::npos,               "error: names fn");
}

static void test_error_arity_too_few() {
    auto msg = errMsg("int add(int a, int b) { return a+b; } int main() { return add(1); }");
    EXPECT(msg.find("expects 2") != std::string::npos, "error: arity too few");
    EXPECT(msg.find("got 1") != std::string::npos,     "error: got count");
}

static void test_error_arity_too_many() {
    auto msg = errMsg("int f(int x) { return x; } int main() { return f(1,2,3); }");
    EXPECT(msg.find("expects 1") != std::string::npos, "error: arity too many");
    EXPECT(msg.find("got 3") != std::string::npos,     "error: got count");
}

// ── Scope boundary tests ──────────────────────────────────────────────────────

static void test_scope_if_branches_independent() {
    // Variable in 'then' branch is NOT visible in 'else' branch.
    auto msg = errMsg(
        "int f() { if (1) { int x = 1; } else { return x; } return 0; }"
    );
    EXPECT(msg.find("undeclared") != std::string::npos, "error: then var not in else");
}

static void test_scope_while_body_isolated() {
    // Variable declared in while body is not visible after the loop.
    auto msg = errMsg(
        "int f() { while (1) { int t = 5; } return t; }"
    );
    EXPECT(msg.find("undeclared") != std::string::npos, "error: while var after loop");
}

// ── Full program that should pass sema ───────────────────────────────────────
static void test_fibonacci_passes() {
    EXPECT(ok(
        "int fib(int n) {"
        "  if (n <= 1) { return n; }"
        "  return fib(n - 1) + fib(n - 2);"
        "}"
        "int main() { print(fib(10)); return 0; }"
    ), "valid: fibonacci");
}

int main() {
    // Valid programs
    test_valid_hello();
    test_valid_var_use();
    test_valid_params();
    test_valid_mutual_recursion();
    test_valid_shadowing();
    test_valid_while_scope();
    test_valid_call_with_args();
    test_valid_zero_arg_call();

    // Error: undeclared variable
    test_error_undeclared_var();
    test_error_use_before_declare();
    test_error_out_of_scope();

    // Error: redeclaration
    test_error_redeclare_same_scope();
    test_error_redeclare_param();
    test_error_redeclare_function();

    // Error: function call problems
    test_error_undeclared_function();
    test_error_arity_too_few();
    test_error_arity_too_many();

    // Scope boundary
    test_scope_if_branches_independent();
    test_scope_while_body_isolated();

    // Full program
    test_fibonacci_passes();

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed.\n";
    return failed > 0 ? 1 : 0;
}
