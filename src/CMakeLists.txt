# ── minicc executable ─────────────────────────────────────────────────────
# We link against a subset of LLVM libraries. The list grows as we add IR
# generation in later milestones.
#
# For now we only need Support (LLVMContext, raw_ostream, etc.).
# Later we will add: Core IRWriter Analysis TransformUtils InstCombine
llvm_map_components_to_libnames(LLVM_LIBS support)

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
    ${CMAKE_SOURCE_DIR}/include   # our own headers
    ${LLVM_INCLUDE_DIRS}          # LLVM public headers
)

target_link_libraries(minicc PRIVATE ${LLVM_LIBS})
