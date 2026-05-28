// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

std::unique_ptr<Node> make_random(::nlohmann::json cfg) {
    return NodeRegistry::instance().create("Sources.Random", "rnd", cfg);
}

template <typename T>
std::vector<T> collect(Node& n, int triggers) {
    std::vector<T> got;
    dynamic_cast<OutputPort<T>*>(n.output("out"))
        ->attach_sink([&](auto v) { got.push_back(*v); });
    n.start();
    auto* trig = dynamic_cast<InputPort<bool>*>(n.input("trigger"));
    for (int i = 0; i < triggers; ++i)
        trig->deliver(std::make_shared<const bool>(true));
    settle();
    n.stop();
    return got;
}

}  // namespace

TEST_CASE("Sources.Random Double stays within [min,max]") {
    auto node = make_random({{"outputType", "flowboard::Double"}, {"min", 2.0}, {"max", 7.0},
                             {"autoTriggerOnInit", false}});
    auto got = collect<double>(*node, 50);
    REQUIRE(got.size() == 50);
    for (double v : got) { CHECK(v >= 2.0); CHECK(v <= 7.0); }
}

TEST_CASE("Sources.Random integer with min==max is deterministic") {
    auto node = make_random({{"outputType", "flowboard::Int32"}, {"min", 42}, {"max", 42},
                             {"autoTriggerOnInit", false}});
    auto got = collect<std::int32_t>(*node, 10);
    REQUIRE(got.size() == 10);
    for (auto v : got) CHECK(v == 42);
}

TEST_CASE("Sources.Random integer respects bounds") {
    auto node = make_random({{"outputType", "flowboard::UInt32"}, {"min", 10}, {"max", 20},
                             {"autoTriggerOnInit", false}});
    auto got = collect<std::uint32_t>(*node, 100);
    for (auto v : got) { CHECK(v >= 10u); CHECK(v <= 20u); }
}

TEST_CASE("Sources.Random String honors length and lowercase-only default") {
    auto node = make_random({{"outputType", "flowboard::String"}, {"length", 12},
                             {"autoTriggerOnInit", false}});
    auto got = collect<std::string>(*node, 20);
    REQUIRE(!got.empty());
    for (auto const& s : got) {
        CHECK(s.size() == 12);
        for (char c : s) CHECK((c >= 'a' && c <= 'z'));
    }
}

TEST_CASE("Sources.Random String can include uppercase, digits and symbols") {
    auto node = make_random({{"outputType", "flowboard::String"}, {"length", 200},
                             {"allowLowercase", false}, {"allowUppercase", false},
                             {"allowDigits", true}, {"allowSymbols", false},
                             {"autoTriggerOnInit", false}});
    auto got = collect<std::string>(*node, 1);
    REQUIRE(got.size() == 1);
    for (char c : got[0]) CHECK((c >= '0' && c <= '9'));  // digits-only alphabet
}

TEST_CASE("Sources.Random Bool follows trueProbability extremes") {
    auto always = make_random({{"outputType", "flowboard::Bool"}, {"trueProbability", 1.0},
                               {"autoTriggerOnInit", false}});
    for (bool v : collect<bool>(*always, 20)) CHECK(v == true);

    auto never = make_random({{"outputType", "flowboard::Bool"}, {"trueProbability", 0.0},
                              {"autoTriggerOnInit", false}});
    for (bool v : collect<bool>(*never, 20)) CHECK(v == false);
}

TEST_CASE("Sources.Random auto-triggers one value on init") {
    auto node = make_random({{"outputType", "flowboard::Double"}, {"min", 1.0}, {"max", 1.0},
                             {"autoTriggerOnInit", true}});
    std::vector<double> got;
    dynamic_cast<OutputPort<double>*>(node->output("out"))
        ->attach_sink([&](auto v) { got.push_back(*v); });
    node->start();
    settle();
    node->stop();
    REQUIRE(got.size() == 1);
    CHECK(got[0] == 1.0);
}

TEST_CASE("Sources.Random rejects an unsupported outputType") {
    CHECK_THROWS(make_random({{"outputType", "flowboard::NotAType"}}));
}
