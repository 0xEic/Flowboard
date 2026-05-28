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

// Deliver one EventButton: index, then name, then IsSelected (the trigger).
void fire_event(Node& n, std::uint32_t index, std::string const& name, bool pressed) {
    dynamic_cast<InputPort<std::uint32_t>*>(n.input("ButtonIndex"))
        ->deliver(std::make_shared<const std::uint32_t>(index));
    dynamic_cast<InputPort<std::string>*>(n.input("ButtonName"))
        ->deliver(std::make_shared<const std::string>(name));
    dynamic_cast<InputPort<bool>*>(n.input("IsSelected"))
        ->deliver(std::make_shared<const bool>(pressed));
}

}  // namespace

TEST_CASE("Input.ButtonHandler derives one bool output per configured button") {
    auto node = NodeRegistry::instance().create("Input.ButtonHandler", "bh", two_output_cfg());
    REQUIRE(node);
    CHECK(node->input("ButtonIndex") != nullptr);
    CHECK(node->input("ButtonName") != nullptr);
    CHECK(node->input("IsSelected") != nullptr);
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
