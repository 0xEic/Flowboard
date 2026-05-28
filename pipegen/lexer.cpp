// SPDX-License-Identifier: MIT
#include "lexer.hpp"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace pipegen {
namespace {

const std::unordered_map<std::string_view, TokenKind> kKeywords = {
    {"module", TokenKind::KwModule}, {"interface", TokenKind::KwInterface},
    {"struct", TokenKind::KwStruct}, {"enum", TokenKind::KwEnum},
    {"typedef", TokenKind::KwTypedef}, {"sequence", TokenKind::KwSequence},
    {"void", TokenKind::KwVoid}, {"in", TokenKind::KwIn},
    {"boolean", TokenKind::KwBoolean}, {"string", TokenKind::KwString},
    {"long", TokenKind::KwLong}, {"unsigned", TokenKind::KwUnsigned},
    {"short", TokenKind::KwShort}, {"double", TokenKind::KwDouble},
    {"octet", TokenKind::KwOctet}, {"const", TokenKind::KwConst},
    {"float", TokenKind::KwFloat}, {"char", TokenKind::KwChar},
    {"out", TokenKind::KwOut}, {"inout", TokenKind::KwInout},
};

struct Lexer {
    std::string_view src;
    std::size_t pos = 0;
    int line = 1, column = 1;

    bool eof() const { return pos >= src.size(); }
    char peek(std::size_t off = 0) const {
        return pos + off < src.size() ? src[pos + off] : '\0';
    }
    char advance() {
        char c = src[pos++];
        if (c == '\n') { ++line; column = 1; } else { ++column; }
        return c;
    }
    void skip_whitespace_and_line_comments() {
        while (!eof()) {
            char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) { advance(); }
            else if (c == '/' && peek(1) == '/') {
                // // or /// — consume rest of line
                while (!eof() && peek() != '\n') advance();
            }
            else { break; }
        }
    }

    Token read_doccomment() {
        int sl = line, sc = column;
        // consume "/**"
        advance(); advance(); advance();
        std::string body;
        while (!eof()) {
            if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
            body.push_back(advance());
        }
        return Token{TokenKind::DocComment, std::move(body), sl, sc};
    }

    Token read_preproc() {
        int sl = line, sc = column;
        std::string body;
        while (!eof() && peek() != '\n') body.push_back(advance());
        return Token{TokenKind::Preproc, std::move(body), sl, sc};
    }

    Token read_string() {
        int sl = line, sc = column;
        advance();  // opening "
        std::string body;
        while (!eof() && peek() != '"') body.push_back(advance());
        if (!eof()) advance();  // closing "
        return Token{TokenKind::StringLit, std::move(body), sl, sc};
    }

    Token read_number() {
        int sl = line, sc = column;
        std::string s;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek())))
            s.push_back(advance());
        return Token{TokenKind::IntLit, std::move(s), sl, sc};
    }

    Token read_ident_or_kw() {
        int sl = line, sc = column;
        std::string s;
        while (!eof()) {
            char c = peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') s.push_back(advance());
            else break;
        }
        auto it = kKeywords.find(s);
        TokenKind k = (it == kKeywords.end()) ? TokenKind::Ident : it->second;
        return Token{k, std::move(s), sl, sc};
    }

    Token next() {
        skip_whitespace_and_line_comments();
        if (eof()) return Token{TokenKind::Eof, "", line, column};
        char c = peek();
        // /** ... */ doc comment
        if (c == '/' && peek(1) == '*' && peek(2) == '*') return read_doccomment();
        // /* ... */ ordinary block comment — skip
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (!eof()) {
                if (peek() == '*' && peek(1) == '/') { advance(); advance(); break; }
                advance();
            }
            return next();
        }
        if (c == '#') return read_preproc();
        if (c == '"') return read_string();
        if (std::isdigit(static_cast<unsigned char>(c))) return read_number();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return read_ident_or_kw();

        int sl = line, sc = column;
        char a = advance();
        switch (a) {
            case '{': return Token{TokenKind::LBrace,    "{",  sl, sc};
            case '}': return Token{TokenKind::RBrace,    "}",  sl, sc};
            case '(': return Token{TokenKind::LParen,    "(",  sl, sc};
            case ')': return Token{TokenKind::RParen,    ")",  sl, sc};
            case '[': return Token{TokenKind::LBracket,  "[",  sl, sc};
            case ']': return Token{TokenKind::RBracket,  "]",  sl, sc};
            case ';': return Token{TokenKind::Semicolon, ";",  sl, sc};
            case ',': return Token{TokenKind::Comma,     ",",  sl, sc};
            case '<': return Token{TokenKind::Lt,        "<",  sl, sc};
            case '>': return Token{TokenKind::Gt,        ">",  sl, sc};
            case '=': return Token{TokenKind::Eq,        "=",  sl, sc};
            case ':':
                if (peek() == ':') { advance(); return Token{TokenKind::ColonColon, "::", sl, sc}; }
                return Token{TokenKind::Colon, ":", sl, sc};
        }
        throw std::runtime_error(std::string("lexer: unexpected character '") + a +
                                 "' at line " + std::to_string(sl));
    }
};

}  // namespace

std::vector<Token> lex(std::string_view source) {
    Lexer l{source};
    std::vector<Token> out;
    while (true) {
        Token t = l.next();
        out.push_back(t);
        if (t.kind == TokenKind::Eof) break;
    }
    return out;
}

}  // namespace pipegen
