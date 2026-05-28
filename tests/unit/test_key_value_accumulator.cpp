// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "builtin_types.hpp"
#include "flowboard/key_value_accumulator.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

using namespace flowboard;

// Drive the accumulator synchronously through its ports and grab whatever
// the output port emits last. Tests run the node's worker thread, so we
// sleep briefly after each is_removed delivery to let commit() run.
namespace {

struct AccCapture {
    std::shared_ptr<const ListValue> latest;
};

void wait_for_commit() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

}  // namespace

TEST_CASE("KeyValueAccumulator upserts on isRemoved=false") {
    KeyValueAccumulator<std::string, std::string> acc(
        "acc", ::nlohmann::json{{"keyType", "flowboard::String"},
                                 {"valueType", "flowboard::String"}});
    AccCapture cap;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(acc.output("list"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { cap.latest = v; });
    acc.start();

    auto* k = dynamic_cast<InputPort<std::string>*>(acc.input("key"));
    auto* v = dynamic_cast<InputPort<std::string>*>(acc.input("value"));
    auto* r = dynamic_cast<InputPort<bool>*>       (acc.input("isRemoved"));
    REQUIRE(k); REQUIRE(v); REQUIRE(r);

    k->deliver(std::make_shared<const std::string>("vehicle-A"));
    v->deliver(std::make_shared<const std::string>("Alpha"));
    r->deliver(std::make_shared<const bool>(false));
    wait_for_commit();
    REQUIRE(cap.latest);
    CHECK(cap.latest->items.size() == 1);
    CHECK(cap.latest->items[0]["key"]   == "vehicle-A");
    CHECK(cap.latest->items[0]["value"] == "Alpha");

    k->deliver(std::make_shared<const std::string>("vehicle-B"));
    v->deliver(std::make_shared<const std::string>("Bravo"));
    r->deliver(std::make_shared<const bool>(false));
    wait_for_commit();
    REQUIRE(cap.latest);
    CHECK(cap.latest->items.size() == 2);
    CHECK(cap.latest->items[1]["key"]   == "vehicle-B");
    CHECK(cap.latest->items[1]["value"] == "Bravo");

    acc.stop();
}

TEST_CASE("KeyValueAccumulator erases on isRemoved=true") {
    KeyValueAccumulator<std::string, std::string> acc(
        "acc", ::nlohmann::json{{"keyType", "flowboard::String"},
                                 {"valueType", "flowboard::String"}});
    AccCapture cap;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(acc.output("list"));
    out->attach_sink([&](auto v) { cap.latest = v; });
    acc.start();

    auto* k = dynamic_cast<InputPort<std::string>*>(acc.input("key"));
    auto* v = dynamic_cast<InputPort<std::string>*>(acc.input("value"));
    auto* r = dynamic_cast<InputPort<bool>*>       (acc.input("isRemoved"));

    k->deliver(std::make_shared<const std::string>("A"));
    v->deliver(std::make_shared<const std::string>("v1"));
    r->deliver(std::make_shared<const bool>(false));
    k->deliver(std::make_shared<const std::string>("B"));
    v->deliver(std::make_shared<const std::string>("v2"));
    r->deliver(std::make_shared<const bool>(false));
    wait_for_commit();
    REQUIRE(cap.latest);
    CHECK(cap.latest->items.size() == 2);

    // Remove A. A real Report* callback passes all three params on every call
    // (including removals), so deliver a value too — the accumulator pairs the
    // three ports positionally and a removal still consumes one value.
    k->deliver(std::make_shared<const std::string>("A"));
    v->deliver(std::make_shared<const std::string>("v1"));
    r->deliver(std::make_shared<const bool>(true));
    wait_for_commit();
    REQUIRE(cap.latest);
    CHECK(cap.latest->items.size() == 1);
    CHECK(cap.latest->items[0]["key"] == "B");

    acc.stop();
}

// Regression: independent per-edge pump threads can deliver the three ports in
// any cross-port interleaving (a coalescing Report drain emits many trios at
// once). The accumulator must still pair the i-th key with the i-th value/flag.
// Worst case: ALL keys arrive, then ALL values, then ALL isRemoved flags — the
// old "latest pending key/value" design collapsed every commit onto the last
// key. Positional FIFO pairing must keep each key with its own value.
TEST_CASE("KeyValueAccumulator pairs by FIFO position under scrambled cross-port order") {
    KeyValueAccumulator<std::uint32_t, double> acc(
        "acc", ::nlohmann::json{{"keyType", "flowboard::UInt32"},
                                 {"valueType", "flowboard::Double"}});
    AccCapture cap;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(acc.output("list"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { cap.latest = v; });
    acc.start();

    auto* k = dynamic_cast<InputPort<std::uint32_t>*>(acc.input("key"));
    auto* v = dynamic_cast<InputPort<double>*>       (acc.input("value"));
    auto* r = dynamic_cast<InputPort<bool>*>         (acc.input("isRemoved"));
    REQUIRE(k); REQUIRE(v); REQUIRE(r);

    // axis 0 -> 10.0, axis 1 -> 11.0, axis 2 -> 12.0, delivered fully scrambled.
    k->deliver(std::make_shared<const std::uint32_t>(0));
    k->deliver(std::make_shared<const std::uint32_t>(1));
    k->deliver(std::make_shared<const std::uint32_t>(2));
    v->deliver(std::make_shared<const double>(10.0));
    v->deliver(std::make_shared<const double>(11.0));
    v->deliver(std::make_shared<const double>(12.0));
    r->deliver(std::make_shared<const bool>(false));
    r->deliver(std::make_shared<const bool>(false));
    r->deliver(std::make_shared<const bool>(false));
    wait_for_commit();

    REQUIRE(cap.latest);
    REQUIRE(cap.latest->items.size() == 3);
    // Insertion order = key order here; each key keeps its own value.
    CHECK(cap.latest->items[0]["key"]   == 0);
    CHECK(cap.latest->items[0]["value"] == 10.0);
    CHECK(cap.latest->items[1]["key"]   == 1);
    CHECK(cap.latest->items[1]["value"] == 11.0);
    CHECK(cap.latest->items[2]["key"]   == 2);
    CHECK(cap.latest->items[2]["value"] == 12.0);

    acc.stop();
}

TEST_CASE("KeyValueAccumulator updates existing entries in place") {
    KeyValueAccumulator<std::string, std::string> acc(
        "acc", ::nlohmann::json{{"keyType", "flowboard::String"},
                                 {"valueType", "flowboard::String"}});
    AccCapture cap;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(acc.output("list"));
    out->attach_sink([&](auto v) { cap.latest = v; });
    acc.start();

    auto* k = dynamic_cast<InputPort<std::string>*>(acc.input("key"));
    auto* v = dynamic_cast<InputPort<std::string>*>(acc.input("value"));
    auto* r = dynamic_cast<InputPort<bool>*>       (acc.input("isRemoved"));

    k->deliver(std::make_shared<const std::string>("K"));
    v->deliver(std::make_shared<const std::string>("v1"));
    r->deliver(std::make_shared<const bool>(false));
    k->deliver(std::make_shared<const std::string>("K"));
    v->deliver(std::make_shared<const std::string>("v2"));
    r->deliver(std::make_shared<const bool>(false));
    wait_for_commit();
    REQUIRE(cap.latest);
    CHECK(cap.latest->items.size() == 1);
    CHECK(cap.latest->items[0]["value"] == "v2");

    acc.stop();
}
