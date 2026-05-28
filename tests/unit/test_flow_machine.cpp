// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
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

::nlohmann::json escalation_cfg() {
    return {
        {"initial", "Normal"},
        {"states", {{{"name", "Normal"}}, {{"name", "Warning"}}, {{"name", "Critical"}}}},
        {"transitions", {
            {{"from", "Normal"},   {"to", "Warning"},  {"trigger", "warn"}},
            {{"from", "Warning"},  {"to", "Critical"}, {"trigger", "crit"}},
            {{"from", "Warning"},  {"to", "Normal"},   {"trigger", "clear"}},
            {{"from", "Critical"}, {"to", "Warning"},  {"trigger", "deescalate"}},
        }},
    };
}

// Capture the latest value emitted on each named output port.
struct Capture {
    std::shared_ptr<const std::string> state;
    std::shared_ptr<const std::string> transition;
    std::shared_ptr<const bool>        changed;
    std::map<std::string, std::shared_ptr<const bool>> active;
};

void wire(Node& n, Capture& cap, std::initializer_list<std::string> state_names) {
    dynamic_cast<OutputPort<std::string>*>(n.output("state"))
        ->attach_sink([&](auto v) { cap.state = v; });
    dynamic_cast<OutputPort<std::string>*>(n.output("transition"))
        ->attach_sink([&](auto v) { cap.transition = v; });
    dynamic_cast<OutputPort<bool>*>(n.output("changed"))
        ->attach_sink([&](auto v) { cap.changed = v; });
    for (auto const& s : state_names) {
        dynamic_cast<OutputPort<bool>*>(n.output("active." + s))
            ->attach_sink([&cap, s](auto v) { cap.active[s] = v; });
    }
}

void fire(Node& n, std::string const& trigger) {
    dynamic_cast<InputPort<bool>*>(n.input(trigger))
        ->deliver(std::make_shared<const bool>(true));
}

}  // namespace

TEST_CASE("Flow.StateMachine emits initial state on start") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", escalation_cfg());
    REQUIRE(node);
    Capture cap;
    wire(*node, cap, {"Normal", "Warning", "Critical"});
    node->start();
    settle();
    REQUIRE(cap.state);
    CHECK(*cap.state == "Normal");
    REQUIRE(cap.active.count("Normal"));
    CHECK(*cap.active["Normal"] == true);
    CHECK(*cap.active["Warning"] == false);
    CHECK(*cap.active["Critical"] == false);
    CHECK(cap.transition == nullptr);  // no transition emitted at startup
    node->stop();
}

TEST_CASE("Flow.StateMachine transitions on matching trigger") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", escalation_cfg());
    Capture cap;
    wire(*node, cap, {"Normal", "Warning", "Critical"});
    node->start();
    settle();

    fire(*node, "warn");
    settle();
    CHECK(*cap.state == "Warning");
    CHECK(*cap.active["Normal"] == false);
    CHECK(*cap.active["Warning"] == true);
    REQUIRE(cap.transition);
    CHECK(*cap.transition == "Normal->Warning");
    REQUIRE(cap.changed);
    CHECK(*cap.changed == true);

    fire(*node, "crit");
    settle();
    CHECK(*cap.state == "Critical");
    CHECK(*cap.transition == "Warning->Critical");
    node->stop();
}

TEST_CASE("Flow.StateMachine ignores triggers with no transition from current state") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", escalation_cfg());
    Capture cap;
    wire(*node, cap, {"Normal", "Warning", "Critical"});
    node->start();
    settle();

    // 'crit' only applies in Warning; in Normal it is a no-op.
    fire(*node, "crit");
    settle();
    CHECK(*cap.state == "Normal");
    CHECK(cap.transition == nullptr);
    node->stop();
}

TEST_CASE("Flow.StateMachine full escalation/de-escalation walk") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", escalation_cfg());
    Capture cap;
    wire(*node, cap, {"Normal", "Warning", "Critical"});
    node->start();
    settle();

    fire(*node, "warn");       settle(); CHECK(*cap.state == "Warning");
    fire(*node, "crit");       settle(); CHECK(*cap.state == "Critical");
    fire(*node, "deescalate"); settle(); CHECK(*cap.state == "Warning");
    fire(*node, "clear");      settle(); CHECK(*cap.state == "Normal");
    CHECK(*cap.active["Normal"] == true);
    CHECK(*cap.active["Warning"] == false);
    CHECK(*cap.active["Critical"] == false);
    node->stop();
}

TEST_CASE("Flow.StateMachine first-declared transition wins on shared from+trigger") {
    ::nlohmann::json cfg = {
        {"initial", "A"},
        {"states", {{{"name", "A"}}, {{"name", "B"}}, {{"name", "C"}}}},
        {"transitions", {
            {{"from", "A"}, {"to", "B"}, {"trigger", "go"}},
            {{"from", "A"}, {"to", "C"}, {"trigger", "go"}},
        }},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    Capture cap;
    wire(*node, cap, {"A", "B", "C"});
    node->start();
    settle();
    fire(*node, "go");
    settle();
    CHECK(*cap.state == "B");  // first declared wins
    node->stop();
}

TEST_CASE("Flow.StateMachine self-transition re-emits") {
    ::nlohmann::json cfg = {
        {"initial", "Idle"},
        {"states", {{{"name", "Idle"}}}},
        {"transitions", {{{"from", "Idle"}, {"to", "Idle"}, {"trigger", "kick"}}}},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    Capture cap;
    wire(*node, cap, {"Idle"});
    node->start();
    settle();
    fire(*node, "kick");
    settle();
    REQUIRE(cap.transition);
    CHECK(*cap.transition == "Idle->Idle");
    CHECK(*cap.state == "Idle");
    node->stop();
}

TEST_CASE("Flow.StateMachine construction validates config") {
    auto& reg = NodeRegistry::instance();
    // initial not in states
    CHECK_THROWS(reg.create("Flow.StateMachine", "m", {
        {"initial", "Nope"},
        {"states", {{{"name", "A"}}}},
    }));
    // transition to unknown state
    CHECK_THROWS(reg.create("Flow.StateMachine", "m", {
        {"initial", "A"},
        {"states", {{{"name", "A"}}}},
        {"transitions", {{{"from", "A"}, {"to", "Ghost"}, {"trigger", "go"}}}},
    }));
    // empty states
    CHECK_THROWS(reg.create("Flow.StateMachine", "m", {
        {"initial", "A"}, {"states", ::nlohmann::json::array()},
    }));
}
