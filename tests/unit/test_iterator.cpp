// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

using namespace flowboard;

namespace {
bool wait_until(std::function<bool()> pred, int ms = 2000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}
}  // namespace

TEST_CASE("Iterator valueType retypes value/start/end/step ports and emits that type") {
    auto node = NodeRegistry::instance().create("Sources.Iterator", "itT", {
        {"valueType", "flowboard::Int32"},
        {"startValue", 0}, {"endValue", 5}, {"stepSize", 1},
        {"timeBetweenStepsMs", 5}, {"autostart", true},
    });
    REQUIRE(node);
    // Ports are now Int32-typed, not Double.
    CHECK(dynamic_cast<OutputPort<std::int32_t>*>(node->output("value")) != nullptr);
    CHECK(dynamic_cast<OutputPort<double>*>(node->output("value")) == nullptr);
    CHECK(dynamic_cast<InputPort<std::int32_t>*>(node->input("startValue")) != nullptr);
    CHECK(dynamic_cast<InputPort<std::int32_t>*>(node->input("stepSize")) != nullptr);

    std::atomic<std::int32_t> last{-1};
    dynamic_cast<OutputPort<std::int32_t>*>(node->output("value"))
        ->attach_sink([&](auto v) { last.store(*v); });
    node->start();
    REQUIRE(wait_until([&] { return last.load() == 5; }));  // reaches the integer end value
    CHECK(last.load() == 5);
    node->stop();
}

TEST_CASE("Iterator sweeps start->end (linear) and reports endReached + running") {
    auto node = NodeRegistry::instance().create("Sources.Iterator", "it1", {
        {"startValue", 0.0}, {"endValue", 10.0}, {"stepSize", 1.0},
        {"timeBetweenStepsMs", 5}, {"loop", false}, {"autoReverse", false},
    });
    REQUIRE(node);
    std::atomic<double> last_value{-999};
    std::atomic<int> end_reached{-1};
    std::atomic<int> running{-1};
    std::atomic<int> started{-1};
    auto* vout = dynamic_cast<OutputPort<double>*>(node->output("value"));
    auto* eout = dynamic_cast<OutputPort<bool>*>(node->output("endReached"));
    auto* rout = dynamic_cast<OutputPort<bool>*>(node->output("running"));
    auto* sout = dynamic_cast<OutputPort<bool>*>(node->output("started"));
    REQUIRE(vout); REQUIRE(eout); REQUIRE(rout); REQUIRE(sout);
    vout->attach_sink([&](auto v) { last_value.store(*v); });
    eout->attach_sink([&](auto v) { end_reached.store(*v ? 1 : 0); });
    rout->attach_sink([&](auto v) { running.store(*v ? 1 : 0); });
    sout->attach_sink([&](auto v) { started.store(*v ? 1 : 0); });

    node->start();
    auto* startTrig = dynamic_cast<InputPort<bool>*>(node->input("start"));
    REQUIRE(startTrig);
    startTrig->deliver(std::make_shared<const bool>(true));

    // It should reach the end value, flag endReached, and stop running.
    REQUIRE(wait_until([&] { return end_reached.load() == 1; }));
    CHECK(last_value.load() == doctest::Approx(10.0));
    CHECK(started.load() == 1);            // was started
    REQUIRE(wait_until([&] { return running.load() == 0; }));  // stops at end (no loop)
    node->stop();
}

TEST_CASE("Iterator stop/pause control the run state") {
    auto node = NodeRegistry::instance().create("Sources.Iterator", "it2", {
        {"startValue", 0.0}, {"endValue", 100.0}, {"stepSize", 1.0},
        {"timeBetweenStepsMs", 10}, {"loop", false}, {"autoReverse", false},
    });
    REQUIRE(node);
    std::atomic<int> running{-1};
    auto* rout = dynamic_cast<OutputPort<bool>*>(node->output("running"));
    REQUIRE(rout);
    rout->attach_sink([&](auto v) { running.store(*v ? 1 : 0); });
    node->start();
    auto* startTrig = dynamic_cast<InputPort<bool>*>(node->input("start"));
    auto* pauseTrig = dynamic_cast<InputPort<bool>*>(node->input("pause"));
    REQUIRE(startTrig); REQUIRE(pauseTrig);

    startTrig->deliver(std::make_shared<const bool>(true));
    REQUIRE(wait_until([&] { return running.load() == 1; }));
    pauseTrig->deliver(std::make_shared<const bool>(true));
    REQUIRE(wait_until([&] { return running.load() == 0; }));
    node->stop();
}

TEST_CASE("Iterator auto-reverse reverses direction (increasing -> decreasing)") {
    auto node = NodeRegistry::instance().create("Sources.Iterator", "it3", {
        {"startValue", 0.0}, {"endValue", 5.0}, {"stepSize", 1.0},
        {"timeBetweenStepsMs", 5}, {"loop", false}, {"autoReverse", true},
    });
    REQUIRE(node);
    std::atomic<int> decreasing{-1};
    auto* dout = dynamic_cast<OutputPort<bool>*>(node->output("decreasing"));
    REQUIRE(dout);
    dout->attach_sink([&](auto v) { decreasing.store(*v ? 1 : 0); });
    node->start();
    dynamic_cast<InputPort<bool>*>(node->input("start"))->deliver(std::make_shared<const bool>(true));
    // After reaching the top it should flip to decreasing.
    REQUIRE(wait_until([&] { return decreasing.load() == 1; }));
    node->stop();
}
