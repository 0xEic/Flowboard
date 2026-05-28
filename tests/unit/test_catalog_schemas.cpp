// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "control/serialization.hpp"
#include "flowboard/registry.hpp"

TEST_CASE("catalog includes configSchema for nodes registered with a schema") {
    auto j = flowboard::control::catalog_to_json();
    bool found = false;
    for (auto const& entry : j) {
        if (entry.value("typeName", std::string{}) == "Test.Schemed") {
            found = true;
            REQUIRE(entry.contains("configSchema"));
            REQUIRE(entry.contains("configDefaults"));
            CHECK(entry["configSchema"].is_object());
            CHECK(entry["configDefaults"].contains("foo"));
            CHECK(entry["configDefaults"]["foo"] == 42);
        }
    }
    CHECK(found);
}

TEST_CASE("catalog still includes nodes without a schema") {
    auto j = flowboard::control::catalog_to_json();
    bool found = false;
    for (auto const& entry : j) {
        if (entry.value("typeName", std::string{}) == "Test.Dummy") {
            found = true;
        }
    }
    CHECK(found);
}
