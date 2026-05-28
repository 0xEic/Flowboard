// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

TEST_CASE("ConstantSource emits configured string on trigger=true only (autoTrigger off)") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.ConstantSource", "cs", {
        {"outputType", "flowboard::String"},
        {"value", "hello"},
        {"autoTriggerOnInit", false}
    });
    REQUIRE(node);
    std::atomic<int> n{0};
    std::string seen;
    auto* out = dynamic_cast<OutputPort<std::string>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { seen = *v; n.fetch_add(1); });
    node->start();
    auto* trig = dynamic_cast<InputPort<bool>*>(node->input("trigger"));
    REQUIRE(trig);
    trig->deliver(std::make_shared<const bool>(false));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(n.load() == 0);
    trig->deliver(std::make_shared<const bool>(true));
    for (int i = 0; i < 100 && n.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(n.load() == 1);
    CHECK(seen == "hello");
    node->stop();
}

TEST_CASE("ConstantSource tolerates a value of the wrong JSON kind") {
    auto& reg = NodeRegistry::instance();
    // outputType=Bool but value is a leftover string (e.g. from switching type
    // via the connect-menu) — must fall back to the default, not throw.
    std::unique_ptr<Node> node;
    REQUIRE_NOTHROW(node = reg.create("Transform.ConstantSource", "csbad", {
        {"outputType", "flowboard::Bool"},
        {"value", ""},
        {"autoTriggerOnInit", false}
    }));
    REQUIRE(node);
    CHECK(node->output("out") != nullptr);
}

TEST_CASE("ConstantSource auto-triggers on init by default") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.ConstantSource", "cs2", {
        {"outputType", "flowboard::Int64"},
        {"value", 42}
    });
    REQUIRE(node);
    std::atomic<int> n{0};
    std::int64_t seen = 0;
    auto* out = dynamic_cast<OutputPort<std::int64_t>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { seen = *v; n.fetch_add(1); });
    node->start();
    for (int i = 0; i < 100 && n.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(n.load() == 1);
    CHECK(seen == 42);
    node->stop();
}
