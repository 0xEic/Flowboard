// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace flowboard;

namespace {
// Deliver one value at a time with a settle gap so each is fully processed
// before the next — serialises the otherwise-independent in/gate edges so the
// tests assert the steady-state semantics, not a particular race interleaving.
void settle(int ms = 60) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

TEST_CASE("Filter (hold): out tracks in while open and holds the last value on close") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Filter", "f1", {
        {"inputType", "flowboard::Double"}  // closedBehavior defaults to "hold"
    });
    REQUIRE(node);
    std::atomic<int> count{0};
    std::atomic<double> last{-1};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v); count.fetch_add(1); });
    node->start();
    auto* in   = dynamic_cast<InputPort<double>*>(node->input("in"));
    auto* gate = dynamic_cast<InputPort<bool>*>  (node->input("gate"));
    REQUIRE(in); REQUIRE(gate);

    in->deliver(std::make_shared<const double>(1.0));   settle();  // closed: latched, no emit
    CHECK(count.load() == 0);
    gate->deliver(std::make_shared<const bool>(true));  settle();  // open: re-emits latched 1.0
    CHECK(count.load() == 1); CHECK(last.load() == 1.0);
    in->deliver(std::make_shared<const double>(2.0));   settle();  // pass-through
    CHECK(count.load() == 2); CHECK(last.load() == 2.0);
    gate->deliver(std::make_shared<const bool>(false)); settle();  // hold: no emit
    CHECK(count.load() == 2); CHECK(last.load() == 2.0);
    in->deliver(std::make_shared<const double>(3.0));   settle();  // closed: dropped (still latched)
    CHECK(count.load() == 2); CHECK(last.load() == 2.0);
    gate->deliver(std::make_shared<const bool>(true));  settle();  // reopen: re-emits current latched 3.0
    CHECK(count.load() == 3); CHECK(last.load() == 3.0);
    node->stop();
}

TEST_CASE("Filter (bool, hold): closing the gate does not resurrect a stale true") {
    // Reproduces the reported bug: with in eventually false while the gate is
    // open, out must settle to false — not hold a stale true.
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Filter", "fb", {
        {"inputType", "flowboard::Bool"}
    });
    REQUIRE(node);
    std::atomic<int> count{0};
    std::atomic<int> last{-1};
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); count.fetch_add(1); });
    node->start();
    auto* in   = dynamic_cast<InputPort<bool>*>(node->input("in"));
    auto* gate = dynamic_cast<InputPort<bool>*>(node->input("gate"));
    REQUIRE(in); REQUIRE(gate);

    gate->deliver(std::make_shared<const bool>(true));  settle();  // open, nothing latched yet
    in->deliver(std::make_shared<const bool>(true));    settle();  // emit true
    CHECK(last.load() == 1);
    in->deliver(std::make_shared<const bool>(false));   settle();  // emit false (still open)
    CHECK(last.load() == 0);
    gate->deliver(std::make_shared<const bool>(false)); settle();  // hold: stays false
    CHECK(last.load() == 0);
    node->stop();
}

TEST_CASE("Filter (default): emits the configured default value on gate close") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Filter", "fd", {
        {"inputType", "flowboard::Double"},
        {"closedBehavior", "default"},
        {"defaultValue", 99.0},
    });
    REQUIRE(node);
    std::atomic<int> count{0};
    std::atomic<double> last{-1};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v); count.fetch_add(1); });
    node->start();
    auto* in   = dynamic_cast<InputPort<double>*>(node->input("in"));
    auto* gate = dynamic_cast<InputPort<bool>*>  (node->input("gate"));
    REQUIRE(in); REQUIRE(gate);

    in->deliver(std::make_shared<const double>(5.0));   settle();  // latched
    gate->deliver(std::make_shared<const bool>(true));  settle();  // open: emit 5.0
    CHECK(last.load() == 5.0);
    gate->deliver(std::make_shared<const bool>(false)); settle();  // close: emit default 99.0
    CHECK(last.load() == 99.0);
    node->stop();
}

TEST_CASE("Filter supports the newly added primitive types (Int32)") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Filter", "f2", {
        {"inputType", "flowboard::Int32"}
    });
    REQUIRE(node);
    std::atomic<int> emissions{0};
    std::atomic<int> last{0};
    auto* out = dynamic_cast<OutputPort<std::int32_t>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v); emissions.fetch_add(1); });
    node->start();
    auto* in   = dynamic_cast<InputPort<std::int32_t>*>(node->input("in"));
    auto* gate = dynamic_cast<InputPort<bool>*>       (node->input("gate"));
    REQUIRE(in); REQUIRE(gate);

    gate->deliver(std::make_shared<const bool>(true));      settle();  // open, nothing latched
    in->deliver(std::make_shared<const std::int32_t>(7));   settle();  // emit 7
    CHECK(emissions.load() == 1);
    CHECK(last.load() == 7);
    node->stop();
}
