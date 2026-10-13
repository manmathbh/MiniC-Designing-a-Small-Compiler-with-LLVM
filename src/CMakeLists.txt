# ── minicc executable ─────────────────────────────────────────────────────
# LLVM components we use:
#   support → raw_ostream, InitLLVM, error handling
#   core    → Module, Function, IRBuilder, Type, Constants, Verifier
#
# We do NOT link instcombine/transformutils because we run those via the
# external `opt` tool in the driver. Keeping link deps minimal means faster
# incremental builds and makes the "codegen only" boundary clear.
llvm_map_components_to_libnames(LLVM_LIBS support core)

add_executable(minicc
    main.cpp
    Token.cpp
    AST.cpp
    Lexer.cpp
    Parser.cpp
    Sema.cpp
    Codegen.cpp
)

target_include_directories(minicc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

target_link_libraries(minicc PRIVATE ${LLVM_LIBS})

# Copy runtime.c to build dir so the driver can find it
configure_file(
    ${CMAKE_SOURCE_DIR}/runtime/minic_runtime.c
    ${CMAKE_BINARY_DIR}/minic_runtime.c
    COPYONLY
)
