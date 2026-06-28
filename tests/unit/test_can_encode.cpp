// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/can_frame.hpp"
#include "builtin_types.hpp"
#include <chrono>
#include <cstdint>
#include <thread>

using namespace flowboard;
namespace { void settle(int ms = 60) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); } }

TEST_CASE("Can.Encode builds a frame; Can.Decode reverses it") {
    auto enc = NodeRegistry::instance().create("Can.Encode", "enc", {{"autoTriggerOnNewInput", false}});
    REQUIRE(enc);
    std::shared_ptr<const CanFrame> frame;
    auto* out = dynamic_cast<OutputPort<CanFrame>*>(enc->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v){ frame = v; });

    enc->start();
    auto* pid   = dynamic_cast<InputPort<std::uint32_t>*>(enc->input("id"));
    auto* pdata = dynamic_cast<InputPort<ListValue>*>(enc->input("data"));
    auto* pext  = dynamic_cast<InputPort<bool>*>(enc->input("isExtended"));
    auto* ptrig = dynamic_cast<InputPort<bool>*>(enc->input("trigger"));
    REQUIRE(pid); REQUIRE(pdata); REQUIRE(pext); REQUIRE(ptrig);

    pid->deliver(std::make_shared<const std::uint32_t>(0x123));
    pext->deliver(std::make_shared<const bool>(true));
    auto lv = std::make_shared<ListValue>();
    lv->element_type_tag = "flowboard::UInt8";
    lv->items = {1, 2, 3};
    pdata->deliver(lv);
    settle();
    REQUIRE(frame == nullptr);   // autoTrigger off → no emit yet

    ptrig->deliver(std::make_shared<const bool>(true));
    settle();
    REQUIRE(frame != nullptr);
    CHECK(frame->id == 0x123u);
    CHECK(frame->is_extended == true);
    CHECK(frame->dlc == 3);
    REQUIRE(frame->data.size() == 3);
    CHECK(frame->data[0] == 1); CHECK(frame->data[1] == 2); CHECK(frame->data[2] == 3);

    auto dec = NodeRegistry::instance().create("Can.Decode", "dec", {});
    REQUIRE(dec);
    std::uint32_t gotId = 0; bool gotExt = false; int gotDlc = -1;
    std::shared_ptr<const ListValue> gotData;
    dynamic_cast<OutputPort<std::uint32_t>*>(dec->output("id"))->attach_sink([&](auto v){ gotId = *v; });
    dynamic_cast<OutputPort<bool>*>(dec->output("isExtended"))->attach_sink([&](auto v){ gotExt = *v; });
    dynamic_cast<OutputPort<std::uint8_t>*>(dec->output("dlc"))->attach_sink([&](auto v){ gotDlc = *v; });
    dynamic_cast<OutputPort<ListValue>*>(dec->output("data"))->attach_sink([&](auto v){ gotData = v; });

    dec->start();
    dynamic_cast<InputPort<CanFrame>*>(dec->input("frame"))->deliver(frame);
    settle();
    CHECK(gotId == 0x123u);
    CHECK(gotExt == true);
    CHECK(gotDlc == 3);
    REQUIRE(gotData != nullptr);
    REQUIRE(gotData->items.size() == 3);
    CHECK(gotData->items[2].get<int>() == 3);

    enc->stop(); dec->stop();
}
