// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include "lexer.hpp"
#include "parser.hpp"

using namespace pipegen;

static std::string slurp(std::string const& path) {
    std::ifstream f(path);
    std::ostringstream os; os << f.rdbuf();
    return os.str();
}

TEST_CASE("parser: module with const + typedefs") {
    auto src = slurp("pipegen/tests/fixtures/typedef_only.rmodel");
    REQUIRE(!src.empty());
    auto tokens = lex(src);
    auto file = parse(tokens);
    REQUIRE(file.modules.size() == 1);
    auto& m = *file.modules[0];
    CHECK(m.name == "M_Sample");
    REQUIRE(m.consts.size() == 1);
    CHECK(m.consts[0].name == "Version");
    CHECK(m.consts[0].literal == "1.0.0");
    REQUIRE(m.typedefs.size() == 3);
    CHECK(m.typedefs[0].name == "LongListType");
    CHECK(m.typedefs[1].name == "LongOptionalType");
    CHECK(m.typedefs[2].name == "BoundedStringType");
    // typedef sequence<long, 1> — element long, bound 1
    auto& opt_seq = std::get<ast::SequenceType>(m.typedefs[1].aliased->value);
    REQUIRE(opt_seq.max_size.has_value());
    CHECK(*opt_seq.max_size == 1);
}

TEST_CASE("parser: enum nested in submodule") {
    auto src = slurp("pipegen/tests/fixtures/enum_only.rmodel");
    REQUIRE(!src.empty());
    auto file = parse(lex(src));
    REQUIRE(file.modules.size() == 1);
    auto& m = *file.modules[0];
    REQUIRE(m.submodules.size() == 1);
    auto& sub = *m.submodules[0];
    CHECK(sub.name == "CategoryKind");
    REQUIRE(sub.enums.size() == 1);
    auto& e = sub.enums[0];
    CHECK(e.name == "Type");
    REQUIRE(e.values.size() == 4);
    CHECK(e.values[0] == "Critical_Warning");
    CHECK(e.values[3] == "Advisory");
}

TEST_CASE("parser: struct with primitive + sequence + bounded string fields") {
    auto src = slurp("pipegen/tests/fixtures/struct_only.rmodel");
    REQUIRE(!src.empty());
    auto file = parse(lex(src));
    REQUIRE(file.modules.size() == 1);
    auto& m = *file.modules[0];
    REQUIRE(m.structs.size() == 2);
    CHECK(m.structs[0].name == "Inner");
    CHECK(m.structs[1].name == "Outer");
    REQUIRE(m.structs[1].fields.size() == 3);
    CHECK(m.structs[1].fields[0].name == "First");
    CHECK(m.structs[1].fields[2].name == "BoundedField");
}

TEST_CASE("parser: interface with parameter-less ops") {
    auto src = slurp("pipegen/tests/fixtures/interface_with_void_ops.rmodel");
    REQUIRE(!src.empty());
    auto file = parse(lex(src));
    auto& m = *file.modules[0];
    REQUIRE(m.interfaces.size() == 1);
    auto& iface = m.interfaces[0];
    CHECK(iface.name == "IService");
    REQUIRE(iface.operations.size() == 2);
    CHECK(iface.operations[0].name == "OpA");
    CHECK(iface.operations[0].params.empty());
}

TEST_CASE("parser: interface with typed params") {
    auto src = slurp("pipegen/tests/fixtures/interface_with_params.rmodel");
    REQUIRE(!src.empty());
    auto file = parse(lex(src));
    auto& m = *file.modules[0];
    REQUIRE(m.interfaces.size() == 1);
    auto& iface = m.interfaces[0];
    REQUIRE(iface.operations.size() == 2);
    auto& op = iface.operations[0];
    CHECK(op.name == "ReportPosition");
    REQUIRE(op.params.size() == 3);
    CHECK(op.params[0].name == "KeyId");
    CHECK(op.params[1].name == "Position");
    CHECK(op.params[2].name == "IsRemoved");
}

TEST_CASE("parser: smoke-parse CommonTypes.rmodel") {
    auto src = slurp("external/onboardapi/datamodel/CommonTypes.rmodel");
    REQUIRE(!src.empty());
    REQUIRE_NOTHROW(parse(lex(src)));
}

TEST_CASE("parser: smoke-parse AlertTypes.rmodel") {
    auto src = slurp("external/onboardapi/datamodel/AlertTypes.rmodel");
    REQUIRE(!src.empty());
    REQUIRE_NOTHROW(parse(lex(src)));
}

TEST_CASE("parser: smoke-parse Alert.rmodel") {
    auto src = slurp("external/onboardapi/datamodel/Alert.rmodel");
    REQUIRE(!src.empty());
    REQUIRE_NOTHROW(parse(lex(src)));
}

TEST_CASE("parser: smoke-parse MountTypes.rmodel") {
    auto src = slurp("external/onboardapi/datamodel/MountTypes.rmodel");
    REQUIRE(!src.empty());
    REQUIRE_NOTHROW(parse(lex(src)));
}

TEST_CASE("parser: smoke-parse Mount.rmodel") {
    auto src = slurp("external/onboardapi/datamodel/Mount.rmodel");
    REQUIRE(!src.empty());
    REQUIRE_NOTHROW(parse(lex(src)));
}
