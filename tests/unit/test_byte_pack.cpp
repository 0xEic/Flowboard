// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/list_value.hpp"
#include "builtin_types.hpp"
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace flowboard;
namespace { void settle(int ms = 60) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

TEST_CASE("Bytes.Pack assembles fields at offsets; Bytes.Unpack reverses it") {
    auto pack = NodeRegistry::instance().create("Bytes.Pack", "pk", {
        {"endian", "little"},
        {"fields", {
            {{"name","a"},{"type","flowboard::UInt8"},{"offset",0}},
            {{"name","b"},{"type","flowboard::Int16"},{"offset",1}},
        }},
        {"autoTriggerOnNewInput", false},
    });
    REQUIRE(pack);

    std::shared_ptr<const ListValue> packed;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(pack->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v){ packed = v; });

    pack->start();
    auto* a = dynamic_cast<InputPort<std::uint8_t>*>(pack->input("a"));
    auto* b = dynamic_cast<InputPort<std::int16_t>*>(pack->input("b"));
    auto* trig = dynamic_cast<InputPort<bool>*>(pack->input("trigger"));
    REQUIRE(a); REQUIRE(b); REQUIRE(trig);

    a->deliver(std::make_shared<const std::uint8_t>(0xAB));
    b->deliver(std::make_shared<const std::int16_t>(-2));
    settle();
    { REQUIRE(packed == nullptr); }   // autoTrigger off -> no emit yet
    trig->deliver(std::make_shared<const bool>(true));
    settle();
    REQUIRE(packed != nullptr);
    REQUIRE(packed->items.size() == 3);          // 1 + 2 bytes
    CHECK(packed->items[0].get<int>() == 0xAB);
    CHECK(packed->items[1].get<int>() == 0xFE);  // -2 little-endian low byte
    CHECK(packed->items[2].get<int>() == 0xFF);

    auto unpack = NodeRegistry::instance().create("Bytes.Unpack", "up", {
        {"endian", "little"},
        {"fields", {
            {{"name","a"},{"type","flowboard::UInt8"},{"offset",0}},
            {{"name","b"},{"type","flowboard::Int16"},{"offset",1}},
        }},
    });
    REQUIRE(unpack);
    int gotA = -1; long long gotB = 0;
    auto* oa = dynamic_cast<OutputPort<std::uint8_t>*>(unpack->output("a"));
    auto* ob = dynamic_cast<OutputPort<std::int16_t>*>(unpack->output("b"));
    REQUIRE(oa); REQUIRE(ob);
    oa->attach_sink([&](auto v){ gotA = *v; });
    ob->attach_sink([&](auto v){ gotB = *v; });

    unpack->start();
    auto* in = dynamic_cast<InputPort<ListValue>*>(unpack->input("in"));
    REQUIRE(in);
    in->deliver(packed);
    settle();
    CHECK(gotA == 0xAB);
    CHECK(gotB == -2);

    pack->stop(); unpack->stop();
}

TEST_CASE("Bytes.Pack writes a bit-offset field (mixed with a byte field)") {
    nlohmann::json fields = {
        {{"name","count"},{"type","flowboard::UInt8"},{"offset",0}},
        {{"name","hi"},  {"type","flowboard::Bool"}, {"offset",1},{"bitOffset",4}},
    };
    auto pack = NodeRegistry::instance().create("Bytes.Pack", "pkbit", {
        {"endian","little"}, {"length",4}, {"fields", fields},
        {"autoTriggerOnNewInput", false},
    });
    REQUIRE(pack);
    std::shared_ptr<const ListValue> packed;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(pack->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v){ packed = v; });

    pack->start();
    auto* count = dynamic_cast<InputPort<std::uint8_t>*>(pack->input("count"));
    auto* hi    = dynamic_cast<InputPort<bool>*>(pack->input("hi"));
    auto* trig  = dynamic_cast<InputPort<bool>*>(pack->input("trigger"));
    REQUIRE(count); REQUIRE(hi); REQUIRE(trig);

    count->deliver(std::make_shared<const std::uint8_t>(0x05));
    hi->deliver(std::make_shared<const bool>(true));
    trig->deliver(std::make_shared<const bool>(true));
    settle();
    REQUIRE(packed != nullptr);
    REQUIRE(packed->items.size() == 4);
    CHECK(packed->items[0].get<int>() == 0x05);
    CHECK(packed->items[1].get<int>() == 0x10);  // Bool at bit 12 -> byte1 bit4
    CHECK(packed->items[2].get<int>() == 0x00);
    CHECK(packed->items[3].get<int>() == 0x00);
    pack->stop();
}

TEST_CASE("Bytes.Unpack reads a bit-offset field (round-trips Pack)") {
    nlohmann::json fields = {
        {{"name","count"},{"type","flowboard::UInt8"},{"offset",0}},
        {{"name","hi"},  {"type","flowboard::Bool"}, {"offset",1},{"bitOffset",4}},
    };
    // A buffer as Task 2 would produce: count=5 in byte0, hi=true at bit 12.
    auto lv = std::make_shared<ListValue>();
    lv->element_type_tag = "flowboard::UInt8";
    for (int b : {0x05, 0x10, 0x00, 0x00}) lv->items.push_back(::nlohmann::json(b));

    auto unpack = NodeRegistry::instance().create("Bytes.Unpack", "upbit", {
        {"endian","little"}, {"fields", fields},
    });
    REQUIRE(unpack);
    int gotCount = -1; bool gotHi = false;
    auto* oc = dynamic_cast<OutputPort<std::uint8_t>*>(unpack->output("count"));
    auto* oh = dynamic_cast<OutputPort<bool>*>(unpack->output("hi"));
    REQUIRE(oc); REQUIRE(oh);
    oc->attach_sink([&](auto v){ gotCount = *v; });
    oh->attach_sink([&](auto v){ gotHi = *v; });

    unpack->start();
    auto* in = dynamic_cast<InputPort<ListValue>*>(unpack->input("in"));
    REQUIRE(in);
    in->deliver(std::shared_ptr<const ListValue>(lv));
    settle();
    CHECK(gotCount == 0x05);
    CHECK(gotHi == true);
    unpack->stop();
}

TEST_CASE("Bytes.Pack skips a bit field that exceeds length") {
    auto pack = NodeRegistry::instance().create("Bytes.Pack", "pkskip", {
        {"endian","little"}, {"length",2},
        {"fields", {
            {{"name","big"},{"type","flowboard::UInt16"},{"offset",1},{"bitOffset",4}},
        }},
        {"autoTriggerOnNewInput", false},
    });
    REQUIRE(pack);
    std::shared_ptr<const ListValue> packed;
    auto* out = dynamic_cast<OutputPort<ListValue>*>(pack->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v){ packed = v; });
    pack->start();
    auto* big  = dynamic_cast<InputPort<std::uint16_t>*>(pack->input("big"));
    auto* trig = dynamic_cast<InputPort<bool>*>(pack->input("trigger"));
    REQUIRE(big); REQUIRE(trig);
    big->deliver(std::make_shared<const std::uint16_t>(0xFFFF));
    trig->deliver(std::make_shared<const bool>(true));
    settle();
    REQUIRE(packed != nullptr);
    REQUIRE(packed->items.size() == 2);   // need = ceil((12+16)/8) = 4 bytes > length 2 -> skipped
    CHECK(packed->items[0].get<int>() == 0x00);
    CHECK(packed->items[1].get<int>() == 0x00);
    pack->stop();
}
