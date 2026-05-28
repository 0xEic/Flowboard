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

namespace {
bool wait_for_value(std::atomic<int>& got, int expected, int ms = 1000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (got.load() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return got.load() == expected;
}
}

TEST_CASE("Threshold<double> emits true when value > threshold, false otherwise") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Threshold", "t1", {
        {"inputType", "flowboard::Double"},
        {"op", ">"},
        {"value", 10.0}
    });
    REQUIRE(node);

    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    std::atomic<int> last{-1};
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });

    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(11.0));
    REQUIRE(wait_for_value(last, 1));
    last.store(-1);
    in->deliver(std::make_shared<const double>(9.5));
    REQUIRE(wait_for_value(last, 0));
    node->stop();
}

TEST_CASE("Threshold<int32> compares with the configured integer value") {
    auto node = NodeRegistry::instance().create("Transform.Threshold", "ti", {
        {"inputType", "flowboard::Int32"}, {"op", ">="}, {"value", 5}
    });
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    std::atomic<int> last{-1};
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });
    node->start();
    auto* in = dynamic_cast<InputPort<std::int32_t>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const std::int32_t>(5));   // 5 >= 5 -> true
    REQUIRE(wait_for_value(last, 1));
    last.store(-1);
    in->deliver(std::make_shared<const std::int32_t>(4));   // 4 >= 5 -> false
    REQUIRE(wait_for_value(last, 0));
    node->stop();
}

TEST_CASE("Threshold<string> compares lexicographically") {
    auto node = NodeRegistry::instance().create("Transform.Threshold", "ts", {
        {"inputType", "flowboard::String"}, {"op", "=="}, {"value", "go"}
    });
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<bool>*>(node->output("out"));
    REQUIRE(out);
    std::atomic<int> last{-1};
    out->attach_sink([&](auto v) { last.store(*v ? 1 : 0); });
    node->start();
    auto* in = dynamic_cast<InputPort<std::string>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const std::string>("go"));    // == "go" -> true
    REQUIRE(wait_for_value(last, 1));
    last.store(-1);
    in->deliver(std::make_shared<const std::string>("stop"));  // != "go" -> false
    REQUIRE(wait_for_value(last, 0));
    node->stop();
}
