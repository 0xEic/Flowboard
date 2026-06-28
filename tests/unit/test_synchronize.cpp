// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
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

TEST_CASE("Synchronize supports per-pair (heterogeneous) types") {
    auto node = NodeRegistry::instance().create("Transform.Synchronize", "synh", {
        {"inputTypes", {"flowboard::Double", "flowboard::String", "flowboard::Int64"}},
        {"order", {0, 1, 2}},
    });
    REQUIRE(node);

    std::mutex mu;
    std::vector<std::string> log;
    auto record = [&](std::string s) { std::scoped_lock lk(mu); log.push_back(std::move(s)); };

    auto* before = dynamic_cast<OutputPort<bool>*>(node->output("beforeOutput"));
    auto* after  = dynamic_cast<OutputPort<bool>*>(node->output("afterOutput"));
    auto* o0 = dynamic_cast<OutputPort<double>*>(node->output("out0"));
    auto* o1 = dynamic_cast<OutputPort<std::string>*>(node->output("out1"));
    auto* o2 = dynamic_cast<OutputPort<std::int64_t>*>(node->output("out2"));
    REQUIRE(before); REQUIRE(after); REQUIRE(o0); REQUIRE(o1); REQUIRE(o2);
    before->attach_sink([&](auto) { record("before"); });
    after->attach_sink([&](auto) { record("after"); });
    o0->attach_sink([&](auto v) { record("out0=" + std::to_string(static_cast<int>(*v))); });
    o1->attach_sink([&](auto v) { record("out1=" + *v); });
    o2->attach_sink([&](auto v) { record("out2=" + std::to_string(*v)); });

    node->start();
    auto* i0 = dynamic_cast<InputPort<double>*>(node->input("in0"));
    auto* i1 = dynamic_cast<InputPort<std::string>*>(node->input("in1"));
    auto* i2 = dynamic_cast<InputPort<std::int64_t>*>(node->input("in2"));
    REQUIRE(i0); REQUIRE(i1); REQUIRE(i2);

    i0->deliver(std::make_shared<const double>(1.0));
    i1->deliver(std::make_shared<const std::string>("hi"));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }

    i2->deliver(std::make_shared<const std::int64_t>(7));
    settle();
    {
        std::scoped_lock lk(mu);
        REQUIRE(log.size() == 5);
        CHECK(log[0] == "before");
        CHECK(log[1] == "out0=1");
        CHECK(log[2] == "out1=hi");
        CHECK(log[3] == "out2=7");
        CHECK(log[4] == "after");
    }

    { std::scoped_lock lk(mu); log.clear(); }
    i0->deliver(std::make_shared<const double>(2.0));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }

    node->stop();
}

TEST_CASE("Synchronize rejects an unregistered type") {
    bool failed = false;
    try {
        auto n = NodeRegistry::instance().create("Transform.Synchronize", "synbad", {
            {"inputTypes", {"flowboard::NotARealType"}},
        });
        failed = (n == nullptr);
    } catch (...) {
        failed = true;
    }
    CHECK(failed);
}

TEST_CASE("Synchronize forceOutput emits current-or-default without the barrier") {
    auto node = NodeRegistry::instance().create("Transform.Synchronize", "synf", {
        {"inputTypes", {"flowboard::Double", "flowboard::Int64"}},
        {"order", {0, 1}},
        {"defaults", {{"in0", 1.5}, {"in1", 7}}},
    });
    REQUIRE(node);

    std::mutex mu;
    std::vector<std::string> log;
    auto record = [&](std::string s) { std::scoped_lock lk(mu); log.push_back(std::move(s)); };

    auto* before = dynamic_cast<OutputPort<bool>*>(node->output("beforeOutput"));
    auto* after  = dynamic_cast<OutputPort<bool>*>(node->output("afterOutput"));
    auto* o0 = dynamic_cast<OutputPort<double>*>(node->output("out0"));
    auto* o1 = dynamic_cast<OutputPort<std::int64_t>*>(node->output("out1"));
    REQUIRE(before); REQUIRE(after); REQUIRE(o0); REQUIRE(o1);
    before->attach_sink([&](auto) { record("before"); });
    after->attach_sink([&](auto) { record("after"); });
    o0->attach_sink([&](auto v) { record("out0=" + std::to_string(*v)); });
    o1->attach_sink([&](auto v) { record("out1=" + std::to_string(*v)); });

    node->start();
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }   // no auto-fire at start

    auto* force = dynamic_cast<InputPort<bool>*>(node->input("forceOutput"));
    REQUIRE(force);

    force->deliver(std::make_shared<const bool>(true));
    settle();
    {
        std::scoped_lock lk(mu);
        REQUIRE(log.size() == 4);
        CHECK(log[0] == "before");
        CHECK(log[1] == "out0=1.500000");
        CHECK(log[2] == "out1=7");
        CHECK(log[3] == "after");
    }

    { std::scoped_lock lk(mu); log.clear(); }
    force->deliver(std::make_shared<const bool>(true));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.size() == 4); }

    { std::scoped_lock lk(mu); log.clear(); }
    auto* i0 = dynamic_cast<InputPort<double>*>(node->input("in0"));
    REQUIRE(i0);
    i0->deliver(std::make_shared<const double>(9.0));
    settle();
    { std::scoped_lock lk(mu); CHECK(log.empty()); }   // barrier not satisfied by one value

    force->deliver(std::make_shared<const bool>(true));
    settle();
    {
        std::scoped_lock lk(mu);
        REQUIRE(log.size() == 4);
        CHECK(log[1] == "out0=9.000000");   // current value wins over default
        CHECK(log[2] == "out1=7");          // still the default
    }

    node->stop();
}
