// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <string>
#include <vector>
#include "flowboard/json_helpers.hpp"

// A struct that only exposes the engine's 1-arg to_json (like every generated
// onboardapi struct) — nlohmann cannot serialise it directly.
namespace test_jh {
struct Item {
    std::uint32_t Index;
    std::string Name;
};
inline ::nlohmann::json to_json(Item const& v) {
    ::nlohmann::json j = ::nlohmann::json::object();
    j["Index"] = v.Index;
    j["Name"] = v.Name;
    return j;
}
}  // namespace test_jh

using flowboard::to_json_or_passthrough;

TEST_CASE("to_json_or_passthrough serialises a struct with a 1-arg to_json") {
    test_jh::Item it{7, "btn"};
    auto j = to_json_or_passthrough(it);
    CHECK(j["Index"] == 7);
    CHECK(j["Name"] == "btn");
}

TEST_CASE("to_json_or_passthrough serialises a vector<Struct> as a populated array (not null)") {
    // Regression: list-of-struct fields (e.g. JoystickSettingsType.Buttons)
    // used to collapse to JSON null because nlohmann couldn't serialise the
    // element type and ADL had no to_json(vector).
    std::vector<test_jh::Item> items{{1, "a"}, {2, "b"}};
    auto j = to_json_or_passthrough(items);
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 2);
    CHECK(j[0]["Index"] == 1);
    CHECK(j[0]["Name"] == "a");
    CHECK(j[1]["Index"] == 2);
    CHECK(j[1]["Name"] == "b");
}

TEST_CASE("to_json_or_passthrough still handles primitives, strings and vectors of primitives") {
    CHECK(to_json_or_passthrough(42) == 42);
    CHECK(to_json_or_passthrough(std::string{"hi"}) == "hi");
    auto arr = to_json_or_passthrough(std::vector<int>{1, 2, 3});
    REQUIRE(arr.is_array());
    CHECK(arr.size() == 3);
    CHECK(arr[2] == 3);
}
