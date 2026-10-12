#include "minic/Parser.h"
#include "minic/Token.h"
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Parser.cpp — Recursive-descent parser.
//
// Read together with the grammar in the README. Each parseXxx() method
// corresponds to one grammar rule. The structure of the code IS the grammar.
// ─────────────────────────────────────────────────────────────────────────────

namespace minic {

// ── Constructor ───────────────────────────────────────────────────────────────
// Immediately fetch the first token so current_ is always valid.
Parser::Parser(Lexer lex) : lexer_(std::move(lex)), current_(lexer_.nextToken()) {}

// ── Token management ─────────────────────────────────────────────────────────

// Consume current_ and return it; fetch the next token into current_.
Token Parser::advance() {
    Token t   = current_;
    current_  = lexer_.nextToken();
    return t;
}

// Consume and return current_ if it matches k; otherwise throw.
// The `ctx` string appears in the error message ("expected ';' after statement").
Token Parser::expect(TokenKind k, const char* ctx) {
    if (current_.kind != k) {
        error(std::string("expected '") + tokenKindName(k) +
              "' " + ctx + ", but got '" + tokenKindName(current_.kind) + "'");
    }
    return advance();
}

bool Parser::check(TokenKind k) const { return current_.kind == k; }

// Consume and return true only if current_ matches k.
bool Parser::match(TokenKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Program = { FuncDecl }
// ─────────────────────────────────────────────────────────────────────────────
Program Parser::parseProgram() {
    Program prog;
    while (!check(TokenKind::END_OF_FILE)) {
        prog.funcs.push_back(parseFuncDecl());
    }
    return prog;
}

// ─────────────────────────────────────────────────────────────────────────────
// FuncDecl = "int" IDENT "(" ParamList ")" Block
// ParamList = [ "int" IDENT { "," "int" IDENT } ]
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<FuncDecl> Parser::parseFuncDecl() {
    int line = current_.line;
    expect(TokenKind::KW_INT, "at function return type");
    Token nameTok = expect(TokenKind::IDENT, "as function name");
    expect(TokenKind::LPAREN, "after function name");

    std::vector<std::string> params;
    if (!check(TokenKind::RPAREN)) {
        // First parameter
        expect(TokenKind::KW_INT, "at parameter type");
        params.push_back(expect(TokenKind::IDENT, "as parameter name").name());
        // Additional parameters
        while (match(TokenKind::COMMA)) {
            expect(TokenKind::KW_INT, "at parameter type");
            params.push_back(expect(TokenKind::IDENT, "as parameter name").name());
        }
    }
    expect(TokenKind::RPAREN, "after parameter list");

    auto body = parseBlock();

    auto decl    = std::make_unique<FuncDecl>();
    decl->name   = nameTok.name();
    decl->line   = line;
    decl->params = std::move(params);
    decl->body   = std::move(body);
    return decl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Block = "{" { Stmt } "}"
// ─────────────────────────────────────────────────────────────────────────────
std::unique_ptr<BlockStmt> Parser::parseBlock() {
    expect(TokenKind::LBRACE, "at block start");
    auto block = std::make_unique<BlockStmt>();
    // Parse statements until we hit '}' or EOF (EOF guard prevents infinite loop
    // on unterminated blocks).
    while (!check(TokenKind::RBRACE) && !check(TokenKind::END_OF_FILE)) {
        block->stmts.push_back(parseStmt());
    }
    expect(TokenKind::RBRACE, "at block end");
    return block;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stmt dispatch
//
// Most cases are unambiguous from the first token. The one tricky case is IDENT:
//   IDENT "=" ...        → assignment  (need to see the "=")
//   IDENT "(" ...        → call expr-stmt
//   IDENT "+" ...        → expr-stmt
//
// We use lexer_.peek() (which gives us the token AFTER current_) to distinguish.
// This is the only place in the parser that uses 2-token lookahead.
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseStmt() {
    switch (current_.kind) {
    case TokenKind::KW_INT:    return parseVarDecl();
    case TokenKind::KW_IF:     return parseIfStmt();
    case TokenKind::KW_WHILE:  return parseWhileStmt();
    case TokenKind::KW_RETURN: return parseReturnStmt();
    case TokenKind::KW_PRINT:  return parsePrintStmt();
    case TokenKind::IDENT:
        // Peek one token ahead. If it's '=', this is an assignment.
        // lexer_.peek() returns the token AFTER current_ without consuming it.
        if (lexer_.peek().kind == TokenKind::EQ) {
            return parseAssignStmt();
        }
        [[fallthrough]]; // otherwise treat as expression statement
    default:
        return parseExprStmt();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VarDecl = "int" IDENT "=" Expr ";"
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseVarDecl() {
    int line = current_.line;
    expect(TokenKind::KW_INT, "at variable declaration");
    Token nameTok = expect(TokenKind::IDENT, "as variable name");
    expect(TokenKind::EQ, "after variable name in declaration");
    auto init = parseExpr();
    expect(TokenKind::SEMICOLON, "after variable declaration");

    auto node  = std::make_unique<VarDeclStmt>();
    node->name = nameTok.name();
    node->line = line;
    node->init = std::move(init);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// AssignStmt = IDENT "=" Expr ";"
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseAssignStmt() {
    int line = current_.line;
    Token nameTok = expect(TokenKind::IDENT, "in assignment target");
    expect(TokenKind::EQ, "in assignment");
    auto val = parseExpr();
    expect(TokenKind::SEMICOLON, "after assignment");

    auto node   = std::make_unique<AssignStmt>();
    node->name  = nameTok.name();
    node->line  = line;
    node->value = std::move(val);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// IfStmt = "if" "(" Expr ")" Block [ "else" Block ]
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseIfStmt() {
    expect(TokenKind::KW_IF, "at if statement");
    expect(TokenKind::LPAREN, "after 'if'");
    auto cond = parseExpr();
    expect(TokenKind::RPAREN, "after if condition");
    auto thenB = parseBlock();

    StmtPtr elseB;
    if (match(TokenKind::KW_ELSE)) {
        elseB = parseBlock();
    }

    auto node        = std::make_unique<IfStmt>();
    node->cond       = std::move(cond);
    node->thenBranch = std::move(thenB);
    node->elseBranch = std::move(elseB);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// WhileStmt = "while" "(" Expr ")" Block
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseWhileStmt() {
    expect(TokenKind::KW_WHILE, "at while statement");
    expect(TokenKind::LPAREN, "after 'while'");
    auto cond = parseExpr();
    expect(TokenKind::RPAREN, "after while condition");
    auto body = parseBlock();

    auto node  = std::make_unique<WhileStmt>();
    node->cond = std::move(cond);
    node->body = std::move(body);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReturnStmt = "return" Expr ";"
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseReturnStmt() {
    expect(TokenKind::KW_RETURN, "at return statement");
    auto val = parseExpr();
    expect(TokenKind::SEMICOLON, "after return value");

    auto node   = std::make_unique<ReturnStmt>();
    node->value = std::move(val);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// PrintStmt = "print" "(" Expr ")" ";"
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parsePrintStmt() {
    expect(TokenKind::KW_PRINT, "at print statement");
    expect(TokenKind::LPAREN, "after 'print'");
    auto val = parseExpr();
    expect(TokenKind::RPAREN, "after print argument");
    expect(TokenKind::SEMICOLON, "after print statement");

    auto node   = std::make_unique<PrintStmt>();
    node->value = std::move(val);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// ExprStmt = Expr ";"
// ─────────────────────────────────────────────────────────────────────────────
StmtPtr Parser::parseExprStmt() {
    auto expr = parseExpr();
    expect(TokenKind::SEMICOLON, "after expression statement");

    auto node  = std::make_unique<ExprStmt>();
    node->expr = std::move(expr);
    return node;
}

// ═════════════════════════════════════════════════════════════════════════════
// EXPRESSION PARSING — precedence ladder
//
// Lowest precedence at the top (parseExpr/parseOrExpr),
// highest at the bottom (parsePrimary).
//
// Each level handles ONE precedence class. Left-associative operators use a
// while loop so that "a + b + c" builds as "(a+b)+c" (left-leaning tree).
// The one non-left-associative level is parseCmpExpr, which uses a single
// optional comparison (not chained) — `a < b < c` is a parse error.
// ═════════════════════════════════════════════════════════════════════════════

ExprPtr Parser::parseExpr() { return parseOrExpr(); }

// ─────────────────────────────────────────────────────────────────────────────
// OrExpr = AndExpr { "||" AndExpr }
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseOrExpr() {
    auto lhs = parseAndExpr();
    while (check(TokenKind::PIPE_PIPE)) {
        advance();
        auto rhs  = parseAndExpr();
        auto node = std::make_unique<BinaryExpr>();
        node->op  = "||";
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        lhs = std::move(node); // left-fold: lhs becomes the new parent
    }
    return lhs;
}

// ─────────────────────────────────────────────────────────────────────────────
// AndExpr = CmpExpr { "&&" CmpExpr }
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseAndExpr() {
    auto lhs = parseCmpExpr();
    while (check(TokenKind::AMP_AMP)) {
        advance();
        auto rhs  = parseCmpExpr();
        auto node = std::make_unique<BinaryExpr>();
        node->op  = "&&";
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        lhs = std::move(node);
    }
    return lhs;
}

// ─────────────────────────────────────────────────────────────────────────────
// CmpExpr = AddExpr [ CmpOp AddExpr ]
//
// Comparisons are NON-chaining (one optional comparison per expression).
// This matches C semantics: "a < b < c" would be "(a<b) < c" in C, which
// is legal but almost always a bug. Our grammar simply disallows chaining.
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseCmpExpr() {
    auto lhs = parseAddExpr();

    std::string op;
    switch (current_.kind) {
    case TokenKind::EQ_EQ:   op = "=="; break;
    case TokenKind::BANG_EQ: op = "!="; break;
    case TokenKind::LT:      op = "<";  break;
    case TokenKind::LT_EQ:   op = "<="; break;
    case TokenKind::GT:      op = ">";  break;
    case TokenKind::GT_EQ:   op = ">="; break;
    default: return lhs; // no comparison operator → return lhs unchanged
    }

    advance();
    auto rhs  = parseAddExpr();
    auto node = std::make_unique<BinaryExpr>();
    node->op  = op;
    node->lhs = std::move(lhs);
    node->rhs = std::move(rhs);
    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddExpr = MulExpr { ( "+" | "-" ) MulExpr }
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseAddExpr() {
    auto lhs = parseMulExpr();
    while (check(TokenKind::PLUS) || check(TokenKind::MINUS)) {
        std::string op = check(TokenKind::PLUS) ? "+" : "-";
        advance();
        auto rhs  = parseMulExpr();
        auto node = std::make_unique<BinaryExpr>();
        node->op  = op;
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        lhs = std::move(node);
    }
    return lhs;
}

// ─────────────────────────────────────────────────────────────────────────────
// MulExpr = Unary { ( "*" | "/" | "%" ) Unary }
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseMulExpr() {
    auto lhs = parseUnary();
    while (check(TokenKind::STAR) || check(TokenKind::SLASH) || check(TokenKind::PERCENT)) {
        std::string op;
        if      (check(TokenKind::STAR))    op = "*";
        else if (check(TokenKind::SLASH))   op = "/";
        else                                op = "%";
        advance();
        auto rhs  = parseUnary();
        auto node = std::make_unique<BinaryExpr>();
        node->op  = op;
        node->lhs = std::move(lhs);
        node->rhs = std::move(rhs);
        lhs = std::move(node);
    }
    return lhs;
}

// ─────────────────────────────────────────────────────────────────────────────
// Unary = [ "-" | "!" ] Primary
//
// Right-recursive: parseUnary() calls itself, so "!!x" parses as "!(!(x))".
// This is correct — unary operators are right-associative.
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parseUnary() {
    if (check(TokenKind::MINUS) || check(TokenKind::BANG)) {
        char op = check(TokenKind::MINUS) ? '-' : '!';
        advance();
        auto node     = std::make_unique<UnaryExpr>();
        node->op      = op;
        node->operand = parseUnary(); // right-recursive
        return node;
    }
    return parsePrimary();
}

// ─────────────────────────────────────────────────────────────────────────────
// Primary = INT_LIT
//         | IDENT "(" ArgList ")"    (function call)
//         | IDENT                    (variable reference)
//         | "(" Expr ")"             (parenthesised expression)
// ─────────────────────────────────────────────────────────────────────────────
ExprPtr Parser::parsePrimary() {
    // Integer literal
    if (check(TokenKind::INT_LIT)) {
        long long val = current_.intVal();
        advance();
        auto node   = std::make_unique<IntLitExpr>();
        node->value = val;
        return node;
    }

    // Identifier — variable or function call
    if (check(TokenKind::IDENT)) {
        std::string name = current_.name();
        int         line = current_.line;
        advance(); // consume the IDENT

        // If the next token is '(', it's a function call.
        if (match(TokenKind::LPAREN)) {
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RPAREN)) {
                args.push_back(parseExpr());
                while (match(TokenKind::COMMA)) {
                    args.push_back(parseExpr());
                }
            }
            expect(TokenKind::RPAREN, "after call arguments");

            auto node    = std::make_unique<CallExpr>();
            node->callee = name;
            node->line   = line;
            node->args   = std::move(args);
            return node;
        }

        // Plain variable reference.
        auto node  = std::make_unique<VarExpr>();
        node->name = name;
        node->line = line;
        return node;
    }

    // Parenthesised expression — consume '(', parse inner, consume ')'.
    if (match(TokenKind::LPAREN)) {
        auto inner = parseExpr();
        expect(TokenKind::RPAREN, "after parenthesised expression");
        return inner; // note: no wrapper node — the parens affect parsing, not the AST
    }

    error("expected expression, got '" + std::string(tokenKindName(current_.kind)) + "'");
    return nullptr; // unreachable — silences -Wreturn-type
}

// ── Error helper ──────────────────────────────────────────────────────────────
void Parser::error(const std::string& msg) const {
    throw std::runtime_error("Parse error (line " +
                             std::to_string(current_.line) + "): " + msg);
}

} // namespace minic
