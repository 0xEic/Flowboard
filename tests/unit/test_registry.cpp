// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include <nlohmann/json.hpp>

class DummyNode : public flowboard::Node {
public:
    DummyNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.Dummy") {}
};

OP_REGISTER_NODE("Test.Dummy", DummyNode);

TEST_CASE("registry constructs a node by registered type name") {
    auto& reg = flowboard::NodeRegistry::instance();
    auto node = reg.create("Test.Dummy", "my-id", nlohmann::json::object());
    REQUIRE(node);
    CHECK(node->id() == "my-id");
    CHECK(node->type_name() == "Test.Dummy");
}

TEST_CASE("registry returns nullptr for unknown type") {
    auto& reg = flowboard::NodeRegistry::instance();
    CHECK(reg.create("Does.Not.Exist", "x", nlohmann::json::object()) == nullptr);
}

TEST_CASE("registry lists registered names") {
    auto& reg = flowboard::NodeRegistry::instance();
    auto names = reg.registered_names();
    bool found = false;
    for (auto const& n : names) if (n == "Test.Dummy") { found = true; break; }
    CHECK(found);
}
