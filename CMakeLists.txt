cmake_minimum_required(VERSION 3.20)
project(MiniC VERSION 0.1 LANGUAGES CXX)

# ── C++17 ──────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Export compile_commands.json (useful for clangd / IDEs) ───────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find LLVM ─────────────────────────────────────────────────────────────
# Pass -DLLVM_DIR=$(llvm-config --cmakedir) at configure time if LLVM is
# not on the default CMake search path.
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake from: ${LLVM_DIR}")

# Make LLVM's CMake helpers available (llvm_map_components_to_libnames, etc.)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# Propagate LLVM's compile definitions (e.g. _GNU_SOURCE, NDEBUG in release)
add_definitions(${LLVM_DEFINITIONS})

# ── Compiler warnings ──────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Sub-projects ───────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(passes)
