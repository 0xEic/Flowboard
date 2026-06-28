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

// Two independent chains in one machine, each with its own initial state, so
// two states are active at once.
::nlohmann::json multichain_cfg() {
    return {
        {"states", {
            {{"name", "Idle"},  {"initial", true}},
            {{"name", "Run"}},
            {{"name", "Armed"}, {"initial", true}},
            {{"name", "Fire"}},
        }},
        {"transitions", {
            {{"from", "Idle"},  {"to", "Run"},   {"trigger", "go"}},    // shared trigger
            {{"from", "Armed"}, {"to", "Fire"},  {"trigger", "go"}},    // fires BOTH chains
            {{"from", "Run"},   {"to", "Idle"},  {"trigger", "back"}},
            {{"from", "Fire"},  {"to", "Armed"}, {"trigger", "trip"}},
        }},
    };
}

TEST_CASE("Flow.StateMachine starts every initial state active") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", multichain_cfg());
    REQUIRE(node);
    Capture cap;
    wire(*node, cap, {"Idle", "Run", "Armed", "Fire"});
    node->start();
    settle();
    CHECK(*cap.active["Idle"]  == true);
    CHECK(*cap.active["Armed"] == true);
    CHECK(*cap.active["Run"]   == false);
    CHECK(*cap.active["Fire"]  == false);
    REQUIRE(cap.state);
    CHECK(*cap.state == "Armed,Idle");  // comma-joined, sorted active set
    node->stop();
}

TEST_CASE("Flow.StateMachine advances only the chain that reacts") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", multichain_cfg());
    Capture cap;
    wire(*node, cap, {"Idle", "Run", "Armed", "Fire"});
    node->start();
    settle();

    // 'back' only applies to Run (chain 1, currently in Idle) -> no-op everywhere.
    fire(*node, "back");
    settle();
    CHECK(*cap.active["Idle"]  == true);
    CHECK(*cap.active["Armed"] == true);
    CHECK(*cap.state == "Armed,Idle");
    node->stop();
}

TEST_CASE("Flow.StateMachine fires both chains on a shared trigger") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", multichain_cfg());
    Capture cap;
    wire(*node, cap, {"Idle", "Run", "Armed", "Fire"});
    node->start();
    settle();

    fire(*node, "go");  // Idle->Run AND Armed->Fire
    settle();
    CHECK(*cap.active["Idle"]  == false);
    CHECK(*cap.active["Armed"] == false);
    CHECK(*cap.active["Run"]   == true);
    CHECK(*cap.active["Fire"]  == true);
    CHECK(*cap.state == "Fire,Run");
    REQUIRE(cap.changed);
    CHECK(*cap.changed == true);
    node->stop();
}

// A composite state On with a nested submachine (Warming -> Hot on "warn").
::nlohmann::json nested_cfg() {
    return {
        {"states", {
            {{"name", "Off"}, {"initial", true}},
            {{"name", "On"}, {"machine", {
                {"states", {
                    {{"name", "Warming"}, {"initial", true}},
                    {{"name", "Hot"}},
                }},
                {"transitions", {
                    {{"from", "Warming"}, {"to", "Hot"}, {"trigger", "warn"}},
                }},
            }}},
        }},
        {"transitions", {
            {{"from", "Off"}, {"to", "On"}, {"trigger", "power"}},
            {{"from", "On"}, {"to", "Off"}, {"trigger", "power"}},
        }},
    };
}

TEST_CASE("Flow.StateMachine substate is inactive until its parent is entered") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", nested_cfg());
    REQUIRE(node);
    Capture cap;
    wire(*node, cap, {"Off", "On", "On.Warming", "On.Hot"});
    node->start();
    settle();
    CHECK(*cap.active["Off"]        == true);
    CHECK(*cap.active["On"]         == false);
    CHECK(*cap.active["On.Warming"] == false);
    CHECK(*cap.active["On.Hot"]     == false);
    CHECK(*cap.state == "Off");
    node->stop();
}

TEST_CASE("Flow.StateMachine entering a composite state activates its substate initial") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", nested_cfg());
    Capture cap;
    wire(*node, cap, {"Off", "On", "On.Warming", "On.Hot"});
    node->start();
    settle();
    fire(*node, "power");
    settle();
    CHECK(*cap.active["On"]         == true);
    CHECK(*cap.active["On.Warming"] == true);   // substate started from its initial
    CHECK(*cap.active["On.Hot"]     == false);
    CHECK(*cap.active["Off"]        == false);
    CHECK(*cap.state == "On,On.Warming");
    CHECK(*cap.transition == "Off->On");
    node->stop();
}

TEST_CASE("Flow.StateMachine routes a substate trigger only while the parent is active") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", nested_cfg());
    Capture cap;
    wire(*node, cap, {"Off", "On", "On.Warming", "On.Hot"});
    node->start();
    settle();

    // Parent Off -> the substate isn't running, so On.warn is a no-op.
    fire(*node, "On.warn");
    settle();
    CHECK(*cap.state == "Off");

    fire(*node, "power");   settle();  // enter On (substate -> Warming)
    fire(*node, "On.warn"); settle();  // now the substate advances
    CHECK(*cap.active["On.Warming"] == false);
    CHECK(*cap.active["On.Hot"]     == true);
    CHECK(*cap.active["On"]         == true);
    CHECK(*cap.state == "On,On.Hot");
    CHECK(*cap.transition == "On.Warming->On.Hot");
    node->stop();
}

TEST_CASE("Flow.StateMachine clears the substate on exit and resets it on re-entry") {
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", nested_cfg());
    Capture cap;
    wire(*node, cap, {"Off", "On", "On.Warming", "On.Hot"});
    node->start();
    settle();

    fire(*node, "power");   settle();  // On, Warming
    fire(*node, "On.warn"); settle();  // On, Hot
    fire(*node, "power");   settle();  // exit On -> substate torn down
    CHECK(*cap.active["Off"]        == true);
    CHECK(*cap.active["On"]         == false);
    CHECK(*cap.active["On.Warming"] == false);
    CHECK(*cap.active["On.Hot"]     == false);
    CHECK(*cap.state == "Off");

    fire(*node, "power");   settle();  // re-enter On -> resets to Warming, NOT Hot
    CHECK(*cap.active["On.Warming"] == true);
    CHECK(*cap.active["On.Hot"]     == false);
    CHECK(*cap.state == "On,On.Warming");
    node->stop();
}

TEST_CASE("Flow.StateMachine null transition fires immediately on entry") {
    ::nlohmann::json cfg = {
        {"states", {{{"name", "A"}, {"initial", true}}, {{"name", "B"}}, {{"name", "C"}}}},
        {"transitions", {
            {{"from", "A"}, {"to", "B"}, {"kind", "null"}},                    // A -> B at once
            {{"from", "B"}, {"to", "C"}, {"kind", "trigger"}, {"trigger", "go"}},
        }},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    REQUIRE(node);
    // No input port for the null transition; only "go" exists.
    CHECK(node->input("go") != nullptr);
    Capture cap;
    wire(*node, cap, {"A", "B", "C"});
    node->start();
    settle();
    CHECK(*cap.active["A"] == false);   // null already advanced A -> B
    CHECK(*cap.active["B"] == true);
    CHECK(*cap.state == "B");
    CHECK(*cap.transition == "A->B");
    fire(*node, "go");
    settle();
    CHECK(*cap.active["C"] == true);
    CHECK(*cap.state == "C");
    node->stop();
}

TEST_CASE("Flow.StateMachine null transitions chain to a stable state") {
    ::nlohmann::json cfg = {
        {"states", {{{"name", "A"}, {"initial", true}}, {{"name", "B"}}, {{"name", "C"}}}},
        {"transitions", {
            {{"from", "A"}, {"to", "B"}, {"kind", "null"}},
            {{"from", "B"}, {"to", "C"}, {"kind", "null"}},
        }},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    Capture cap;
    wire(*node, cap, {"A", "B", "C"});
    node->start();
    settle();
    CHECK(*cap.active["C"] == true);   // A -> B -> C settled
    CHECK(*cap.state == "C");
    node->stop();
}

TEST_CASE("Flow.StateMachine timed transition fires after its delay") {
    ::nlohmann::json cfg = {
        {"states", {{{"name", "Idle"}, {"initial", true}}, {{"name", "Done"}}}},
        {"transitions", {
            {{"from", "Idle"}, {"to", "Done"}, {"kind", "timed"}, {"delayMs", 40}},
        }},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    Capture cap;
    wire(*node, cap, {"Idle", "Done"});
    node->start();
    settle();  // 20ms < 40ms -> not yet
    CHECK(*cap.active["Idle"] == true);
    CHECK(*cap.active["Done"] == false);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(*cap.active["Idle"] == false);
    CHECK(*cap.active["Done"] == true);
    CHECK(*cap.state == "Done");
    CHECK(*cap.transition == "Idle->Done");
    node->stop();
}

TEST_CASE("Flow.StateMachine timed transition is cancelled when a trigger wins") {
    ::nlohmann::json cfg = {
        {"states", {{{"name", "Idle"}, {"initial", true}}, {{"name", "Fast"}}, {{"name", "Slow"}}}},
        {"transitions", {
            {{"from", "Idle"}, {"to", "Slow"}, {"kind", "timed"}, {"delayMs", 200}},
            {{"from", "Idle"}, {"to", "Fast"}, {"kind", "trigger"}, {"trigger", "go"}},
        }},
    };
    auto node = NodeRegistry::instance().create("Flow.StateMachine", "m", cfg);
    Capture cap;
    wire(*node, cap, {"Idle", "Fast", "Slow"});
    node->start();
    settle();
    fire(*node, "go");   // beat the timer
    settle();
    CHECK(*cap.active["Fast"] == true);
    CHECK(*cap.active["Idle"] == false);
    std::this_thread::sleep_for(std::chrono::milliseconds(260));  // past the timer
    CHECK(*cap.active["Fast"] == true);    // timer was cancelled on exit
    CHECK(*cap.active["Slow"] == false);
    CHECK(*cap.state == "Fast");
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
