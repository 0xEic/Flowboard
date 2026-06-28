// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/can_adapter.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace flowboard;

TEST_CASE("Virtual CAN adapter is registered as 'virtual'") {
    CHECK(find_can_adapter("virtual") != nullptr);
}

TEST_CASE("Virtual CAN adapter loops frames between buses on the same name") {
    auto* adapter = find_can_adapter("virtual");
    REQUIRE(adapter != nullptr);

    std::vector<CanFrame> rx_a, rx_b;
    std::mutex mu;
    auto push_to = [&mu](std::vector<CanFrame>& sink) {
        return CanRxCallback{
            [&mu, &sink](CanFrame const& f) { std::scoped_lock lk(mu); sink.push_back(f); },
            [](std::string_view){}};
    };

    auto bus_a = adapter->open({"loop", 500000, false, false, 0}, push_to(rx_a));
    auto bus_b = adapter->open({"loop", 500000, false, false, 0}, push_to(rx_b));
    REQUIRE(bus_a);
    REQUIRE(bus_b);

    CanFrame f;
    f.id = 0x100; f.dlc = 2; f.data = {0xAA, 0xBB};
    REQUIRE(bus_a->send(f));

    for (int i = 0; i < 50 && rx_b.empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    {   std::scoped_lock lk(mu);
        CHECK(rx_a.empty());                 // sender does not receive its own frame
        REQUIRE(rx_b.size() == 1u);
        CHECK(rx_b.front().id == 0x100u);
        CHECK(rx_b.front().data == std::vector<std::uint8_t>{0xAA, 0xBB});
    }
}
