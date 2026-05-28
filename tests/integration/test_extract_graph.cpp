// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "builtin_types.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "M_Common_types.hpp"
#include "M_Mount_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

namespace {
template <typename T>
bool wait_for(std::atomic<T>& counter, T target, int ms = 1000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (counter.load() >= target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return counter.load() >= target;
}
}  // anonymous

TEST_CASE("Transform.Extract: real MountPositionType.Elevation Optional<Double>") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Extract", "ex", {
        {"inputType", "M_Mount::MountPositionType"},
        {"field",     "Elevation"}
    });
    REQUIRE(node != nullptr);
    auto* value  = dynamic_cast<OutputPort<double>*>(node->output("value"));
    auto* filled = dynamic_cast<OutputPort<bool>*>  (node->output("isFilled"));
    REQUIRE(value);
    REQUIRE(filled);

    std::atomic<int> filled_emits{0}, value_emits{0};
    std::atomic<int> filled_last{-1};
    std::atomic<double> value_last{0};
    filled->attach_sink([&](auto v) { filled_last.store(*v ? 1 : 0); filled_emits.fetch_add(1); });
    value ->attach_sink([&](auto v) { value_last.store(*v); value_emits.fetch_add(1); });

    node->start();
    auto* in = dynamic_cast<InputPort<::M_Mount::MountPositionType>*>(node->input("in"));
    REQUIRE(in);

    SUBCASE("filled optional") {
        ::M_Mount::MountPositionType mp{};
        mp.Elevation = {12.75};  // OnboardAPI optional = vector of 0 or 1
        in->deliver(std::make_shared<const ::M_Mount::MountPositionType>(mp));
        REQUIRE(wait_for<int>(filled_emits, 1));
        REQUIRE(wait_for<int>(value_emits, 1));
        CHECK(filled_last.load() == 1);
        CHECK(value_last.load() == doctest::Approx(12.75));
    }

    SUBCASE("empty optional") {
        ::M_Mount::MountPositionType mp{};
        mp.Elevation = {};
        in->deliver(std::make_shared<const ::M_Mount::MountPositionType>(mp));
        REQUIRE(wait_for<int>(filled_emits, 1));
        CHECK(filled_last.load() == 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK(value_emits.load() == 0);
    }

    node->stop();
}
