// SPDX-License-Identifier: MIT
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "flowboard/type_tag.hpp"
#include "builtin_types.hpp"

TEST_CASE("type tag for built-in bool is flowboard::Bool") {
    CHECK(flowboard::type_tag_v<bool> == "flowboard::Bool");
}

TEST_CASE("type tag for built-in double is flowboard::Double") {
    CHECK(flowboard::type_tag_v<double> == "flowboard::Double");
}

TEST_CASE("type tag for built-in int64 is flowboard::Int64") {
    CHECK(flowboard::type_tag_v<int64_t> == "flowboard::Int64");
}

TEST_CASE("type tag for std::string is flowboard::String") {
    CHECK(flowboard::type_tag_v<std::string> == "flowboard::String");
}
