// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

template <typename T>
void deliver(Node& n, std::string const& port, T v) {
    dynamic_cast<InputPort<T>*>(n.input(port))->deliver(std::make_shared<const T>(v));
}
void pulse(Node& n, std::string const& port) { deliver<bool>(n, port, true); }

}  // namespace

TEST_CASE("Transform.List.Build derives N item inputs + assembles a list on trigger") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Build", "b", {{"elementType", "flowboard::Double"}, {"count", 3}});
    REQUIRE(node);
    CHECK(node->input("item0") != nullptr);
    CHECK(node->input("item1") != nullptr);
    CHECK(node->input("item2") != nullptr);
    CHECK(node->input("trigger") != nullptr);

    std::shared_ptr<const ListValue> got;
    dynamic_cast<OutputPort<ListValue>*>(node->output("out"))->attach_sink([&](auto v) { got = v; });
    node->start();
    deliver<double>(*node, "item0", 1.0);
    deliver<double>(*node, "item1", 2.0);
    deliver<double>(*node, "item2", 3.0);
    pulse(*node, "trigger");
    settle();
    REQUIRE(got);
    CHECK(got->element_type_tag == "flowboard::Double");
    REQUIRE(got->items.size() == 3);
    CHECK(got->items[0].get<double>() == doctest::Approx(1.0));
    CHECK(got->items[2].get<double>() == doctest::Approx(3.0));
    node->stop();
}

TEST_CASE("Transform.List.Build auto-emits once all items are present") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Build", "b", {{"elementType", "flowboard::Int32"}, {"count", 2}});
    std::shared_ptr<const ListValue> got;
    dynamic_cast<OutputPort<ListValue>*>(node->output("out"))->attach_sink([&](auto v) { got = v; });
    node->start();
    deliver<std::int32_t>(*node, "item0", 5);
    settle();
    CHECK(!got);  // not all items present yet
    deliver<std::int32_t>(*node, "item1", 6);  // now complete -> auto-emit, no trigger needed
    settle();
    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    CHECK(got->items[0].get<int>() == 5);
    CHECK(got->items[1].get<int>() == 6);
    node->stop();
}

TEST_CASE("Transform.List.Accumulate grows and emits on trigger, clears on reset") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Accumulate", "a", {{"elementType", "flowboard::String"}});
    std::shared_ptr<const ListValue> got;
    dynamic_cast<OutputPort<ListValue>*>(node->output("out"))->attach_sink([&](auto v) { got = v; });
    node->start();
    deliver<std::string>(*node, "item", std::string("x"));
    deliver<std::string>(*node, "item", std::string("y"));
    pulse(*node, "trigger");
    settle();
    REQUIRE(got);
    REQUIRE(got->items.size() == 2);
    CHECK(got->items[0].get<std::string>() == "x");

    pulse(*node, "reset");
    deliver<std::string>(*node, "item", std::string("z"));
    pulse(*node, "trigger");
    settle();
    REQUIRE(got->items.size() == 1);
    CHECK(got->items[0].get<std::string>() == "z");
    node->stop();
}

TEST_CASE("Transform.List.Accumulate caps the buffer dropping oldest") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Accumulate", "a", {{"elementType", "flowboard::Int32"}, {"maxItems", 2}});
    std::shared_ptr<const ListValue> got;
    dynamic_cast<OutputPort<ListValue>*>(node->output("out"))->attach_sink([&](auto v) { got = v; });
    node->start();
    for (int i = 1; i <= 4; ++i) deliver<std::int32_t>(*node, "item", i);
    pulse(*node, "trigger");
    settle();
    REQUIRE(got);
    REQUIRE(got->items.size() == 2);   // only the last two
    CHECK(got->items[0].get<int>() == 3);
    CHECK(got->items[1].get<int>() == 4);
    node->stop();
}

TEST_CASE("Transform.List.Constant emits an authored literal list of any element type") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Constant", "c",
        {{"elementType", "M_HidJoystick::ButtonInfoType"},
         {"values", {{{"Index", 0}, {"Name", "A"}}, {{"Index", 1}, {"Name", "B"}}}},
         {"autoTriggerOnInit", true}});
    REQUIRE(node);
    CHECK(node->input("trigger") != nullptr);
    CHECK(node->output("out") != nullptr);
    std::shared_ptr<const ListValue> got;
    dynamic_cast<OutputPort<ListValue>*>(node->output("out"))->attach_sink([&](auto v) { got = v; });
    node->start();
    settle();  // auto-trigger on init emits once
    REQUIRE(got);
    CHECK(got->element_type_tag == "M_HidJoystick::ButtonInfoType");
    REQUIRE(got->items.size() == 2);
    CHECK(got->items[1]["Name"].get<std::string>() == "B");
    node->stop();
}

TEST_CASE("Transform.List.Build supports struct element types (codegen registry)") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Build", "b",
        {{"elementType", "M_HidJoystick::ButtonInfoType"}, {"count", 2}});
    REQUIRE(node);
    auto* item0 = node->input("item0");
    REQUIRE(item0 != nullptr);
    CHECK(item0->type_tag() == "M_HidJoystick::ButtonInfoType");
    CHECK(node->input("item1") != nullptr);
    CHECK(node->output("out") != nullptr);
}

TEST_CASE("Transform.List.Accumulate supports struct element types") {
    auto node = NodeRegistry::instance().create(
        "Transform.List.Accumulate", "a", {{"elementType", "M_HidJoystick::ButtonInfoType"}});
    REQUIRE(node);
    auto* item = node->input("item");
    REQUIRE(item != nullptr);
    CHECK(item->type_tag() == "M_HidJoystick::ButtonInfoType");
}

TEST_CASE("Transform.List.Build rejects a truly unknown element type") {
    CHECK_THROWS(NodeRegistry::instance().create(
        "Transform.List.Build", "b", {{"elementType", "M_Nope::DoesNotExist"}, {"count", 2}}));
}
