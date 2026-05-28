// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/extract_nodes.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// A toy struct so the test is self-contained — no pipegen dependency.
struct Probe {
    double                scalar;
    std::vector<double>   opt;     // sequence<double, 1>
    std::vector<double>   list;    // sequence<double>
};

inline ::nlohmann::json to_json(Probe const& p) {
    ::nlohmann::json j = ::nlohmann::json::object();
    j["scalar"] = p.scalar;
    j["opt"]    = p.opt;
    j["list"]   = p.list;
    return j;
}

}  // anonymous

OP_DECLARE_TYPE(::Probe, "test::Probe")

using namespace flowboard;

namespace {
template <typename T>
bool wait_for(std::atomic<int>& counter, int target, int ms = 1000) {
    for (int i = 0; i < ms / 10; ++i) {
        if (counter.load() >= target) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return counter.load() >= target;
}
}  // anonymous

TEST_CASE("ExtractScalar: emits the scalar field exactly once") {
    ExtractScalar<Probe, double> node("x1", "scalar");
    auto* out = dynamic_cast<OutputPort<double>*>(node.output("out"));
    REQUIRE(out);
    std::atomic<int> emits{0};
    std::atomic<double> last{0};
    out->attach_sink([&](auto v) { last.store(*v); emits.fetch_add(1); });

    node.start();
    auto* in = dynamic_cast<InputPort<Probe>*>(node.input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const Probe>(Probe{42.5, {}, {}}));
    REQUIRE(wait_for<double>(emits, 1));
    CHECK(last.load() == doctest::Approx(42.5));
    node.stop();
}

TEST_CASE("ExtractOptional: empty optional emits isFilled=false only") {
    ExtractOptional<Probe, double> node("x2", "opt");
    auto* filled = dynamic_cast<OutputPort<bool>*>  (node.output("isFilled"));
    auto* value  = dynamic_cast<OutputPort<double>*>(node.output("value"));
    REQUIRE(filled);
    REQUIRE(value);

    std::atomic<int> filled_emits{0};
    std::atomic<int> value_emits {0};
    std::atomic<int> filled_last {-1};
    filled->attach_sink([&](auto v) { filled_last.store(*v ? 1 : 0); filled_emits.fetch_add(1); });
    value ->attach_sink([&](auto)   { value_emits.fetch_add(1); });

    node.start();
    auto* in = dynamic_cast<InputPort<Probe>*>(node.input("in"));
    in->deliver(std::make_shared<const Probe>(Probe{0, {}, {}}));
    REQUIRE(wait_for<int>(filled_emits, 1));
    CHECK(filled_last.load() == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(value_emits.load() == 0);
    node.stop();
}

TEST_CASE("ExtractOptional: filled optional emits isFilled=true then value") {
    ExtractOptional<Probe, double> node("x3", "opt");
    auto* filled = dynamic_cast<OutputPort<bool>*>  (node.output("isFilled"));
    auto* value  = dynamic_cast<OutputPort<double>*>(node.output("value"));
    std::atomic<int> filled_emits{0}, value_emits{0};
    std::atomic<int> filled_last{-1};
    std::atomic<double> value_last{0};
    filled->attach_sink([&](auto v) { filled_last.store(*v ? 1 : 0); filled_emits.fetch_add(1); });
    value ->attach_sink([&](auto v) { value_last.store(*v); value_emits.fetch_add(1); });

    node.start();
    auto* in = dynamic_cast<InputPort<Probe>*>(node.input("in"));
    in->deliver(std::make_shared<const Probe>(Probe{0, {17.25}, {}}));
    REQUIRE(wait_for<int>(filled_emits, 1));
    REQUIRE(wait_for<int>(value_emits, 1));
    CHECK(filled_last.load() == 1);
    CHECK(value_last.load() == doctest::Approx(17.25));
    node.stop();
}

TEST_CASE("ExtractList: empty list emits an empty ListValue once") {
    ExtractList<Probe, double> node("x4", "list");
    auto* list = dynamic_cast<OutputPort<ListValue>*>(node.output("list"));
    REQUIRE(list);
    std::atomic<int> emits{0};
    std::shared_ptr<const ListValue> last;
    list->attach_sink([&](auto v) { last = v; emits.fetch_add(1); });

    node.start();
    auto* in = dynamic_cast<InputPort<Probe>*>(node.input("in"));
    in->deliver(std::make_shared<const Probe>(Probe{0, {}, {}}));
    REQUIRE(wait_for<int>(emits, 1));
    REQUIRE(last);
    CHECK(last->items.empty());
    CHECK(last->element_type_tag == "flowboard::Double");
    node.stop();
}

TEST_CASE("ExtractList: three-element list emits one ListValue with all elements") {
    ExtractList<Probe, double> node("x5", "list");
    auto* list = dynamic_cast<OutputPort<ListValue>*>(node.output("list"));
    REQUIRE(list);
    std::atomic<int> emits{0};
    std::shared_ptr<const ListValue> last;
    list->attach_sink([&](auto v) { last = v; emits.fetch_add(1); });

    node.start();
    auto* in = dynamic_cast<InputPort<Probe>*>(node.input("in"));
    in->deliver(std::make_shared<const Probe>(Probe{0, {}, {1.0, 2.0, 3.0}}));
    REQUIRE(wait_for<int>(emits, 1));
    REQUIRE(last);
    REQUIRE(last->items.size() == 3);
    CHECK(last->items[0].get<double>() == doctest::Approx(1.0));
    CHECK(last->items[1].get<double>() == doctest::Approx(2.0));
    CHECK(last->items[2].get<double>() == doctest::Approx(3.0));
    // Only one emission for the whole list (no per-element fanout).
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(emits.load() == 1);
    node.stop();
}
