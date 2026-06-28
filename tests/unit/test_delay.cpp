// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace flowboard;

TEST_CASE("Delay re-emits a value after delayMs and not before") {
    auto node = NodeRegistry::instance().create("Transform.Delay", "d1", {
        {"inputType", "flowboard::Double"},
        {"delayMs", 80}
    });
    REQUIRE(node);
    std::atomic<int> emits{0};
    std::atomic<double> seen{0.0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { seen.store(*v); emits.fetch_add(1); });

    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(3.5));

    // Must NOT have emitted well before the delay elapses.
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(emits.load() == 0);

    // Must emit shortly after the delay elapses.
    for (int i = 0; i < 100 && emits.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(emits.load() == 1);
    CHECK(seen.load() == doctest::Approx(3.5));
    node->stop();
}

#include "flowboard/port_factory_registry.hpp"
#include "flowboard/type_tag.hpp"
#include <mutex>
#include <vector>

// --- A local registered struct type, to exercise the JSON round-trip path that
// --- structs take (mirrors how pipegen registers OnboardAPI structs).
namespace {
struct DelayPair { double a; double b; };
// 1-arg form consumed by to_json_or_passthrough in the port factory's input sink.
inline ::nlohmann::json to_json(DelayPair const& p) {
    return ::nlohmann::json{{"a", p.a}, {"b", p.b}};
}
// 2-arg form consumed by j.get<DelayPair>() in the port factory's emit_from_json.
inline void from_json(::nlohmann::json const& j, DelayPair& p) {
    p.a = j.at("a").get<double>();
    p.b = j.at("b").get<double>();
}
}  // namespace

OP_DECLARE_TYPE(::DelayPair, "test::DelayPair")
OP_REGISTER_PORT_FACTORY("test::DelayPair", ::DelayPair)

TEST_CASE("Delay queues several values independently and emits them in order") {
    auto node = NodeRegistry::instance().create("Transform.Delay", "d2", {
        {"inputType", "flowboard::Int64"},
        {"delayMs", 60}
    });
    REQUIRE(node);
    std::mutex m;
    std::vector<std::int64_t> got;
    auto* out = dynamic_cast<OutputPort<std::int64_t>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { std::scoped_lock lk(m); got.push_back(*v); });

    node->start();
    auto* in = dynamic_cast<InputPort<std::int64_t>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const std::int64_t>(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    in->deliver(std::make_shared<const std::int64_t>(2));

    for (int i = 0; i < 100; ++i) {
        { std::scoped_lock lk(m); if (got.size() >= 2) break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::scoped_lock lk(m);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == 1);   // FIFO: first in, first out
    CHECK(got[1] == 2);
    node->stop();
}

TEST_CASE("Delay round-trips a registered struct type through JSON") {
    auto node = NodeRegistry::instance().create("Transform.Delay", "d3", {
        {"inputType", "test::DelayPair"},
        {"delayMs", 40}
    });
    REQUIRE(node);
    std::atomic<int> emits{0};
    std::atomic<double> a{0.0}, b{0.0};
    auto* out = dynamic_cast<OutputPort<DelayPair>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { a.store(v->a); b.store(v->b); emits.fetch_add(1); });

    node->start();
    auto* in = dynamic_cast<InputPort<DelayPair>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const DelayPair>(DelayPair{1.25, -2.5}));
    for (int i = 0; i < 100 && emits.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(emits.load() == 1);
    CHECK(a.load() == doctest::Approx(1.25));
    CHECK(b.load() == doctest::Approx(-2.5));
    node->stop();
}

TEST_CASE("Delay rejects an unregistered inputType") {
    CHECK_THROWS(NodeRegistry::instance().create("Transform.Delay", "dx", {
        {"inputType", "flowboard::NoSuchType"},
        {"delayMs", 50}
    }));
}

TEST_CASE("Delay drops pending values on stop") {
    auto node = NodeRegistry::instance().create("Transform.Delay", "d4", {
        {"inputType", "flowboard::Double"},
        {"delayMs", 200}
    });
    REQUIRE(node);
    std::atomic<int> emits{0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto) { emits.fetch_add(1); });

    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(9.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));  // well within the delay
    node->stop();                                                // should discard it
    std::this_thread::sleep_for(std::chrono::milliseconds(250)); // past original due time
    CHECK(emits.load() == 0);
}
