// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "builtin_types.hpp"
#include "M_ObjectManager_types.hpp"
#include "flowboard/key_value_accumulator.hpp"
#include "flowboard/key_value_accumulator_registry.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

using namespace flowboard;

namespace {

void settle() { std::this_thread::sleep_for(std::chrono::milliseconds(30)); }

}  // namespace

// End-to-end-ish test: drive the accumulator with the same shape ReportOwnInfo
// produces (KeyPlatformId: string, Obj: OwnInfoType, IsRemoved: bool), pipe
// the resulting list through Transform.List.Sort, then assert ordering. This
// uses the dispatching factory + the generated per-struct accumulator
// registration (pipegen emits OP_REGISTER_KEY_VALUE_ACCUMULATOR_FACTORY for
// every struct, so the (String, OwnInfoType) pair is resolvable).
TEST_CASE("KeyValueAccumulator + List.Sort: ObjectManager ReportOwnInfo shape") {
    auto& reg = NodeRegistry::instance();

    auto acc = reg.create("Transform.KeyValueAccumulator", "acc", {
        {"keyType",   "flowboard::String"},
        {"valueType", "M_ObjectManager::OwnInfoType"},
        {"elementTypeTag", "M_ObjectManager::OwnInfoType"},
        {"orderBy",        "insertion"},
        {"emitOnEmpty",    true}
    });
    REQUIRE(acc != nullptr);

    auto sort = reg.create("Transform.List.Sort", "sort", {
        {"field", "key"}, {"ascending", true}
    });
    REQUIRE(sort != nullptr);

    auto* acc_list  = dynamic_cast<OutputPort<ListValue>*>(acc->output("list"));
    auto* sort_in   = dynamic_cast<InputPort<ListValue>*> (sort->input("in"));
    auto* sort_out  = dynamic_cast<OutputPort<ListValue>*>(sort->output("out"));
    REQUIRE(acc_list); REQUIRE(sort_in); REQUIRE(sort_out);

    // Wire: acc.list → sort.in (in-process; no edges/SPSC, since this is
    // a unit-scope wiring not a Graph integration).
    acc_list->attach_sink([sort_in](auto v) { sort_in->deliver(v); });

    std::shared_ptr<const ListValue> latest;
    sort_out->attach_sink([&](auto v) { latest = v; });

    acc->start();
    sort->start();

    auto* k = dynamic_cast<InputPort<std::string>*>                       (acc->input("key"));
    auto* v = dynamic_cast<InputPort<::M_ObjectManager::OwnInfoType>*>    (acc->input("value"));
    auto* r = dynamic_cast<InputPort<bool>*>                              (acc->input("isRemoved"));
    REQUIRE(k); REQUIRE(v); REQUIRE(r);

    ::M_ObjectManager::OwnInfoType a{}; a.UnitName = "Alpha";
    ::M_ObjectManager::OwnInfoType b{}; b.UnitName = "Bravo";

    // Send the entries in unsorted-by-key order to verify Sort actually runs.
    k->deliver(std::make_shared<const std::string>("vehicle-B"));
    v->deliver(std::make_shared<const ::M_ObjectManager::OwnInfoType>(b));
    r->deliver(std::make_shared<const bool>(false));
    settle();

    k->deliver(std::make_shared<const std::string>("vehicle-A"));
    v->deliver(std::make_shared<const ::M_ObjectManager::OwnInfoType>(a));
    r->deliver(std::make_shared<const bool>(false));
    settle();

    REQUIRE(latest != nullptr);
    REQUIRE(latest->items.size() == 2);
    CHECK(latest->items[0]["key"] == "vehicle-A");
    CHECK(latest->items[1]["key"] == "vehicle-B");

    // Remove vehicle-A. A real Report* callback passes all three params on
    // every call (including removals), so deliver a value too — the accumulator
    // pairs the three ports positionally and a removal still consumes one value.
    k->deliver(std::make_shared<const std::string>("vehicle-A"));
    v->deliver(std::make_shared<const ::M_ObjectManager::OwnInfoType>(a));
    r->deliver(std::make_shared<const bool>(true));
    settle();
    REQUIRE(latest != nullptr);
    REQUIRE(latest->items.size() == 1);
    CHECK(latest->items[0]["key"] == "vehicle-B");

    acc->stop();
    sort->stop();
}
