// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pipegen {

enum class TokenKind {
    Eof,
    // Identifiers + literals
    Ident, StringLit, IntLit,
    // Doc / preproc passthrough
    DocComment, Preproc,
    // Keywords
    KwModule, KwInterface, KwStruct, KwEnum, KwTypedef, KwSequence,
    KwVoid, KwIn, KwBoolean, KwString, KwLong, KwUnsigned, KwShort,
    KwDouble, KwOctet, KwConst, KwFloat, KwChar, KwOut, KwInout,
    // Punctuation
    LBrace, RBrace, LParen, RParen, LBracket, RBracket,
    Semicolon, Comma, Colon, ColonColon, Lt, Gt, Eq,
};

struct Token {
    TokenKind   kind;
    std::string lexeme;
    int         line   = 0;
    int         column = 0;
};

std::vector<Token> lex(std::string_view source);

}  // namespace pipegen
