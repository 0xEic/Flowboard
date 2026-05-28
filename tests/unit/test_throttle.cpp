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

TEST_CASE("Throttle limits emission rate") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Throttle", "th", {
        {"inputType", "flowboard::Double"},
        {"minPeriodMs", 100}
    });
    REQUIRE(node);
    std::atomic<int> emits{0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto) { emits.fetch_add(1); });
    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250)) {
        in->deliver(std::make_shared<const double>(1.0));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(emits.load() >= 2);
    CHECK(emits.load() <= 3);
    node->stop();
}

TEST_CASE("Throttle dispatches via the registry for any registered type (String)") {
    auto node = NodeRegistry::instance().create("Transform.Throttle", "ths", {
        {"inputType", "flowboard::String"},
        {"minPeriodMs", 0}  // never drop, so the first value always passes
    });
    REQUIRE(node);
    std::atomic<int> emits{0};
    std::string seen;
    auto* out = dynamic_cast<OutputPort<std::string>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { seen = *v; emits.fetch_add(1); });
    node->start();
    auto* in = dynamic_cast<InputPort<std::string>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const std::string>("hello"));
    for (int i = 0; i < 100 && emits.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(emits.load() == 1);
    CHECK(seen == "hello");
    node->stop();
}

TEST_CASE("Throttle rejects an unregistered type") {
    CHECK_THROWS(NodeRegistry::instance().create("Transform.Throttle", "thx", {
        {"inputType", "flowboard::NoSuchType"},
        {"minPeriodMs", 100}
    }));
}
