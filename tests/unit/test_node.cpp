// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

// Minimal test node: doubles incoming int64 values to its output.
class DoublerNode : public Node {
public:
    DoublerNode() : Node("doubler", "Test.Doubler"), in_("in"), out_("out") {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](auto v) { enqueue([this, v] { handle(v); }); });
    }

private:
    void handle(std::shared_ptr<const int64_t> v) {
        out_.emit(std::make_shared<const int64_t>(*v * 2));
    }
    InputPort<int64_t> in_;
    OutputPort<int64_t> out_;
};

TEST_CASE("Node processes a queued event") {
    DoublerNode node;
    std::atomic<int64_t> seen{0};
    auto out = dynamic_cast<OutputPort<int64_t>*>(node.output("out"));
    REQUIRE(out != nullptr);
    out->attach_sink([&](std::shared_ptr<const int64_t> v) { seen.store(*v); });

    node.start();
    auto in = dynamic_cast<InputPort<int64_t>*>(node.input("in"));
    REQUIRE(in != nullptr);
    in->deliver(std::make_shared<const int64_t>(21));

    // Wait up to 1s for processing.
    for (int i = 0; i < 100 && seen.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(seen.load() == 42);

    node.stop();
}
