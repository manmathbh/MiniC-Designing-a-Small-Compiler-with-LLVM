# MiniC Compiler

> A small, hand-written C-like compiler in **C++17** that produces native executables via **LLVM IR**.
> Built to learn — and explain — every stage of a real compiler pipeline.

---

## Table of Contents

1. [What is MiniC?](#what-is-minic)
2. [Language Features](#language-features)
3. [Architecture](#architecture)
4. [Prerequisites](#prerequisites)
5. [Building](#building)
6. [Quick Start](#quick-start)
7. [Compiler Flags](#compiler-flags)
8. [Writing MiniC Programs](#writing-minic-programs)
9. [Running Tests](#running-tests)
10. [The Custom LLVM Pass](#the-custom-llvm-pass)
11. [IR Before and After Optimization](#ir-before-and-after-optimization)
12. [Project Structure](#project-structure)
13. [Design Decisions](#design-decisions)
14. [Known Limitations](#known-limitations)
15. [Contributing](#contributing)

---

## What is MiniC?

MiniC is a **toy compiler** built as a learning project. It compiles a tiny C-like language into native executables using LLVM as a backend. Every stage is hand-written and intentionally simple so you can read, understand, and modify the whole thing.

**What this project teaches:**
- How a lexer tokenises source text character-by-character
- How a recursive-descent parser builds an AST from a grammar
- How semantic analysis catches errors before code generation
- How LLVM IR works — basic blocks, SSA form, and phi nodes
- Why the `alloca + mem2reg` strategy is used by real compilers like Clang
- How to write and load an out-of-tree LLVM pass plugin
- What strength reduction is and why shifts are cheaper than multiplies on modern CPUs

**This is not:**
- A production compiler (no C standard compliance)
- A complete language (no pointers, arrays, floats, or structs)
- An example of how to write fast compiler code (clarity is preferred over performance)

---

## Language Features

MiniC supports a small but complete subset:

| Feature | Example |
|---|---|
| Integer variables | `int x = 42;` |
| Arithmetic | `x + y * 2 - z % 3` |
| Comparisons | `x == y`, `a < b`, `p >= q` |
| Logical operators | `a && b`, `x \|\| y`, `!flag` |
| Assignment | `x = x + 1;` |
| If / else | `if (x > 0) { ... } else { ... }` |
| While loops | `while (i < 10) { i = i + 1; }` |
| Functions | `int add(int a, int b) { return a + b; }` |
| Recursion | `return fib(n-1) + fib(n-2);` |
| Print | `print(result);` |
| Line comments | `// this is a comment` |

**Not supported** (flag these in interviews): pointers, arrays, structs, `for` loops, `break`/`continue`, multiple files, `bool` type keyword, floating-point.

---

## Architecture

```
Source file (input.mc)
        │
        ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 1 — LEXER  (src/Lexer.cpp)                                │
│  Character-at-a-time scanner → stream of Tokens                  │
│  Hand-written, no lex/flex. Keyword detection via hash map.       │
└────────────────────────────┬─────────────────────────────────────┘
                             │ Token stream (pull model)
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 2 — PARSER  (src/Parser.cpp)                              │
│  Recursive-descent, one method per grammar rule                   │
│  Produces an AST owned by unique_ptr                             │
└────────────────────────────┬─────────────────────────────────────┘
                             │ AST
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 3 — SEMANTIC ANALYSIS  (src/Sema.cpp)                     │
│  Two-pass: collect function signatures, then check bodies         │
│  Scope stack for variable resolution and redeclaration checks     │
└────────────────────────────┬─────────────────────────────────────┘
                             │ Validated AST
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 4 — LLVM IR CODEGEN  (src/Codegen.cpp)                    │
│  alloca + mem2reg strategy (same as Clang's CodeGen)              │
│  if/else → diamond CFG,  while → natural loop CFG                │
│  Emits: input.ll  (raw, before optimization)                     │
└────────────────────────────┬─────────────────────────────────────┘
                             │ LLVM IR text (.ll)
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 5 — OPT  (external LLVM tool)                             │
│  mem2reg     → promotes alloca/load/store to SSA φ-nodes          │
│  instcombine → peephole simplifications (x+0→x, !!x→x, ...)      │
│  Emits: input.opt.ll                                              │
└────────────────────────────┬─────────────────────────────────────┘
                             │ Optimised IR
                             ▼
┌──────────────────────────────────────────────────────────────────┐
│  STAGE 6 — CLANG  (linker/assembler)                             │
│  Links input.opt.ll + minic_runtime.c → native binary            │
└────────────────────────────┬─────────────────────────────────────┘
                             │
                        ./a.out  ✓
```

**Out-of-tree pass plugin** (optional, separate from the main pipeline):
```
StrengthReduction.so
  │  mul x, 2^n  →  shl x, n
  │  udiv x, 2^n →  lshr x, n
  └─ loaded by: opt -load-pass-plugin=./StrengthReduction.so
                    -passes="strength-reduction"
```

---

## Prerequisites

| Tool | Minimum Version | Purpose |
|---|---|---|
| LLVM | 17 or 18 | IR libraries, `opt`, `clang` |
| CMake | 3.20+ | Build system |
| C++ compiler | GCC 9+ or Clang 10+ | Build MiniC itself |
| clang | any LLVM 17/18 build | Link generated IR to binary |

**Install on Ubuntu / Debian:**
```bash
# LLVM 17
sudo apt install llvm-17-dev clang-17 cmake build-essential

# or LLVM 18
sudo apt install llvm-18-dev clang-18 cmake build-essential
```

**Install on Arch Linux:**
```bash
sudo pacman -S llvm clang cmake base-devel
```

**Install on macOS (Homebrew):**
```bash
brew install llvm cmake
export PATH="$(brew --prefix llvm)/bin:$PATH"
```

---

## Building

```bash
# 1. Clone the repository
git clone <repo-url> minic
cd minic

# 2. Create a build directory
mkdir build && cd build

# 3. Configure — point CMake at your LLVM installation
cmake .. -DLLVM_DIR=$(llvm-config-17 --cmakedir)

# For LLVM 18:
# cmake .. -DLLVM_DIR=$(llvm-config-18 --cmakedir)

# For a custom LLVM path (e.g. /opt/llvm):
# cmake .. -DLLVM_DIR=/opt/llvm/lib/cmake/llvm

# 4. Build
make -j$(nproc)

# 5. Confirm it works
./minicc --help
```

Expected output:
```
MiniC Compiler v0.1
Usage: ./minicc <input.mc> [-o <out>] [--dump-tokens] [--dump-ast] [--emit-llvm] [--no-opt]
```

---

## Quick Start

**Write a program** (`hello.mc`):
```c
int main() {
    print(42);
    return 0;
}
```

**Compile and run:**
```bash
cd build
./minicc ../tests/hello.mc -o hello
./hello
# 42
```

**Fibonacci:**
```bash
./minicc ../tests/fibonacci.mc -o fib
./fib
# 55
# 8
```

---

## Compiler Flags

| Flag | Description |
|---|---|
| `-o <file>` | Output binary name (default: `a.out`) |
| `--dump-tokens` | Print all tokens to stdout, then exit |
| `--dump-ast` | Print the AST tree to stdout, then exit |
| `--emit-llvm` | Write `<input>.ll` (raw IR before opt), then exit |
| `--no-opt` | Skip the `opt` step — link raw unoptimised IR |
| `--help` / `-h` | Print usage |

**Examples:**

```bash
# See what the lexer produces
./minicc ../tests/arith.mc --dump-tokens

# Inspect the AST
./minicc ../tests/fibonacci.mc --dump-ast

# Emit raw IR (before mem2reg), inspect it
./minicc ../tests/control.mc --emit-llvm
cat ../tests/control.ll

# Compare raw vs. optimised IR
./minicc ../tests/control.mc --emit-llvm        # → control.ll
./minicc ../tests/control.mc -o ctrl            # → control.opt.ll (written as side-effect)
diff ../tests/control.ll ../tests/control.opt.ll
```

---

## Writing MiniC Programs

### Grammar at a glance

```
// Declare functions — all return int, all params are int
int function_name(int param1, int param2) {
    // Declare local variables (must initialise)
    int x = 10;

    // Assign
    x = x + 1;

    // If / else
    if (x > 5) {
        x = x * 2;
    } else {
        x = 0;
    }

    // While loop
    while (x < 100) {
        x = x + x;
    }

    // Print an integer
    print(x);

    // Return
    return x;
}

// Entry point must be named main
int main() {
    print(function_name(3, 4));
    return 0;
}
```

### Operator precedence (high → low)

| Level | Operators |
|---|---|
| Unary | `-x`, `!x` |
| Multiply | `*`, `/`, `%` |
| Add | `+`, `-` |
| Compare | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Logical AND | `&&` |
| Logical OR | `\|\|` |

### Example programs

**Fibonacci (recursive):**
```c
int fib(int n) {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    print(fib(10));   // 55
    return 0;
}
```

**Sum 1..n:**
```c
int sum(int n) {
    int total = 0;
    int i = 1;
    while (i <= n) {
        total = total + i;
        i = i + 1;
    }
    return total;
}

int main() {
    print(sum(10));   // 55
    return 0;
}
```

**GCD (Euclidean algorithm):**
```c
int gcd(int a, int b) {
    while (b != 0) {
        int tmp = b;
        b = a % b;
        a = tmp;
    }
    return a;
}

int main() {
    print(gcd(48, 18));   // 6
    return 0;
}
```

---

## Running Tests

**Unit tests** (no LLVM needed — just a C++17 compiler):
```bash
cd tests

# Build and run lexer tests
clang++ -std=c++17 -I../include test_lexer.cpp \
    ../src/Token.cpp ../src/Lexer.cpp \
    -o test_lexer && ./test_lexer

# Build and run parser tests
clang++ -std=c++17 -I../include test_parser.cpp \
    ../src/Token.cpp ../src/Lexer.cpp \
    ../src/AST.cpp ../src/Parser.cpp \
    -o test_parser && ./test_parser

# Build and run sema tests
clang++ -std=c++17 -I../include test_sema.cpp \
    ../src/Token.cpp ../src/Lexer.cpp \
    ../src/AST.cpp ../src/Parser.cpp ../src/Sema.cpp \
    -o test_sema && ./test_sema
```

**End-to-end tests** (requires a built `minicc`):
```bash
cd build
../tests/run_tests.sh          # run all .mc programs
../tests/run_tests.sh hello    # run only hello.mc
```

Expected:
```
PASS  hello
PASS  arith
PASS  control
PASS  fibonacci

Results: 4 passed, 0 failed.
```

---

## The Custom LLVM Pass

The `passes/` directory contains an **out-of-tree LLVM pass plugin** that implements strength reduction:

| Transformation | Example |
|---|---|
| Multiply by power of 2 | `x * 4` → `x << 2` |
| Multiply (commutative) | `8 * x` → `x << 3` |
| Unsigned divide by power of 2 | `udiv x, 8` → `lshr x, 3` |

**Why shifts?** On Arm Neoverse N2 (the processor in NVIDIA's Grace CPU):
- `MUL`: latency **3 cycles**, throughput 0.5/cycle
- `LSL` (shift): latency **1 cycle**, throughput 2/cycle

This is a 3× latency improvement for compile-time-constant power-of-two multipliers — exactly the kind of transformation a CPU compiler team cares about.

**Build the plugin** (happens automatically with `make`):
```bash
ls build/StrengthReduction.so    # the plugin shared library
```

**Run the pass manually:**
```bash
cd build
opt -load-pass-plugin=./StrengthReduction.so \
    -passes="strength-reduction" \
    -S ../tests/ir/strength_input.ll \
    -o strength_output.ll
cat strength_output.ll
```

**Run the diff-based pass test:**
```bash
cd build
../tests/run_pass_test.sh
# PASS: StrengthReduction output matches expected IR.
```

**Why not signed division (sdiv)?**
```
 (-7) / 4 = 0     (C signed division rounds toward zero)
 (-7) >> 2 = -2   (arithmetic right shift rounds toward -∞)
```
Converting `sdiv` to `ashr` is only safe when the dividend is provably non-negative (requires range analysis). We conservatively skip it — this is the correct production behaviour.

---

## IR Before and After Optimization

### Before `mem2reg` (raw alloca form)

Every variable is a stack slot. `--emit-llvm` shows this:

```llvm
define i64 @add(i64 %a, i64 %b) {
entry:
  %a1 = alloca i64          ; stack slot for param a
  %b2 = alloca i64          ; stack slot for param b
  store i64 %a, ptr %a1     ; write param to stack
  store i64 %b, ptr %b2
  %a3 = load i64, ptr %a1   ; read a from stack
  %b4 = load i64, ptr %b2   ; read b from stack
  %add = add i64 %a3, %b4
  ret i64 %add
}
```

### After `mem2reg` (SSA form)

`mem2reg` (PromoteMemToReg) recognises single-assignment allocas and removes them:

```llvm
define i64 @add(i64 %a, i64 %b) {
entry:
  %add = add i64 %a, %b     ; alloca/load/store eliminated
  ret i64 %add
}
```

### phi nodes (control flow join)

For a variable assigned in both branches of an if/else, `mem2reg` inserts a **phi node** at the merge point. A phi node says "the value depends on which predecessor block we came from":

```c
// Source: if (c) { x = 1; } else { x = 2; }  print(x);
```
```llvm
if.then:
  br label %if.merge

if.else:
  br label %if.merge

if.merge:
  %x = phi i64 [ 1, %if.then ], [ 2, %if.else ]  ; ← phi node
  call void @minic_print(i64 %x)
```

### After `StrengthReduction` pass

```llvm
; Before: double_it compiles x * 2 → mul
%mul = mul i64 %x, 2

; After strength-reduction pass:
%mul.shl = shl i64 %x, 1
```

---

## Project Structure

```
minic/
│
├── CMakeLists.txt              Root CMake — finds LLVM, sets C++17
├── README.md                   This file
│
├── include/minic/              Public headers (no LLVM includes)
│   ├── Token.h                 TokenKind enum + Token struct (std::variant payload)
│   ├── Lexer.h                 Pull-model lexer (nextToken / peek)
│   ├── AST.h                   Expr and Stmt node hierarchy (unique_ptr ownership)
│   ├── Parser.h                Recursive-descent parser interface
│   ├── Sema.h                  Scope stack + semantic checker interface
│   └── Codegen.h               IR generator interface (LLVM headers in .cpp only)
│
├── src/
│   ├── CMakeLists.txt          Links LLVM support + core
│   ├── main.cpp                Driver: arg parsing, pipeline, subprocess calls
│   ├── Token.cpp               tokenKindName() lookup table
│   ├── Lexer.cpp               Hand-written scanner (213 lines)
│   ├── AST.cpp                 dump() pretty-printer for --dump-ast
│   ├── Parser.cpp              Full recursive-descent parser
│   ├── Sema.cpp                Two-pass semantic checker
│   └── Codegen.cpp             LLVM IR generation (alloca + mem2reg strategy)
│
├── passes/
│   ├── CMakeLists.txt          MODULE library, -fno-rtti, no "lib" prefix
│   └── StrengthReduction.cpp   Out-of-tree LLVM pass (mul/udiv → shl/lshr)
│
├── runtime/
│   └── minic_runtime.c         minic_print(long long) → printf wrapper
│
└── tests/
    ├── run_tests.sh            End-to-end diff test runner
    ├── run_pass_test.sh        IR pass diff test
    ├── test_lexer.cpp          57 lexer unit tests (no LLVM needed)
    ├── test_parser.cpp         41 parser unit tests (no LLVM needed)
    ├── test_sema.cpp           27 sema unit tests (no LLVM needed)
    ├── hello.mc                Minimal smoke test
    ├── arith.mc                Arithmetic precedence test
    ├── control.mc              if/else + while test
    ├── fibonacci.mc            Recursive function test
    ├── expected/               Expected stdout for each .mc
    └── ir/
        ├── strength_input.ll   Pass test input IR
        └── strength_expected.ll Pass test expected output IR
```

---

## Design Decisions

| Decision | Rationale |
|---|---|
| **Hand-written lexer** | Learn the internals; lex/flex hides what matters |
| **Recursive descent** | Grammar rules map 1-to-1 to methods; easy to debug |
| **Structural precedence** | One method per level; more verbose than Pratt but maximally readable |
| **`unique_ptr` for AST** | Clear ownership (parent owns child), no manual delete, no leaks |
| **`std::variant` in Token** | Type-safe value payload without `union` footguns |
| **alloca + mem2reg** | Lets us generate valid IR immediately without computing dominance frontiers; same approach as Clang |
| **Entry-block allocas** | `mem2reg` only promotes allocas in the entry block; putting them anywhere else silently skips promotion |
| **MODULE not SHARED for pass** | A MODULE can only be dlopen'd (not link-time imported), which is the correct model for LLVM plugins |
| **`-fno-rtti` on pass** | LLVM is built without RTTI; mixing RTTI boundaries across shared-library boundaries causes linker errors |
| **External `opt` + `clang`** | Keeps `minicc`'s link deps minimal; lets you see intermediate IR files; mirrors the real LLVM toolchain |

---

## Known Limitations

These are intentional simplifications, not bugs. Mention them proactively in interviews:

- **Single type** — only `int` (mapped to LLVM `i64`). No `float`, `char`, `bool`, pointers, arrays, or structs.
- **No short-circuit evaluation** — `&&` and `||` always evaluate both sides. Short-circuit requires branching into new basic blocks (same structure as `if/else`).
- **No `for` loop** — implement as `while` manually.
- **No `break` / `continue`** — would require tracking the nearest loop's exit block.
- **Panic on first error** — one error, stop. No error recovery. Production compilers continue and accumulate multiple diagnostics.
- **No source locations on AST** — only line numbers; no column. Rich diagnostics need `SourceRange` on every node.
- **Signed division only** — `/` compiles to `sdiv`. The strength reduction pass only handles `udiv` because converting `sdiv` to `ashr` is unsound for negative values.
- **Single-file programs** — no `#include`, no multi-file compilation, no header files.
- **`system()` for subprocesses** — a production driver uses `llvm::sys::ExecuteAndWait` for proper error handling and cross-platform argv.

---

## Contributing

Contributions are welcome. Suggested extensions (in order of difficulty):

1. **`for` loop** — desugar to `while` in the parser (no AST change needed)
2. **`break` / `continue`** — track the loop's `afterBB` on a stack in Codegen
3. **Short-circuit `&&` / `||`** — emit as if/else in `emitBinary`
4. **Multiple return types (`void`)** — add `VoidType` to the type system
5. **`bool` keyword** — sugar for `int` with range [0,1]
6. **Array type** — `int arr[10]`, GEP in codegen
7. **Dead code elimination pass** — a simpler out-of-tree pass to pair with strength reduction

**Adding a test:**
1. Write `tests/<name>.mc`
2. Add expected output to `tests/expected/<name>.txt`
3. `run_tests.sh` picks it up automatically

**Adding an IR pass test:**
1. Write input IR to `tests/ir/<name>_input.ll`
2. Write expected output to `tests/ir/<name>_expected.ll`
3. Add a block to `tests/run_pass_test.sh`

---

## License

MIT — see [LICENSE](LICENSE) if present. Free to use for learning, interviews, and coursework.
