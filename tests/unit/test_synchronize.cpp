// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace flowboard;

namespace {
void settle(int ms = 60) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace

TEST_CASE("Synchronize waits for all inputs, then emits before/values/after in order") {
    auto node = NodeRegistry::instance().create("Transform.Synchronize", "sync1", {
        {"inputType", "flowboard::Double"},
        {"inputCount", 3},
        {"order", {2, 0, 1}},  // emit out2, out0, out1
    });
    REQUIRE(node);

    std::mutex mu;
    std::vector<std::string> log;  // ordered record of emissions
    auto record = [&](std::string s) { std::scoped_lock lk(mu); log.push_back(std::move(s)); };

    auto* before = dynamic_cast<OutputPort<bool>*>(node->output("beforeOutput"));
    auto* after  = dynamic_cast<OutputPort<bool>*>(node->output("afterOutput"));
    REQUIRE(before); REQUIRE(after);
    before->attach_sink([&](auto) { record("before"); });
    after->attach_sink([&](auto) { record("after"); });
    for (int i = 0; i < 3; ++i) {
        auto* o = dynamic_cast<OutputPort<double>*>(node->output("out" + std::to_string(i)));
        REQUIRE(o);
        o->attach_sink([&, i](auto v) { record("out" + std::to_string(i) + "=" + std::to_string(static_cast<int>(*v))); });
    }

    node->start();
    auto in = [&](int i) { return dynamic_cast<InputPort<double>*>(node->input("in" + std::to_string(i))); };
    REQUIRE(in(0)); REQUIRE(in(1)); REQUIRE(in(2));

    // Deliver two of three — no release yet.
    in(0)->deliver(std::make_shared<const double>(10));
    in(1)->deliver(std::make_shared<const double>(11));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }

    // Last input arrives → release.
    in(2)->deliver(std::make_shared<const double>(12));
    settle();
    {
        std::scoped_lock lk(mu);
        REQUIRE(log.size() == 5);
        CHECK(log[0] == "before");
        CHECK(log[1] == "out2=12");   // configured order: 2, 0, 1
        CHECK(log[2] == "out0=10");
        CHECK(log[3] == "out1=11");
        CHECK(log[4] == "after");
    }

    // After release it re-arms: partial set again does not fire.
    { std::scoped_lock lk(mu); log.clear(); }
    in(0)->deliver(std::make_shared<const double>(20));
    in(1)->deliver(std::make_shared<const double>(21));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }
    in(2)->deliver(std::make_shared<const double>(22));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.size() == 5); CHECK(log[1] == "out2=22"); }

    node->stop();
}
