// SPDX-License-Identifier: MIT
#include "parser.hpp"
#include <stdexcept>

namespace pipegen {
namespace {

struct Parser {
    std::vector<Token> const& toks;
    std::size_t pos = 0;

    Token const& peek(std::size_t off = 0) const {
        if (pos + off >= toks.size()) return toks.back();
        return toks[pos + off];
    }
    Token const& consume() { return toks[pos++]; }
    bool accept(TokenKind k) {
        if (peek().kind == k) { consume(); return true; }
        return false;
    }
    Token const& expect(TokenKind k, char const* what) {
        if (peek().kind != k)
            throw ParseError(std::string("expected ") + what +
                             " at line " + std::to_string(peek().line) +
                             " (got '" + peek().lexeme + "')",
                             peek().line);
        return consume();
    }

    // ---- Top level ----
    ast::File parse_file() {
        ast::File f;
        while (peek().kind != TokenKind::Eof) {
            switch (peek().kind) {
                case TokenKind::Preproc: {
                    auto& s = peek().lexeme;
                    if (s.find("include") != std::string::npos) {
                        auto a = s.find('"'), b = s.rfind('"');
                        if (a != std::string::npos && b > a)
                            f.includes.push_back(s.substr(a + 1, b - a - 1));
                    }
                    consume();
                    break;
                }
                case TokenKind::DocComment:
                    consume();
                    break;
                case TokenKind::KwModule:
                    f.modules.push_back(parse_module());
                    break;
                default:
                    throw ParseError("unexpected token '" + peek().lexeme +
                                     "' at file scope", peek().line);
            }
        }
        return f;
    }

    ast::ModulePtr parse_module() {
        expect(TokenKind::KwModule, "'module'");
        auto m = std::make_shared<ast::Module>();
        m->name = expect(TokenKind::Ident, "module name").lexeme;
        expect(TokenKind::LBrace, "'{'");
        std::size_t decl_order = 0;
        while (peek().kind != TokenKind::RBrace && peek().kind != TokenKind::Eof) {
            switch (peek().kind) {
                case TokenKind::DocComment: consume(); break;
                case TokenKind::KwModule:    m->submodules.push_back(parse_module()); break;
                case TokenKind::KwConst:     m->consts.push_back(parse_const()); break;
                case TokenKind::KwTypedef: {
                    auto td = parse_typedef();
                    td.source_order = decl_order++;
                    m->typedefs.push_back(std::move(td));
                    break;
                }
                case TokenKind::KwEnum: {
                    expect(TokenKind::KwEnum, "'enum'");
                    ast::EnumDecl e;
                    e.name = expect(TokenKind::Ident, "enum name").lexeme;
                    expect(TokenKind::LBrace, "'{'");
                    while (peek().kind != TokenKind::RBrace) {
                        if (peek().kind == TokenKind::DocComment) { consume(); continue; }
                        e.values.push_back(expect(TokenKind::Ident, "enum value").lexeme);
                        if (!accept(TokenKind::Comma)) break;
                    }
                    expect(TokenKind::RBrace, "'}'");
                    accept(TokenKind::Semicolon);
                    m->enums.push_back(std::move(e));
                    break;
                }
                case TokenKind::KwStruct: {
                    expect(TokenKind::KwStruct, "'struct'");
                    ast::StructDecl s;
                    s.name = expect(TokenKind::Ident, "struct name").lexeme;
                    expect(TokenKind::LBrace, "'{'");
                    while (peek().kind != TokenKind::RBrace) {
                        if (peek().kind == TokenKind::DocComment) { consume(); continue; }
                        ast::StructField field;
                        field.type = parse_type();
                        field.name = expect(TokenKind::Ident, "field name").lexeme;
                        expect(TokenKind::Semicolon, "';'");
                        s.fields.push_back(std::move(field));
                    }
                    expect(TokenKind::RBrace, "'}'");
                    accept(TokenKind::Semicolon);
                    s.source_order = decl_order++;
                    m->structs.push_back(std::move(s));
                    break;
                }
                case TokenKind::KwInterface: {
                    expect(TokenKind::KwInterface, "'interface'");
                    ast::InterfaceDecl iface;
                    iface.name = expect(TokenKind::Ident, "interface name").lexeme;
                    // Skip optional interface inheritance clause: ': Base::Interface'
                    if (accept(TokenKind::Colon)) {
                        // Consume base interface name (possibly qualified)
                        expect(TokenKind::Ident, "base interface name");
                        while (accept(TokenKind::ColonColon))
                            expect(TokenKind::Ident, "base interface segment");
                        // Multiple bases separated by commas
                        while (accept(TokenKind::Comma)) {
                            expect(TokenKind::Ident, "base interface name");
                            while (accept(TokenKind::ColonColon))
                                expect(TokenKind::Ident, "base interface segment");
                        }
                    }
                    expect(TokenKind::LBrace, "'{'");
                    while (peek().kind != TokenKind::RBrace) {
                        if (peek().kind == TokenKind::DocComment) { consume(); continue; }
                        expect(TokenKind::KwVoid, "'void'");
                        ast::Operation op;
                        op.name = expect(TokenKind::Ident, "operation name").lexeme;
                        expect(TokenKind::LParen, "'('");
                        while (peek().kind != TokenKind::RParen) {
                            if (peek().kind == TokenKind::DocComment) { consume(); continue; }
                            ast::OpParam pm;
                            pm.direction = "in";
                            if      (peek().kind == TokenKind::KwIn)    { pm.direction = "in";    consume(); }
                            else if (peek().kind == TokenKind::KwOut)   { pm.direction = "out";   consume(); }
                            else if (peek().kind == TokenKind::KwInout) { pm.direction = "inout"; consume(); }
                            pm.type = parse_type();
                            pm.name = expect(TokenKind::Ident, "parameter name").lexeme;
                            op.params.push_back(std::move(pm));
                            if (!accept(TokenKind::Comma)) break;
                        }
                        expect(TokenKind::RParen, "')'");
                        expect(TokenKind::Semicolon, "';'");
                        iface.operations.push_back(std::move(op));
                    }
                    expect(TokenKind::RBrace, "'}'");
                    accept(TokenKind::Semicolon);
                    m->interfaces.push_back(std::move(iface));
                    break;
                }
                default:
                    throw ParseError("unexpected token in module body: '" +
                                     peek().lexeme + "'", peek().line);
            }
        }
        expect(TokenKind::RBrace, "'}'");
        accept(TokenKind::Semicolon);
        return m;
    }

    ast::ConstDecl parse_const() {
        expect(TokenKind::KwConst, "'const'");
        ast::ConstDecl c;
        c.type = parse_type();
        c.name = expect(TokenKind::Ident, "const name").lexeme;
        expect(TokenKind::Eq, "'='");
        auto& lit = consume();
        c.literal = lit.lexeme;
        expect(TokenKind::Semicolon, "';'");
        return c;
    }

    ast::TypedefDecl parse_typedef() {
        expect(TokenKind::KwTypedef, "'typedef'");
        ast::TypedefDecl t;
        t.aliased = parse_type();
        t.name = expect(TokenKind::Ident, "typedef name").lexeme;
        expect(TokenKind::Semicolon, "';'");
        return t;
    }

    // ---- Type references ----
    ast::TypeRefPtr parse_type() {
        auto t = std::make_shared<ast::TypeRef>();
        std::string raw;
        switch (peek().kind) {
            case TokenKind::KwSequence: {
                consume(); raw = "sequence";
                expect(TokenKind::Lt, "'<'");
                ast::SequenceType seq;
                seq.element = parse_type();
                if (accept(TokenKind::Comma)) {
                    auto& n = expect(TokenKind::IntLit, "sequence bound");
                    seq.max_size = std::stoi(n.lexeme);
                }
                expect(TokenKind::Gt, "'>'");
                t->value = std::move(seq);
                break;
            }
            case TokenKind::KwString: {
                consume(); raw = "string";
                ast::PrimitiveType p{"string", std::nullopt};
                if (accept(TokenKind::Lt)) {
                    auto& n = expect(TokenKind::IntLit, "string bound");
                    p.bound = std::stoi(n.lexeme);
                    expect(TokenKind::Gt, "'>'");
                }
                t->value = std::move(p);
                break;
            }
            case TokenKind::KwUnsigned: {
                consume();
                std::string s = "unsigned";
                while (peek().kind == TokenKind::KwLong || peek().kind == TokenKind::KwShort) {
                    s += " " + consume().lexeme;
                }
                t->value = ast::PrimitiveType{s, std::nullopt};
                break;
            }
            case TokenKind::KwLong: {
                consume();
                std::string s = "long";
                if (peek().kind == TokenKind::KwLong) { s += " long"; consume(); }
                t->value = ast::PrimitiveType{s, std::nullopt};
                break;
            }
            case TokenKind::KwBoolean: case TokenKind::KwDouble: case TokenKind::KwOctet:
            case TokenKind::KwFloat:   case TokenKind::KwChar:    case TokenKind::KwShort: {
                auto& tk = consume();
                t->value = ast::PrimitiveType{tk.lexeme, std::nullopt};
                break;
            }
            case TokenKind::Ident: {
                ast::NamedType n;
                n.qualified_name.push_back(consume().lexeme);
                while (peek().kind == TokenKind::ColonColon) {
                    consume();
                    n.qualified_name.push_back(
                        expect(TokenKind::Ident, "qualified type segment").lexeme);
                }
                t->value = std::move(n);
                break;
            }
            default:
                throw ParseError("expected type, got '" + peek().lexeme + "'", peek().line);
        }
        t->raw = std::move(raw);
        return t;
    }
};

}  // namespace

ast::File parse(std::vector<Token> const& tokens) {
    Parser p{tokens};
    return p.parse_file();
}

}  // namespace pipegen
