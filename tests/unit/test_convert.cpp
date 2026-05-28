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

TEST_CASE("Convert<double, std::string> formats via template + scalar substitution") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Convert", "m1", {
        {"inputType",  "flowboard::Double"},
        {"outputType", "flowboard::String"},
        {"template",   "value={value}"}
    });
    REQUIRE(node);
    std::atomic<bool> got{false};
    std::string seen;
    auto* out = dynamic_cast<OutputPort<std::string>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { seen = *v; got.store(true); });
    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(42.5));
    for (int i = 0; i < 100 && !got.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(seen == "value=42.500000");
    node->stop();
}

namespace {
template <class TOut>
std::shared_ptr<const TOut> run_convert(nlohmann::json cfg, auto deliver) {
    auto node = NodeRegistry::instance().create("Transform.Convert", "m", cfg);
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<TOut>*>(node->output("out"));
    REQUIRE(out);
    std::shared_ptr<const TOut> seen;
    std::atomic<bool> got{false};
    out->attach_sink([&](auto v) { seen = v; got.store(true); });
    node->start();
    deliver(node.get());
    for (int i = 0; i < 100 && !got.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    node->stop();
    return seen;
}
}  // namespace

TEST_CASE("Convert Int32 -> String formats the integer") {
    auto s = run_convert<std::string>(
        {{"inputType", "flowboard::Int32"}, {"outputType", "flowboard::String"}},
        [](Node* n) {
            dynamic_cast<InputPort<std::int32_t>*>(n->input("in"))->deliver(std::make_shared<const std::int32_t>(7));
        });
    REQUIRE(s);
    CHECK(*s == "7");
}

TEST_CASE("Convert String -> Int32 parses the text") {
    auto v = run_convert<std::int32_t>(
        {{"inputType", "flowboard::String"}, {"outputType", "flowboard::Int32"}},
        [](Node* n) {
            dynamic_cast<InputPort<std::string>*>(n->input("in"))->deliver(std::make_shared<const std::string>("42"));
        });
    REQUIRE(v);
    CHECK(*v == 42);
}

TEST_CASE("Convert Bool -> String") {
    auto s = run_convert<std::string>(
        {{"inputType", "flowboard::Bool"}, {"outputType", "flowboard::String"}},
        [](Node* n) {
            dynamic_cast<InputPort<bool>*>(n->input("in"))->deliver(std::make_shared<const bool>(true));
        });
    REQUIRE(s);
    CHECK(*s == "true");
}

TEST_CASE("Convert Int32 -> String enum lookup uses table then fallback") {
    nlohmann::json cfg = {
        {"inputType", "flowboard::Int32"}, {"outputType", "flowboard::String"},
        {"mode", "lookup"},
        {"mappings", {{{"when", 0}, {"value", "Idle"}}, {{"when", 1}, {"value", "Running"}}}},
        {"fallback", "unknown"},
    };
    auto hit = run_convert<std::string>(cfg, [](Node* n) {
        dynamic_cast<InputPort<std::int32_t>*>(n->input("in"))->deliver(std::make_shared<const std::int32_t>(1));
    });
    REQUIRE(hit); CHECK(*hit == "Running");

    auto miss = run_convert<std::string>(cfg, [](Node* n) {
        dynamic_cast<InputPort<std::int32_t>*>(n->input("in"))->deliver(std::make_shared<const std::int32_t>(9));
    });
    REQUIRE(miss); CHECK(*miss == "unknown");
}

TEST_CASE("Convert scale: Double -> Double with offset and clamp") {
    auto v = run_convert<double>(
        {{"inputType", "flowboard::Double"}, {"outputType", "flowboard::Double"},
         {"mode", "scale"}, {"scale", 2.0}, {"offset", 1.0}, {"clamp", true}, {"clampMin", 0.0}, {"clampMax", 10.0}},
        [](Node* n) {
            dynamic_cast<InputPort<double>*>(n->input("in"))->deliver(std::make_shared<const double>(3.0));  // 3*2+1=7
        });
    REQUIRE(v); CHECK(*v == doctest::Approx(7.0));

    auto clamped = run_convert<double>(
        {{"inputType", "flowboard::Double"}, {"outputType", "flowboard::Double"},
         {"mode", "scale"}, {"scale", 2.0}, {"offset", 1.0}, {"clamp", true}, {"clampMin", 0.0}, {"clampMax", 10.0}},
        [](Node* n) {
            dynamic_cast<InputPort<double>*>(n->input("in"))->deliver(std::make_shared<const double>(100.0));  // ->201 clamps to 10
        });
    REQUIRE(clamped); CHECK(*clamped == doctest::Approx(10.0));
}

TEST_CASE("Convert threshold: Double -> Bool") {
    auto v = run_convert<bool>(
        {{"inputType", "flowboard::Double"}, {"outputType", "flowboard::Bool"},
         {"mode", "threshold"}, {"compareOp", "gte"}, {"threshold", 25.0}},
        [](Node* n) {
            dynamic_cast<InputPort<double>*>(n->input("in"))->deliver(std::make_shared<const double>(25.0));
        });
    REQUIRE(v); CHECK(*v == true);
}

TEST_CASE("Convert boolmap: Bool -> String") {
    auto v = run_convert<std::string>(
        {{"inputType", "flowboard::Bool"}, {"outputType", "flowboard::String"},
         {"mode", "boolmap"}, {"trueValue", "ON"}, {"falseValue", "OFF"}},
        [](Node* n) {
            dynamic_cast<InputPort<bool>*>(n->input("in"))->deliver(std::make_shared<const bool>(false));
        });
    REQUIRE(v); CHECK(*v == "OFF");
}

TEST_CASE("Convert case: String -> String uppercases") {
    auto v = run_convert<std::string>(
        {{"inputType", "flowboard::String"}, {"outputType", "flowboard::String"},
         {"mode", "case"}, {"textTransform", "upper"}},
        [](Node* n) {
            dynamic_cast<InputPort<std::string>*>(n->input("in"))->deliver(std::make_shared<const std::string>("hi there"));
        });
    REQUIRE(v); CHECK(*v == "HI THERE");
}

TEST_CASE("Convert decimals: Double -> String precision") {
    auto v = run_convert<std::string>(
        {{"inputType", "flowboard::Double"}, {"outputType", "flowboard::String"},
         {"template", "{value}"}, {"decimals", 2}},
        [](Node* n) {
            dynamic_cast<InputPort<double>*>(n->input("in"))->deliver(std::make_shared<const double>(3.14159));
        });
    REQUIRE(v); CHECK(*v == "3.14");
}
