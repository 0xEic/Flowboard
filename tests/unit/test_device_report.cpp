// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/node.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

TEST_CASE("OnboardApi.DeviceReport registers, is port-less, and exposes a schema") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("OnboardApi.DeviceReport", "dev", nlohmann::json::object());
    REQUIRE(node);
    CHECK(node->type_name() == "OnboardApi.DeviceReport");
    CHECK(node->inputs().empty());
    CHECK(node->outputs().empty());

    auto schema = reg.schema_for("OnboardApi.DeviceReport");
    REQUIRE(schema.has_value());
    CHECK(schema->schema_json.find("heartbeatSec") != std::string::npos);
}

TEST_CASE("OnboardApi.DeviceReport accepts custom service identity config") {
    auto node = NodeRegistry::instance().create(
        "OnboardApi.DeviceReport", "dev",
        {{"domainId", 3}, {"serviceName", "MyApp"}, {"deviceName", "MyApp"}, {"heartbeatSec", 0}});
    REQUIRE(node);
    CHECK(node->outputs().empty());
}
