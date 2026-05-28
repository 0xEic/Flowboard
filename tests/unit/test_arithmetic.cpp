// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

template <typename T>
std::optional<T> sum_two(std::string const& type, std::string const& op, T a, T b) {
    auto node = NodeRegistry::instance().create(
        "Transform.Arithmetic", "ar", {{"inputType", type}, {"op", op}});
    REQUIRE(node);
    std::optional<T> got;
    dynamic_cast<OutputPort<T>*>(node->output("out"))->attach_sink([&](auto v) { got = *v; });
    node->start();
    dynamic_cast<InputPort<T>*>(node->input("a"))->deliver(std::make_shared<const T>(a));
    dynamic_cast<InputPort<T>*>(node->input("b"))->deliver(std::make_shared<const T>(b));
    settle();
    node->stop();
    return got;
}

}  // namespace

TEST_CASE("Transform.Arithmetic adds two double inputs") {
    auto r = sum_two<double>("flowboard::Double", "add", 2.5, 3.0);
    REQUIRE(r.has_value());
    CHECK(*r == doctest::Approx(5.5));
}

TEST_CASE("Transform.Arithmetic supports the four ops + min/max") {
    CHECK(*sum_two<double>("flowboard::Double", "subtract", 10.0, 4.0) == doctest::Approx(6.0));
    CHECK(*sum_two<double>("flowboard::Double", "multiply", 3.0, 4.0) == doctest::Approx(12.0));
    CHECK(*sum_two<double>("flowboard::Double", "divide",   9.0, 2.0) == doctest::Approx(4.5));
    CHECK(*sum_two<double>("flowboard::Double", "min",      3.0, 7.0) == doctest::Approx(3.0));
    CHECK(*sum_two<double>("flowboard::Double", "max",      3.0, 7.0) == doctest::Approx(7.0));
    CHECK(*sum_two<double>("flowboard::Double", "+",        1.0, 2.0) == doctest::Approx(3.0));
}

TEST_CASE("Transform.Arithmetic sums integer types") {
    CHECK(*sum_two<std::int32_t>("flowboard::Int32", "add", 40, 2) == 42);
    CHECK(*sum_two<std::uint32_t>("flowboard::UInt32", "add", 1u, 2u) == 3u);
}

TEST_CASE("Transform.Arithmetic skips integer divide-by-zero (no emit)") {
    auto r = sum_two<std::int32_t>("flowboard::Int32", "divide", 10, 0);
    CHECK(!r.has_value());
}

TEST_CASE("Transform.Arithmetic waits for both inputs before emitting") {
    auto node = NodeRegistry::instance().create(
        "Transform.Arithmetic", "ar", {{"inputType", "flowboard::Double"}, {"op", "add"}});
    REQUIRE(node);
    std::optional<double> got;
    dynamic_cast<OutputPort<double>*>(node->output("out"))->attach_sink([&](auto v) { got = *v; });
    node->start();
    dynamic_cast<InputPort<double>*>(node->input("a"))->deliver(std::make_shared<const double>(5.0));
    settle();
    CHECK(!got.has_value());  // only 'a' arrived
    dynamic_cast<InputPort<double>*>(node->input("b"))->deliver(std::make_shared<const double>(1.0));
    settle();
    REQUIRE(got.has_value());
    CHECK(*got == doctest::Approx(6.0));
    node->stop();
}

TEST_CASE("Transform.Arithmetic rejects non-numeric input types") {
    CHECK_THROWS(NodeRegistry::instance().create(
        "Transform.Arithmetic", "ar", {{"inputType", "flowboard::String"}, {"op", "add"}}));
}
