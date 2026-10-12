#include "minic/Token.h"

namespace minic {

const char* tokenKindName(TokenKind k) {
    switch (k) {
    case TokenKind::INT_LIT:    return "INT_LIT";
    case TokenKind::IDENT:      return "IDENT";
    case TokenKind::KW_INT:     return "int";
    case TokenKind::KW_IF:      return "if";
    case TokenKind::KW_ELSE:    return "else";
    case TokenKind::KW_WHILE:   return "while";
    case TokenKind::KW_RETURN:  return "return";
    case TokenKind::KW_PRINT:   return "print";
    case TokenKind::PLUS:       return "+";
    case TokenKind::MINUS:      return "-";
    case TokenKind::STAR:       return "*";
    case TokenKind::SLASH:      return "/";
    case TokenKind::PERCENT:    return "%";
    case TokenKind::EQ_EQ:      return "==";
    case TokenKind::BANG_EQ:    return "!=";
    case TokenKind::LT:         return "<";
    case TokenKind::LT_EQ:      return "<=";
    case TokenKind::GT:         return ">";
    case TokenKind::GT_EQ:      return ">=";
    case TokenKind::AMP_AMP:    return "&&";
    case TokenKind::PIPE_PIPE:  return "||";
    case TokenKind::BANG:       return "!";
    case TokenKind::EQ:         return "=";
    case TokenKind::LPAREN:     return "(";
    case TokenKind::RPAREN:     return ")";
    case TokenKind::LBRACE:     return "{";
    case TokenKind::RBRACE:     return "}";
    case TokenKind::COMMA:      return ",";
    case TokenKind::SEMICOLON:  return ";";
    case TokenKind::END_OF_FILE: return "EOF";
    case TokenKind::UNKNOWN:    return "UNKNOWN";
    }
    return "?";
}

} // namespace minic
