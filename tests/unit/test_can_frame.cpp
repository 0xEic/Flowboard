// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/can_frame.hpp"
#include <nlohmann/json.hpp>
#include "flowboard/wire_registry.hpp"
#include "flowboard/tap_registry.hpp"
#include "flowboard/port_factory_registry.hpp"

using namespace flowboard;

TEST_CASE("CanFrame round-trips through JSON") {
    CanFrame f;
    f.id           = 0x123;
    f.is_extended  = false;
    f.is_remote    = false;
    f.is_fd        = false;
    f.is_brs       = false;
    f.is_esi       = false;
    f.dlc          = 4;
    f.data         = {0xDE, 0xAD, 0xBE, 0xEF};
    f.timestamp_ns = 1700000000123456789ULL;

    nlohmann::json j = to_json(f);
    CHECK(j.at("id").get<std::uint32_t>() == 0x123u);
    CHECK(j.at("isExtended").get<bool>() == false);
    CHECK(j.at("dlc").get<int>() == 4);
    CHECK(j.at("data").size() == 4u);
    CHECK(j.at("data")[0].get<int>() == 0xDE);
    CHECK(j.at("timestampNs").get<std::uint64_t>() == 1700000000123456789ULL);

    CanFrame back;
    from_json(j, back);
    CHECK(back.id           == f.id);
    CHECK(back.is_extended  == f.is_extended);
    CHECK(back.dlc          == f.dlc);
    CHECK(back.data         == f.data);
    CHECK(back.timestamp_ns == f.timestamp_ns);
}

TEST_CASE("CanFrame from_json tolerates missing optional fields") {
    nlohmann::json j = {{"id", 0x7FF}, {"dlc", 0}, {"data", nlohmann::json::array()}};
    CanFrame f;
    from_json(j, f);
    CHECK(f.id == 0x7FFu);
    CHECK(f.is_extended == false);   // default
    CHECK(f.is_remote   == false);
    CHECK(f.is_fd       == false);
    CHECK(f.data.empty());
    CHECK(f.timestamp_ns == 0u);
}

TEST_CASE("CanFrame is registered with the wire / tap / port factories") {
    CHECK(::flowboard::lookup_wire_factory("flowboard::CanFrame") != nullptr);
    CHECK(::flowboard::lookup_tap_factory("flowboard::CanFrame")  != nullptr);
    CHECK(::flowboard::lookup_port_factory("flowboard::CanFrame") != nullptr);
}
