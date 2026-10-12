#include "minic/Lexer.h"
#include "minic/Parser.h"
#include "minic/Sema.h"
#include "minic/Codegen.h"

#include "llvm/Support/raw_ostream.h"   // llvm::errs(), llvm::outs()
#include "llvm/Support/InitLLVM.h"      // InitLLVM: sets up signal handlers etc.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — Driver: ties all stages together.
//
// Usage:
//   minicc <input.mc> [-o <output>] [--dump-ast] [--emit-llvm]
//
// DESIGN: The driver is intentionally thin — it reads the file, creates each
// pipeline stage in order, and hands off results. Each stage is independent
// and can be tested in isolation. This matches the "compiler as pipeline of
// transformations" mental model.
//
// Flags added incrementally per milestone:
//   --dump-ast   : pretty-print AST then exit     (milestone 3)
//   --emit-llvm  : write .ll file and stop before linking (milestone 5)
// ─────────────────────────────────────────────────────────────────────────────

static void printUsage(const char* argv0) {
    llvm::errs() << "MiniC Compiler v0.1\n"
                 << "Usage: " << argv0
                 << " <input.mc> [-o <output>] [--dump-tokens] [--dump-ast] [--emit-llvm]\n";
}

// Read entire file into a string; returns false on error.
static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) { return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int main(int argc, char** argv) {
    // InitLLVM installs pretty stack traces and signal handlers — good practice
    // even in a toy compiler because LLVM itself can crash with useful traces.
    llvm::InitLLVM X(argc, argv);

    // ── Argument parsing ──────────────────────────────────────────────────
    std::string inputFile;
    std::string outputFile = "out";
    bool dumpTokens = false;
    bool dumpAst   = false;
    bool emitLlvm  = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--dump-tokens") {
            dumpTokens = true;
        } else if (arg == "--dump-ast") {
            dumpAst = true;
        } else if (arg == "--emit-llvm") {
            emitLlvm = true;
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg[0] != '-') {
            inputFile = arg;
        } else {
            llvm::errs() << "Unknown flag: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputFile.empty()) {
        llvm::errs() << "Error: no input file.\n";
        printUsage(argv[0]);
        return 1;
    }

    // ── Read source ───────────────────────────────────────────────────────
    std::string src;
    if (!readFile(inputFile, src)) {
        llvm::errs() << "Error: cannot open '" << inputFile << "'\n";
        return 1;
    }

    try {
        // ── Stage 0 (optional): dump all tokens then exit ───────────
        if (dumpTokens) {
            minic::Lexer dbgLex(src);
            while (true) {
                minic::Token t = dbgLex.nextToken();
                llvm::outs() << "[line " << t.line << "] "
                             << minic::tokenKindName(t.kind);
                if (t.kind == minic::TokenKind::INT_LIT)
                    llvm::outs() << " = " << t.intVal();
                else if (t.kind == minic::TokenKind::IDENT)
                    llvm::outs() << " = " << t.name();
                llvm::outs() << "\n";
                if (t.kind == minic::TokenKind::END_OF_FILE) break;
            }
            return 0;
        }

        // ── Stage 1: Lex + Parse ─────────────────────────────────────────
        minic::Lexer  lexer(src);
        minic::Parser parser(std::move(lexer));
        minic::Program prog = parser.parseProgram();

        // ── Stage 2: Semantic analysis ───────────────────────────────────
        minic::Sema sema;
        sema.check(prog);

        if (dumpAst) {
            prog.dump();
            return 0;
        }

        // ── Stage 3: IR generation (milestone 5+) ────────────────────────
        minic::Codegen cg;
        std::string ir = cg.generate(prog);

        if (emitLlvm || ir.empty()) {
            // In early milestones codegen is a stub; just print what we have.
            llvm::outs() << ir;
            return 0;
        }

        // ── Stage 4: Link to executable (milestone 6+) ───────────────────
        // Will invoke clang on the emitted .ll file.
        llvm::errs() << "[TODO] Linking not yet implemented.\n";

    } catch (const std::exception& e) {
        llvm::errs() << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
