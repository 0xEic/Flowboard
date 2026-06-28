// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/can_frame.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

using namespace flowboard;

namespace {
CanFrame make(std::uint32_t id) { CanFrame f; f.id = id; f.dlc = 0; return f; }

std::unique_ptr<Node> make_filter(nlohmann::json cfg) {
    return NodeRegistry::instance().create("Can.Filter", "f1", cfg);
}

void deliver_and_collect(Node& n, std::vector<CanFrame> const& ins,
                          std::vector<CanFrame>& outs) {
    auto* in  = dynamic_cast<InputPort<CanFrame>*>(n.input("in"));
    auto* out = dynamic_cast<OutputPort<CanFrame>*>(n.output("out"));
    REQUIRE(in);
    REQUIRE(out);
    std::mutex mu;
    out->attach_sink([&](auto v) { std::scoped_lock lk(mu); outs.push_back(*v); });
    n.start();
    for (auto const& f : ins) in->deliver(std::make_shared<const CanFrame>(f));
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::scoped_lock lk(mu);
        if (outs.size() >= ins.size()) break;
    }
    n.stop();
}
}

TEST_CASE("Can.Filter acceptAll passes everything") {
    auto n = make_filter({{"mode", "acceptAll"}});
    REQUIRE(n);
    std::vector<CanFrame> outs;
    deliver_and_collect(*n, {make(0x100), make(0x200), make(0x300)}, outs);
    CHECK(outs.size() == 3u);
}

TEST_CASE("Can.Filter acceptIds keeps only listed IDs") {
    auto n = make_filter({{"mode", "acceptIds"}, {"ids", {0x100, 0x300}}});
    REQUIRE(n);
    std::vector<CanFrame> outs;
    deliver_and_collect(*n, {make(0x100), make(0x200), make(0x300)}, outs);
    REQUIRE(outs.size() == 2u);
    CHECK(outs[0].id == 0x100u);
    CHECK(outs[1].id == 0x300u);
}

TEST_CASE("Can.Filter rejectIds drops only listed IDs") {
    auto n = make_filter({{"mode", "rejectIds"}, {"ids", {0x200}}});
    REQUIRE(n);
    std::vector<CanFrame> outs;
    deliver_and_collect(*n, {make(0x100), make(0x200), make(0x300)}, outs);
    REQUIRE(outs.size() == 2u);
    CHECK(outs[0].id == 0x100u);
    CHECK(outs[1].id == 0x300u);
}

TEST_CASE("Can.Filter idRange keeps inclusive range") {
    auto n = make_filter({{"mode", "idRange"}, {"idMin", 0x100}, {"idMax", 0x200}});
    REQUIRE(n);
    std::vector<CanFrame> outs;
    deliver_and_collect(*n, {make(0xFF), make(0x100), make(0x150), make(0x200), make(0x300)}, outs);
    REQUIRE(outs.size() == 3u);
    CHECK(outs[0].id == 0x100u);
    CHECK(outs[2].id == 0x200u);
}

TEST_CASE("Can.Filter mask keeps frames where (id & idMask) == idMatch") {
    auto n = make_filter({{"mode", "mask"}, {"idMask", 0x700}, {"idMatch", 0x100}});
    REQUIRE(n);
    std::vector<CanFrame> outs;
    deliver_and_collect(*n,
        {make(0x100), make(0x110), make(0x200), make(0x101), make(0x1FF)}, outs);
    // 0x100, 0x110, 0x101, 0x1FF all match (0x?XX & 0x700 == 0x100). 0x200 fails.
    REQUIRE(outs.size() == 4u);
}
