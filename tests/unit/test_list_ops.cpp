// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <memory>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/list_ops.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {

void wait() { std::this_thread::sleep_for(std::chrono::milliseconds(15)); }

std::shared_ptr<const ListValue> make_list(std::initializer_list<::nlohmann::json> items) {
    auto v = std::make_shared<ListValue>();
    v->items.assign(items.begin(), items.end());
    return v;
}

}  // namespace

TEST_CASE("list_ops::resolve_path navigates nested fields") {
    ::nlohmann::json item = {
        {"key", "K1"},
        {"value", {{"Label", "alpha"}, {"Pos", {{"X", 1.5}}}}}
    };
    CHECK(list_ops::resolve_path(item, "key")          == "K1");
    CHECK(list_ops::resolve_path(item, "value.Label")  == "alpha");
    CHECK(list_ops::resolve_path(item, "value.Pos.X")  == 1.5);
    CHECK(list_ops::resolve_path(item, "missing")      == nullptr);
    CHECK(list_ops::resolve_path(item, "value.Missing") == nullptr);
}

TEST_CASE("Transform.List.Sort sorts by field path") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.List.Sort", "s",
        ::nlohmann::json{{"field", "value.score"}, {"ascending", true}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();

    auto in_port = dynamic_cast<InputPort<ListValue>*>(node->input("in"));
    in_port->deliver(make_list({
        {{"key", "a"}, {"value", {{"score", 3}}}},
        {{"key", "b"}, {"value", {{"score", 1}}}},
        {{"key", "c"}, {"value", {{"score", 2}}}}
    }));
    wait();
    REQUIRE(got);
    REQUIRE(got->items.size() == 3);
    CHECK(got->items[0]["key"] == "b");
    CHECK(got->items[1]["key"] == "c");
    CHECK(got->items[2]["key"] == "a");
    node->stop();
}

TEST_CASE("Transform.List.Filter keeps matching elements") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.List.Filter", "f",
        ::nlohmann::json{{"field", "value.score"}, {"op", "gte"}, {"value", 2}});
    REQUIRE(node);
    auto* out = dynamic_cast<OutputPort<ListValue>*>(node->output("out"));
    std::shared_ptr<const ListValue> got;
    out->attach_sink([&](auto v) { got = v; });
    node->start();

    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({
        {{"key", "a"}, {"value", {{"score", 3}}}},
        {{"key", "b"}, {"value", {{"score", 1}}}},
        {{"key", "c"}, {"value", {{"score", 2}}}}
    }));
    wait();
    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    CHECK(got->items[0]["key"] == "a");
    CHECK(got->items[1]["key"] == "c");
    node->stop();
}

TEST_CASE("Transform.List.Size emits count and isEmpty") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.List.Size", "sz", ::nlohmann::json::object());
    REQUIRE(node);
    std::shared_ptr<const std::int64_t> got_count;
    std::shared_ptr<const bool>         got_empty;
    dynamic_cast<OutputPort<std::int64_t>*>(node->output("count"))
        ->attach_sink([&](auto v) { got_count = v; });
    dynamic_cast<OutputPort<bool>*>(node->output("isEmpty"))
        ->attach_sink([&](auto v) { got_empty = v; });
    node->start();

    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({1, 2, 3}));
    wait();
    REQUIRE(got_count); CHECK(*got_count == 3);
    REQUIRE(got_empty); CHECK(*got_empty == false);

    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({}));
    wait();
    REQUIRE(*got_count == 0);
    REQUIRE(*got_empty == true);
    node->stop();
}

TEST_CASE("Transform.List.GetAt indexes positively and negatively") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.List.GetAt", "g",
        ::nlohmann::json{{"index", -1}});
    REQUIRE(node);
    std::shared_ptr<const ListValue> got_v;
    std::shared_ptr<const bool>      got_p;
    dynamic_cast<OutputPort<ListValue>*>(node->output("value"))
        ->attach_sink([&](auto v) { got_v = v; });
    dynamic_cast<OutputPort<bool>*>(node->output("isPresent"))
        ->attach_sink([&](auto v) { got_p = v; });
    node->start();

    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({"a", "b", "c"}));
    wait();
    REQUIRE(got_v);
    REQUIRE(got_v->items.size() == 1);
    CHECK(got_v->items[0] == "c");
    CHECK(*got_p == true);
    node->stop();
}

TEST_CASE("Transform.List.Find returns first match") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.List.Find", "fi",
        ::nlohmann::json{{"field", "key"}, {"op", "eq"}, {"value", "b"}});
    REQUIRE(node);
    std::shared_ptr<const ListValue> got_v;
    std::shared_ptr<const bool>      got_f;
    dynamic_cast<OutputPort<ListValue>*>(node->output("value"))
        ->attach_sink([&](auto v) { got_v = v; });
    dynamic_cast<OutputPort<bool>*>(node->output("isFound"))
        ->attach_sink([&](auto v) { got_f = v; });
    node->start();

    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({
        {{"key", "a"}}, {{"key", "b"}}, {{"key", "c"}}
    }));
    wait();
    REQUIRE(got_v);
    REQUIRE(got_v->items.size() == 1);
    CHECK(got_v->items[0]["key"] == "b");
    CHECK(*got_f == true);

    // No match → empty list, isFound=false
    dynamic_cast<InputPort<ListValue>*>(node->input("in"))->deliver(make_list({
        {{"key", "x"}}, {{"key", "y"}}
    }));
    wait();
    REQUIRE(got_v);
    CHECK(got_v->items.empty());
    CHECK(*got_f == false);
    node->stop();
}
