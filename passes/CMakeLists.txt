# ── StrengthReduction pass — out-of-tree LLVM plugin ─────────────────────
#
# KEY DESIGN CHOICE: We build this as a SHARED LIBRARY (MODULE), not linked
# into minicc. This mirrors how real out-of-tree LLVM passes work: you load
# them at runtime with `opt -load-pass-plugin=./StrengthReduction.so`.
# It forces a clean separation: the pass knows nothing about our compiler;
# it only sees LLVM IR. This is exactly the model used by production passes.
#
# LLVM_LINK_LLVM_DYLIB: if the LLVM install uses a single libLLVM.so we
# must NOT link our plugin against it again (double-link → ODR violations).
# The `MODULE` type + no explicit LLVM libs handles this correctly.

add_library(StrengthReduction MODULE
    StrengthReduction.cpp
)

target_include_directories(StrengthReduction PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

# Passes must be compiled with RTTI and exceptions DISABLED to match LLVM.
# Mixing RTTI settings across shared-library boundaries causes linker errors.
target_compile_options(StrengthReduction PRIVATE
    -fno-rtti
    -fno-exceptions
)

# Do not add a "lib" prefix; opt expects "StrengthReduction.so" not "libStrengthReduction.so"
set_target_properties(StrengthReduction PROPERTIES PREFIX "")
