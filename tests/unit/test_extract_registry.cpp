// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/extract_registry.hpp"
#include "flowboard/node.hpp"

namespace {
std::unique_ptr<flowboard::Node> fake_factory(std::string id, std::string field) {
    (void)id; (void)field;
    return nullptr;  // marker — test only inspects lookup, not constructed node
}
}

TEST_CASE("extract registry: unknown type returns nullptr") {
    auto fn = flowboard::lookup_extract_factory("does::not::Exist");
    CHECK(fn == nullptr);
}

TEST_CASE("extract registry: registered factory is retrievable") {
    flowboard::register_extract_factory("test::Foo", &fake_factory);
    auto fn = flowboard::lookup_extract_factory("test::Foo");
    REQUIRE(fn != nullptr);
    CHECK(fn == &fake_factory);
}

TEST_CASE("extract registry: re-registration replaces the prior entry") {
    flowboard::register_extract_factory("test::Bar", &fake_factory);
    auto a = flowboard::lookup_extract_factory("test::Bar");
    flowboard::register_extract_factory("test::Bar", nullptr);
    auto b = flowboard::lookup_extract_factory("test::Bar");
    CHECK(a != b);
}
