#include "minic/AST.h"
#include <iostream>

// AST dump — minimal pretty-printer, used in --dump-ast mode.
// Each node is printed at 2*indent spaces of indentation.
namespace minic {

static void ind(int n) { for (int i = 0; i < n * 2; i++) std::cout << ' '; }

void IntLitExpr ::dump(int i) const { ind(i); std::cout << "IntLit(" << value << ")\n"; }
void VarExpr    ::dump(int i) const { ind(i); std::cout << "Var(" << name << ")\n"; }
void UnaryExpr  ::dump(int i) const { ind(i); std::cout << "Unary(" << op << ")\n"; operand->dump(i+1); }
void BinaryExpr ::dump(int i) const { ind(i); std::cout << "Binary(" << op << ")\n"; lhs->dump(i+1); rhs->dump(i+1); }
void CallExpr   ::dump(int i) const {
    ind(i); std::cout << "Call(" << callee << ")\n";
    for (auto& a : args) a->dump(i+1);
}

void VarDeclStmt::dump(int i) const { ind(i); std::cout << "VarDecl(" << name << ")\n"; init->dump(i+1); }
void AssignStmt ::dump(int i) const { ind(i); std::cout << "Assign(" << name << ")\n"; value->dump(i+1); }
void ReturnStmt ::dump(int i) const { ind(i); std::cout << "Return\n"; value->dump(i+1); }
void PrintStmt  ::dump(int i) const { ind(i); std::cout << "Print\n"; value->dump(i+1); }
void ExprStmt   ::dump(int i) const { expr->dump(i); }
void BlockStmt  ::dump(int i) const { ind(i); std::cout << "Block\n"; for (auto& s : stmts) s->dump(i+1); }
void IfStmt     ::dump(int i) const {
    ind(i); std::cout << "If\n";
    cond->dump(i+1); thenBranch->dump(i+1);
    if (elseBranch) elseBranch->dump(i+1);
}
void WhileStmt  ::dump(int i) const { ind(i); std::cout << "While\n"; cond->dump(i+1); body->dump(i+1); }
void FuncDecl   ::dump(int i) const { ind(i); std::cout << "Func(" << name << ")\n"; body->dump(i+1); }
void Program    ::dump()      const { for (auto& f : funcs) f->dump(0); }

} // namespace minic
