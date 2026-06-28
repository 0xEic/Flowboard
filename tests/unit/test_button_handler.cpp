// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "M_HidJoystick_types.hpp"

using namespace flowboard;

namespace {

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

::nlohmann::json two_output_cfg() {
    return {{"outputs", {
        {{"match", "byIndex"}, {"index", 3}, {"output", "fire"}},
        {{"match", "byName"},  {"buttonName", "Menu"}},
    }}};
}

struct Caps {
    std::map<std::string, std::shared_ptr<const bool>> last;
};

void wire(Node& n, Caps& c, std::string const& port) {
    dynamic_cast<OutputPort<bool>*>(n.output(port))
        ->attach_sink([&c, port](auto v) { c.last[port] = v; });
}

// Deliver one EventButton as a single atomic struct on the 'args' input.
void fire_event(Node& n, std::uint32_t index, std::string const& name, bool pressed) {
    using ArgsPort = InputPort<M_HidJoystick::EventButton_Args>;
    auto* args = dynamic_cast<ArgsPort*>(n.input("args"));
    REQUIRE(args);
    args->deliver(std::make_shared<const M_HidJoystick::EventButton_Args>(
        M_HidJoystick::EventButton_Args{index, name, pressed}));
}

}  // namespace

TEST_CASE("Input.ButtonHandler exposes a single 'args' struct input, not 3 scalars") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    REQUIRE(node);
    // The atomic EventButton_Args struct input is the only input.
    CHECK(node->input("args") != nullptr);
    CHECK(dynamic_cast<InputPort<M_HidJoystick::EventButton_Args>*>(node->input("args")) != nullptr);
    // The old decomposed scalar inputs are gone.
    CHECK(node->input("ButtonIndex") == nullptr);
    CHECK(node->input("ButtonName")  == nullptr);
    CHECK(node->input("IsSelected")  == nullptr);
    // One bool output per configured button.
    CHECK(node->output("fire") != nullptr);   // byIndex, explicit name
    CHECK(node->output("Menu") != nullptr);    // byName, defaulted to the name
}

TEST_CASE("Input.ButtonHandler routes a press to the matching index output") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    Caps c;
    wire(*node, c, "fire");
    wire(*node, c, "Menu");
    node->start();

    fire_event(*node, 3, "Trigger", true);
    settle();
    REQUIRE(c.last.count("fire"));
    CHECK(*c.last["fire"] == true);
    CHECK(c.last.count("Menu") == 0);  // different button: untouched

    fire_event(*node, 3, "Trigger", false);  // release
    settle();
    REQUIRE(c.last["fire"]);
    CHECK(*c.last["fire"] == false);
    node->stop();
}

TEST_CASE("Input.ButtonHandler routes by name independently of index") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    Caps c;
    wire(*node, c, "fire");
    wire(*node, c, "Menu");
    node->start();

    fire_event(*node, 99, "Menu", true);
    settle();
    REQUIRE(c.last.count("Menu"));
    CHECK(*c.last["Menu"] == true);
    CHECK(c.last.count("fire") == 0);
    node->stop();
}

TEST_CASE("Input.ButtonHandler ignores buttons with no configured output") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    Caps c;
    wire(*node, c, "fire");
    wire(*node, c, "Menu");
    node->start();

    fire_event(*node, 7, "Spare", true);
    settle();
    CHECK(c.last.empty());
    node->stop();
}

TEST_CASE("Input.ButtonHandler routes a whole EventButton atomically (no inter-input race)") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    Caps c;
    wire(*node, c, "fire");
    wire(*node, c, "Menu");
    node->start();

    // index=3 + IsSelected=true delivered together → only "fire" reacts, true.
    fire_event(*node, 3, "ignored-name", true);
    settle();
    REQUIRE(c.last.count("fire"));
    CHECK(*c.last["fire"] == true);
    CHECK(c.last.count("Menu") == 0);
    node->stop();
}

TEST_CASE("Input.ButtonHandler rejects an unknown match mode") {
    ::nlohmann::json bad = {{"outputs", {{{"match", "byColor"}, {"index", 1}}}}};
    CHECK_THROWS(NodeRegistry::instance().create("Input.ButtonHandler", "bh", bad));
}

TEST_CASE("Input.ButtonHandler rejects duplicate output port names") {
    ::nlohmann::json dup = {{"outputs", {
        {{"match", "byIndex"}, {"index", 1}, {"output", "x"}},
        {{"match", "byIndex"}, {"index", 2}, {"output", "x"}},
    }}};
    CHECK_THROWS(NodeRegistry::instance().create("Input.ButtonHandler", "bh", dup));
}

TEST_CASE("Input.ButtonHandler argsType overrides the struct type; unknown type is rejected") {
    ::nlohmann::json bad = {
        {"outputs", {{{"match", "byIndex"}, {"index", 0}}}},
        {"argsType", "NoSuchType::Args"},
    };
    CHECK_THROWS(NodeRegistry::instance().create("Input.ButtonHandler", "bh_x", bad));
}
