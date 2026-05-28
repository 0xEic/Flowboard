// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/manipulate_ops.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/tap_registry.hpp"
#include "flowboard/graph.hpp"
#include <atomic>

using namespace flowboard;

namespace {
void wait() { std::this_thread::sleep_for(std::chrono::milliseconds(15)); }

std::shared_ptr<const ListValue> make_list(std::initializer_list<::nlohmann::json> items) {
    auto v = std::make_shared<ListValue>();
    v->items.assign(items.begin(), items.end());
    return v;
}
}  // namespace

TEST_CASE("eval_formula handles arithmetic, functions, precedence") {
    CHECK(eval_formula("x * 2 + 1", 3.0, 0.0) == doctest::Approx(7.0));
    CHECK(eval_formula("(x + 1) * 2", 3.0, 0.0) == doctest::Approx(8.0));
    CHECK(eval_formula("2 ^ 3 ^ 2", 0.0, 0.0) == doctest::Approx(512.0));  // right-assoc
    CHECK(eval_formula("clamp(x, 0, 10)", 25.0, -1.0) == doctest::Approx(10.0));
    CHECK(eval_formula("sqrt(abs(x))", -16.0, 0.0) == doctest::Approx(4.0));
    CHECK(eval_formula("-x + 5", 2.0, 0.0) == doctest::Approx(3.0));
    // malformed → fallback (pass-through)
    CHECK(eval_formula("x * (2 +", 9.0, 9.0) == doctest::Approx(9.0));
    CHECK(eval_formula("", 42.0, 0.0) == doctest::Approx(42.0));
}

TEST_CASE("apply_string_op modes") {
    CHECK(apply_string_op("a-b-c", "replace", "-", "_", "") == "a_b_c");
    CHECK(apply_string_op("anything", "set", "", "", "NEW") == "NEW");
    CHECK(apply_string_op("world", "prepend", "", "", "hello ") == "hello world");
    CHECK(apply_string_op("hello", "append", "", "", " world") == "hello world");
    CHECK(apply_string_op("x", "replace", "", "y", "") == "x");  // empty find → unchanged
}

TEST_CASE("Manipulate.Number applies formula") {
    auto node = NodeRegistry::instance().create("Manipulate.Number", "n",
        ::nlohmann::json{{"formula", "x * 10 - 2"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    double got = 0;
    out->attach_sink([&](auto v) { got = *v; });
    node->start();
    dynamic_cast<InputPort<double>*>(node->input("in"))->deliver(std::make_shared<const double>(5.0));
    wait();
    CHECK(got == doctest::Approx(48.0));
    node->stop();
}

TEST_CASE("Manipulate.Number supports integer value types (in/out typed, result rounded)") {
    auto node = NodeRegistry::instance().create("Manipulate.Number", "ni",
        ::nlohmann::json{{"valueType", "flowboard::Int32"}, {"formula", "x / 2"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<std::int32_t>*>(node->output("out"));
    REQUIRE(out);  // ports are Int32, not Double
    CHECK(dynamic_cast<InputPort<std::int32_t>*>(node->input("in")) != nullptr);
    std::int32_t got = 0;
    out->attach_sink([&](auto v) { got = *v; });
    node->start();
    dynamic_cast<InputPort<std::int32_t>*>(node->input("in"))->deliver(std::make_shared<const std::int32_t>(7));
    wait();
    CHECK(got == 4);  // 7/2 = 3.5 -> rounded to 4
    node->stop();
}

TEST_CASE("Manipulate.String append") {
    auto node = NodeRegistry::instance().create("Manipulate.String", "s",
        ::nlohmann::json{{"mode", "append"}, {"text", "!"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<std::string>*>(node->output("out"));
    REQUIRE(out);
    std::string got;
    out->attach_sink([&](auto v) { got = *v; });
    node->start();
    dynamic_cast<InputPort<std::string>*>(node->input("in"))->deliver(std::make_shared<const std::string>("hi"));
    wait();
    CHECK(got == "hi!");
    node->stop();
}

TEST_CASE("Transform.List.Combine concatenates a then b") {
    auto node = NodeRegistry::instance().create("Transform.List.Combine", "c", ::nlohmann::json::object());
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    REQUIRE(out);
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();
    dynamic_cast<InputPort<ListValue>*>(node->input("a"))->deliver(make_list({ {{"k", 1}}, {{"k", 2}} }));
    dynamic_cast<InputPort<ListValue>*>(node->input("b"))->deliver(make_list({ {{"k", 3}} }));
    wait();
    REQUIRE(got);
    REQUIRE(got->items.size() == 3);
    CHECK(got->items[0]["k"] == 1);
    CHECK(got->items[2]["k"] == 3);
    node->stop();
}

TEST_CASE("unconnected primitive input is seeded from config.defaults at start") {
    auto node = NodeRegistry::instance().create("Manipulate.Number", "m",
        ::nlohmann::json{{"formula", "x * 2"}});
    REQUIRE(node);
    node->set_input_defaults(::nlohmann::json{{"in", 5}});
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    double got = -1;
    std::atomic<int> n{0};
    out->attach_sink([&](auto v) { got = *v; n.fetch_add(1); });

    Graph g;
    g.add_node(std::move(node));
    g.start();
    for (int i = 0; i < 100 && n.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(n.load() >= 1);
    CHECK(got == doctest::Approx(10.0));  // formula(default 5) = 10
    g.stop();
}

TEST_CASE("KeyValueAccumulator supports integer keys (UInt32) with numeric values") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.KeyValueAccumulator", "kv", ::nlohmann::json{
        {"keyType", "flowboard::UInt32"}, {"valueType", "flowboard::Double"}});
    REQUIRE(node);  // (UInt32, Double) must be a registered pair
    CHECK(node->input("key")   != nullptr);
    CHECK(node->input("value") != nullptr);
    CHECK(node->output("list") != nullptr);
}

TEST_CASE("live tap registry serializes scalar + struct-tag ports") {
    // The newly-registered scalar types must have tap factories so their ports
    // stream live values (the KeyAxisIndex regression).
    CHECK(lookup_tap_factory("flowboard::UInt32") != nullptr);
    CHECK(lookup_tap_factory("flowboard::Float")  != nullptr);
    CHECK(lookup_tap_factory("flowboard::Bool")   != nullptr);

    OutputPort<std::uint32_t> p("out");
    std::string key, val;
    auto fn = lookup_tap_factory("flowboard::UInt32");
    REQUIRE(fn);
    fn(&p, "joy.ReportAxis.KeyAxisIndex",
       [&](std::string const& k, std::string const& v) { key = k; val = v; });
    p.emit(std::make_shared<const std::uint32_t>(7));
    CHECK(key == "joy.ReportAxis.KeyAxisIndex");
    CHECK(val == "7");
}

TEST_CASE("Transform.List.MapField number formula on a field") {
    auto node = NodeRegistry::instance().create("Transform.List.MapField", "m",
        ::nlohmann::json{{"field", "v"}, {"valueType", "number"}, {"formula", "x + 100"}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    REQUIRE(out);
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();
    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({ {{"v", 1}}, {{"v", 2}} }));
    wait();
    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    CHECK(got->items[0]["v"] == doctest::Approx(101.0));
    CHECK(got->items[1]["v"] == doctest::Approx(102.0));
    node->stop();
}
