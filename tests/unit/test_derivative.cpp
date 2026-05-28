// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
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

TEST_CASE("Derivative: first sample primes, second emits a positive rate for rising input") {
    auto node = NodeRegistry::instance().create("Transform.Derivative", "d1",
        {{"inputType", "flowboard::Double"}});
    REQUIRE(node);
    std::atomic<int>    count{0};
    std::atomic<double> last{0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    auto* in  = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(out); REQUIRE(in);
    out->attach_sink([&](auto v) { last.store(*v); count.fetch_add(1); });

    node->start();
    in->deliver(std::make_shared<const double>(0.0));
    // A rate needs two points — the first sample must not emit.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(count.load() == 0);

    in->deliver(std::make_shared<const double>(5.0));
    REQUIRE(wait_until([&] { return count.load() == 1; }));
    CHECK(std::isfinite(last.load()));
    CHECK(last.load() > 0.0);  // rising value over ~50ms -> positive rate
    node->stop();
}

TEST_CASE("Derivative: falling input yields a negative rate") {
    auto node = NodeRegistry::instance().create("Transform.Derivative", "d2",
        {{"inputType", "flowboard::Double"}});
    REQUIRE(node);
    std::atomic<int>    count{0};
    std::atomic<double> last{0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    auto* in  = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(out); REQUIRE(in);
    out->attach_sink([&](auto v) { last.store(*v); count.fetch_add(1); });

    node->start();
    in->deliver(std::make_shared<const double>(10.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    in->deliver(std::make_shared<const double>(4.0));
    REQUIRE(wait_until([&] { return count.load() == 1; }));
    CHECK(last.load() < 0.0);
    node->stop();
}

TEST_CASE("Derivative: also accepts integer inputs (output is a Double rate)") {
    auto node = NodeRegistry::instance().create("Transform.Derivative", "d3",
        {{"inputType", "flowboard::Int64"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    auto* in  = dynamic_cast<InputPort<std::int64_t>*>(node->input("in"));
    REQUIRE(out); REQUIRE(in);
    node->stop();
}

TEST_CASE("Derivative: rejects a non-numeric inputType") {
    CHECK_THROWS(NodeRegistry::instance().create("Transform.Derivative", "d4",
        {{"inputType", "flowboard::String"}}));
}
