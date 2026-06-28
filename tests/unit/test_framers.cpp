// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/list_value.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace flowboard;

namespace {
std::shared_ptr<const ListValue> make_list(std::vector<int> const& bytes) {
    auto lv = std::make_shared<ListValue>();
    lv->element_type_tag = "flowboard::UInt8";
    for (int b : bytes) lv->items.push_back(b & 0xFF);
    return lv;
}

std::vector<std::vector<std::uint8_t>>
run_framer(std::unique_ptr<Node> n, std::vector<std::vector<int>> const& chunks) {
    std::vector<std::vector<std::uint8_t>> out;
    std::mutex mu;
    auto* in  = dynamic_cast<InputPort<ListValue>*>(n->input("in"));
    auto* op  = dynamic_cast<OutputPort<ListValue>*>(n->output("out"));
    REQUIRE(in); REQUIRE(op);
    op->attach_sink([&](auto v) {
        std::vector<std::uint8_t> bytes;
        for (auto const& j : v->items) bytes.push_back(static_cast<std::uint8_t>(j.get<int>() & 0xFF));
        std::scoped_lock lk(mu); out.push_back(std::move(bytes));
    });
    n->start();
    for (auto const& c : chunks) in->deliver(make_list(c));
    for (int i = 0; i < 50; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    n->stop();
    return out;
}
}

TEST_CASE("Framer.Delimiter splits on \\n") {
    auto n = NodeRegistry::instance().create("Framer.Delimiter", "d1",
        {{"delimiter", "\n"}, {"includeDelimiter", false}, {"maxFrameSize", 256}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{'a','b','\n','c','d','\n','e'}});
    REQUIRE(frames.size() == 2u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "ab");
    CHECK(std::string(frames[1].begin(), frames[1].end()) == "cd");
}

TEST_CASE("Framer.Delimiter joins across chunks") {
    auto n = NodeRegistry::instance().create("Framer.Delimiter", "d2",
        {{"delimiter", "\n"}, {"includeDelimiter", false}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n),
        {{'h','e'}, {'l','l','o','\n','w','o'}, {'r','l','d','\n'}});
    REQUIRE(frames.size() == 2u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "hello");
    CHECK(std::string(frames[1].begin(), frames[1].end()) == "world");
}

TEST_CASE("Framer.Delimiter respects includeDelimiter") {
    auto n = NodeRegistry::instance().create("Framer.Delimiter", "d3",
        {{"delimiter", "\n"}, {"includeDelimiter", true}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{'x','\n'}});
    REQUIRE(frames.size() == 1u);
    CHECK(frames[0] == std::vector<std::uint8_t>{'x', '\n'});
}

TEST_CASE("Framer.Delimiter drops oversize frames") {
    auto n = NodeRegistry::instance().create("Framer.Delimiter", "d4",
        {{"delimiter", "\n"}, {"maxFrameSize", 3}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{'a','b','c','d','e','f','\n'}});
    CHECK(frames.empty());
}

TEST_CASE("Framer.LengthPrefix 1-byte BE includes prefix in length") {
    auto n = NodeRegistry::instance().create("Framer.LengthPrefix", "lp1",
        {{"prefixSize", 1}, {"littleEndian", false}, {"lengthIncludesPrefix", true}});
    REQUIRE(n);
    // Frame: total length = 4 -> prefix byte 0x04, body 'a','b','c'
    auto frames = run_framer(std::move(n), {{0x04, 'a','b','c', 0x02, 'd'}});
    REQUIRE(frames.size() == 2u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "abc");
    CHECK(std::string(frames[1].begin(), frames[1].end()) == "d");
}

TEST_CASE("Framer.LengthPrefix 2-byte LE body-length") {
    auto n = NodeRegistry::instance().create("Framer.LengthPrefix", "lp2",
        {{"prefixSize", 2}, {"littleEndian", true}, {"lengthIncludesPrefix", false}});
    REQUIRE(n);
    // Body length 3 (LE 0x03 0x00), then 'X','Y','Z'.
    auto frames = run_framer(std::move(n), {{0x03, 0x00, 'X','Y','Z'}});
    REQUIRE(frames.size() == 1u);
    CHECK(frames[0] == std::vector<std::uint8_t>{'X','Y','Z'});
}

TEST_CASE("Framer.LengthPrefix waits for full body across chunks") {
    auto n = NodeRegistry::instance().create("Framer.LengthPrefix", "lp3",
        {{"prefixSize", 1}, {"littleEndian", true}, {"lengthIncludesPrefix", false}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{0x05}, {'a'}, {'b','c'}, {'d','e'}});
    REQUIRE(frames.size() == 1u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "abcde");
}

TEST_CASE("Framer.Fixed emits every N bytes") {
    auto n = NodeRegistry::instance().create("Framer.Fixed", "f1",
        {{"frameSize", 3}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{'a','b','c','d','e','f','g'}});
    REQUIRE(frames.size() == 2u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "abc");
    CHECK(std::string(frames[1].begin(), frames[1].end()) == "def");
}

TEST_CASE("Framer.Fixed joins across chunks") {
    auto n = NodeRegistry::instance().create("Framer.Fixed", "f2",
        {{"frameSize", 4}});
    REQUIRE(n);
    auto frames = run_framer(std::move(n), {{'a','b'}, {'c','d','e','f','g','h'}});
    REQUIRE(frames.size() == 2u);
    CHECK(std::string(frames[0].begin(), frames[0].end()) == "abcd");
    CHECK(std::string(frames[1].begin(), frames[1].end()) == "efgh");
}
