// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {
void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

template <typename T>
void deliver(Node& n, std::string const& port, T v) {
    dynamic_cast<InputPort<T>*>(n.input(port))->deliver(std::make_shared<const T>(v));
}
}  // namespace

TEST_CASE("Transform.NumberHolder init / increase / decrease / reset") {
    auto node = NodeRegistry::instance().create(
        "Transform.NumberHolder", "h",
        {{"valueType", "flowboard::Int32"}, {"initValue", 0}, {"emitOnInit", false}});
    REQUIRE(node);
    std::optional<std::int32_t> got;
    dynamic_cast<OutputPort<std::int32_t>*>(node->output("value"))
        ->attach_sink([&](auto v) { got = *v; });
    node->start();

    deliver<std::int32_t>(*node, "initValue", 10);
    settle();
    REQUIRE(got.has_value());
    CHECK(*got == 10);

    deliver<std::int32_t>(*node, "increase", 5);
    settle();
    CHECK(*got == 15);

    deliver<std::int32_t>(*node, "decrease", 3);
    settle();
    CHECK(*got == 12);

    deliver<std::int32_t>(*node, "initValue", 0);  // reset to the new initValue
    settle();
    CHECK(*got == 0);
    node->stop();
}

TEST_CASE("Transform.NumberHolder emits its initial value on start") {
    auto node = NodeRegistry::instance().create(
        "Transform.NumberHolder", "h",
        {{"valueType", "flowboard::Double"}, {"initValue", 42.5}, {"emitOnInit", true}});
    std::optional<double> got;
    dynamic_cast<OutputPort<double>*>(node->output("value"))
        ->attach_sink([&](auto v) { got = *v; });
    node->start();
    settle();
    REQUIRE(got.has_value());
    CHECK(*got == doctest::Approx(42.5));
    node->stop();
}

TEST_CASE("Transform.NumberHolder ports are typed by valueType") {
    auto node = NodeRegistry::instance().create(
        "Transform.NumberHolder", "h", {{"valueType", "flowboard::UInt16"}});
    REQUIRE(node);
    CHECK(dynamic_cast<InputPort<std::uint16_t>*>(node->input("increase")) != nullptr);
    CHECK(dynamic_cast<OutputPort<std::uint16_t>*>(node->output("value")) != nullptr);
}

TEST_CASE("Transform.NumberHolder rejects a non-number valueType") {
    CHECK_THROWS(NodeRegistry::instance().create(
        "Transform.NumberHolder", "h", {{"valueType", "flowboard::String"}}));
}
