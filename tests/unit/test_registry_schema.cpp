// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include <nlohmann/json.hpp>

class DummyWithSchema : public flowboard::Node {
public:
    DummyWithSchema(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.Schemed") {}
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Test.Schemed",
    DummyWithSchema,
    R"({"type":"object","properties":{"foo":{"type":"integer"}}})",
    R"({"foo":42})"
)

TEST_CASE("registry returns schema + defaults for registered types") {
    auto& reg = flowboard::NodeRegistry::instance();
    auto info = reg.schema_for("Test.Schemed");
    REQUIRE(info.has_value());
    CHECK(info->schema_json.find("\"foo\"") != std::string::npos);
    CHECK(info->defaults_json.find("42") != std::string::npos);
}

TEST_CASE("registry returns nullopt for nodes registered without schema") {
    auto& reg = flowboard::NodeRegistry::instance();
    // "Test.Dummy" is registered by test_registry.cpp using the schema-less macro.
    auto info = reg.schema_for("Test.Dummy");
    CHECK_FALSE(info.has_value());
}
