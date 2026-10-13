#include "minic/Lexer.h"
#include "minic/Parser.h"
#include "minic/Sema.h"
#include "minic/Codegen.h"

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InitLLVM.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
// main.cpp — MiniC compiler driver.
//
// PIPELINE:
//   input.mc
//     │  Lexer + Parser       → AST
//     │  Sema                 → validated AST
//     │  Codegen              → LLVM IR string (.ll)
//     │  opt (subprocess)     → optimised .ll  (mem2reg + instcombine)
//     │  clang (subprocess)   → native executable (links runtime)
//     └─ output binary
//
// FLAGS:
//   --dump-tokens  Print all tokens, exit
//   --dump-ast     Print AST, exit
//   --emit-llvm    Write <output>.ll (raw, before opt), exit
//   --no-opt       Skip the opt step (useful to compare IR before/after)
//   -o <file>      Output file name (default: a.out)
//
// SUBPROCESS DESIGN:
//   We shell out to `opt` and `clang` rather than embedding the pass manager.
//   This is intentional: it lets you inspect intermediate .ll files, mirrors
//   the real LLVM toolchain workflow, and keeps minicc's link-time dependencies
//   small (just core+support, not the full optimizer).
//
// SIMPLIFICATION 🏷️: std::system() is used for subprocess invocation. A
//   production driver (like clang itself) uses llvm::sys::ExecuteAndWait for
//   proper error capture, cross-platform argv handling, and no shell injection.
// ─────────────────────────────────────────────────────────────────────────────

namespace fs = std::filesystem;

static void usage(const char* argv0) {
    llvm::errs()
        << "MiniC Compiler v0.1\n"
        << "Usage: " << argv0
        << " <input.mc> [-o <out>] [--dump-tokens] [--dump-ast]"
           " [--emit-llvm] [--no-opt]\n";
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return f.good();
}

// Replace file extension: "foo/bar.mc" → "foo/bar.ll"
static std::string replaceExt(const std::string& path, const std::string& ext) {
    auto p = fs::path(path);
    p.replace_extension(ext);
    return p.string();
}

int main(int argc, char** argv) {
    llvm::InitLLVM X(argc, argv);

    // ── Parse arguments ───────────────────────────────────────────────────
    std::string inputFile;
    std::string outputFile = "a.out";
    bool dumpTokens = false;
    bool dumpAst    = false;
    bool emitLlvm   = false;
    bool noOpt      = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if      (arg == "--help" || arg == "-h")  { usage(argv[0]); return 0; }
        else if (arg == "--dump-tokens")           { dumpTokens = true; }
        else if (arg == "--dump-ast")              { dumpAst    = true; }
        else if (arg == "--emit-llvm")             { emitLlvm   = true; }
        else if (arg == "--no-opt")                { noOpt      = true; }
        else if (arg == "-o" && i + 1 < argc)      { outputFile = argv[++i]; }
        else if (arg[0] != '-')                    { inputFile  = arg; }
        else { llvm::errs() << "Unknown flag: " << arg << "\n"; usage(argv[0]); return 1; }
    }

    if (inputFile.empty()) {
        llvm::errs() << "Error: no input file.\n";
        usage(argv[0]);
        return 1;
    }

    // ── Read source ───────────────────────────────────────────────────────
    std::string src;
    if (!readFile(inputFile, src)) {
        llvm::errs() << "Error: cannot open '" << inputFile << "'\n";
        return 1;
    }

    try {
        // ── Stage 0: dump tokens ──────────────────────────────────────────
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

        // ── Stage 1: Lex + Parse → AST ───────────────────────────────────
        minic::Lexer  lexer(src);
        minic::Parser parser(std::move(lexer));
        minic::Program prog = parser.parseProgram();

        // ── Stage 2: Semantic analysis ────────────────────────────────────
        minic::Sema sema;
        sema.check(prog);

        if (dumpAst) {
            prog.dump();
            return 0;
        }

        // ── Stage 3: IR codegen ───────────────────────────────────────────
        minic::Codegen cg;
        std::string ir = cg.generate(prog);

        // Determine .ll output path: beside the input file.
        std::string llPath = replaceExt(inputFile, ".ll");

        if (!writeFile(llPath, ir)) {
            llvm::errs() << "Error: cannot write '" << llPath << "'\n";
            return 1;
        }
        llvm::errs() << "Wrote IR → " << llPath << "\n";

        if (emitLlvm) return 0;

        // ── Stage 4: Optimise with opt (mem2reg + instcombine) ────────────
        std::string optLlPath = replaceExt(inputFile, ".opt.ll");

        if (!noOpt) {
            // mem2reg: promotes alloca/load/store to SSA registers + phi nodes.
            // instcombine: peephole simplifications (e.g. x+0 → x, x*1 → x).
            std::string optCmd =
                "opt -S -passes=\"mem2reg,instcombine\" "
                + llPath + " -o " + optLlPath;
            llvm::errs() << "Running: " << optCmd << "\n";
            if (std::system(optCmd.c_str()) != 0) {
                llvm::errs() << "Error: opt failed. Is LLVM installed?\n";
                return 1;
            }
            llvm::errs() << "Wrote optimised IR → " << optLlPath << "\n";
        } else {
            optLlPath = llPath; // skip opt, link the raw IR
        }

        // ── Stage 5: Link to native executable via clang ──────────────────
        // clang accepts .ll files directly and handles assembly + linking.
        // We also link minic_runtime.c (defines minic_print).
        //
        // Find runtime.c relative to the minicc binary location.
        // When built with CMake, it is copied to the build directory.
        std::string runtimePath;
        {
            // Try same directory as the running binary first.
            fs::path self(argv[0]);
            fs::path candidate = self.parent_path() / "minic_runtime.c";
            if (fs::exists(candidate)) {
                runtimePath = candidate.string();
            } else {
                // Fallback: same directory as input file.
                fs::path altCandidate =
                    fs::path(inputFile).parent_path() / "minic_runtime.c";
                runtimePath = altCandidate.string();
            }
        }

        std::string clangCmd =
            "clang " + optLlPath + " " + runtimePath +
            " -o " + outputFile;
        llvm::errs() << "Running: " << clangCmd << "\n";
        if (std::system(clangCmd.c_str()) != 0) {
            llvm::errs() << "Error: clang linking failed.\n";
            return 1;
        }
        llvm::errs() << "Compiled → " << outputFile << "\n";

    } catch (const std::exception& e) {
        llvm::errs() << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
