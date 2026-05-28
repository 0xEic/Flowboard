// SPDX-License-Identifier: MIT
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "lexer.hpp"

using namespace pipegen;

TEST_CASE("lex empty string yields only EOF") {
    auto tokens = lex("");
    REQUIRE(tokens.size() == 1);
    CHECK(tokens[0].kind == TokenKind::Eof);
}

TEST_CASE("lex identifier") {
    auto tokens = lex("foo_bar");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].kind == TokenKind::Ident);
    CHECK(tokens[0].lexeme == "foo_bar");
    CHECK(tokens[1].kind == TokenKind::Eof);
}

TEST_CASE("lex keywords") {
    auto tokens = lex("module interface struct enum typedef sequence void in boolean string long unsigned short double octet const");
    std::vector<TokenKind> expected{
        TokenKind::KwModule, TokenKind::KwInterface, TokenKind::KwStruct,
        TokenKind::KwEnum, TokenKind::KwTypedef, TokenKind::KwSequence,
        TokenKind::KwVoid, TokenKind::KwIn, TokenKind::KwBoolean,
        TokenKind::KwString, TokenKind::KwLong, TokenKind::KwUnsigned,
        TokenKind::KwShort, TokenKind::KwDouble, TokenKind::KwOctet,
        TokenKind::KwConst, TokenKind::Eof,
    };
    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK(tokens[i].kind == expected[i]);
}

TEST_CASE("lex punctuation") {
    auto tokens = lex("{ } ( ) ; , < > :: =");
    std::vector<TokenKind> expected{
        TokenKind::LBrace, TokenKind::RBrace, TokenKind::LParen, TokenKind::RParen,
        TokenKind::Semicolon, TokenKind::Comma, TokenKind::Lt, TokenKind::Gt,
        TokenKind::ColonColon, TokenKind::Eq, TokenKind::Eof,
    };
    REQUIRE(tokens.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK(tokens[i].kind == expected[i]);
}

TEST_CASE("lex string literal") {
    auto tokens = lex(R"("hello world")");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].kind == TokenKind::StringLit);
    CHECK(tokens[0].lexeme == "hello world");
}

TEST_CASE("lex integer and identifier together") {
    auto tokens = lex("sequence<long, 1>");
    REQUIRE(tokens.size() == 7);
    CHECK(tokens[0].kind == TokenKind::KwSequence);
    CHECK(tokens[1].kind == TokenKind::Lt);
    CHECK(tokens[2].kind == TokenKind::KwLong);
    CHECK(tokens[3].kind == TokenKind::Comma);
    CHECK(tokens[4].kind == TokenKind::IntLit);
    CHECK(tokens[4].lexeme == "1");
    CHECK(tokens[5].kind == TokenKind::Gt);
}

TEST_CASE("skip // line comments") {
    auto tokens = lex("foo // bar\nbaz");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].lexeme == "foo");
    CHECK(tokens[1].lexeme == "baz");
}

TEST_CASE("/// triple-slash doc comment is skipped (preserved later if needed)") {
    auto tokens = lex("/// description\nfoo");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].lexeme == "foo");
}

TEST_CASE("/** */ doc comment becomes single DocComment token with content") {
    auto tokens = lex("/** hello\n@startuml\nfoo->bar\n@enduml\n*/");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].kind == TokenKind::DocComment);
    CHECK(tokens[0].lexeme.find("@startuml") != std::string::npos);
    CHECK(tokens[0].lexeme.find("@enduml") != std::string::npos);
}

TEST_CASE("preprocessor directive becomes Preproc token") {
    auto tokens = lex("#include \"CommonTypes.rmodel\"\nfoo");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].kind == TokenKind::Preproc);
    CHECK(tokens[0].lexeme.find("include") != std::string::npos);
    CHECK(tokens[1].lexeme == "foo");
}

TEST_CASE("location tracking") {
    auto tokens = lex("a\nb");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].line == 1);
    CHECK(tokens[1].line == 2);
}
