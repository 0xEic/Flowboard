// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "lexer.hpp"
#include "parser.hpp"
#include "emit_stub.hpp"

using namespace pipegen;

TEST_CASE("emit_stub emits struct types + interface shells") {
    std::string src = R"(
        module M_Foo
        {
            struct Position { double X; };
            interface IClient { void ReportPos (in Position P); };
            interface IService { void DoIt (in Position Q); };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_stub_header(*file.modules[0]);
    CHECK(out.find("namespace M_Foo") != std::string::npos);
    CHECK(out.find("struct Position") != std::string::npos);
    CHECK(out.find("class IClient") != std::string::npos);
    CHECK(out.find("class IService") != std::string::npos);
    CHECK(out.find("virtual void ReportPos(Position const&") != std::string::npos);
    CHECK(out.find("virtual void DoIt(Position const&") != std::string::npos);
}

TEST_CASE("emit_stub emits enum sub-modules") {
    std::string src = R"(
        module M_Bar
        {
            module ColorKind
            {
                enum Type { Red, Green, Blue };
            };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_stub_header(*file.modules[0]);
    CHECK(out.find("namespace ColorKind") != std::string::npos);
    CHECK(out.find("enum class Type") != std::string::npos);
    CHECK(out.find("Red") != std::string::npos);
    CHECK(out.find("Blue") != std::string::npos);
}

TEST_CASE("emit_stub does not emit OP_DECLARE_TYPE") {
    std::string src = R"(
        module M_Baz
        {
            struct Foo { double X; };
        }
    )";
    auto file = parse(lex(src));
    auto out = emit_stub_header(*file.modules[0]);
    CHECK(out.find("OP_DECLARE_TYPE") == std::string::npos);
    CHECK(out.find("struct Foo") != std::string::npos);
}
