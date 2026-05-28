// SPDX-License-Identifier: MIT
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include "flowboard/graph.hpp"
#include "flowboard/node.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/loader.hpp"
#include "builtin_types.hpp"

using namespace flowboard;
using namespace std::chrono_literals;

class BenchSource : public Node {
public:
    BenchSource(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Bench.Source"), out_("out") { register_output(&out_); }

    void emit_n(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            out_.emit(std::make_shared<const int64_t>(static_cast<int64_t>(i)));
    }

private:
    OutputPort<int64_t> out_;
};
OP_REGISTER_NODE("Bench.Source", BenchSource);

// Threshold emits bool; counter receives bool.
class BenchCounter : public Node {
public:
    BenchCounter(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Bench.Counter"), in_("in") { register_input(&in_); }
    void on_start() override {
        in_.set_internal_sink([this](auto) {
            enqueue([this] { count_.fetch_add(1, std::memory_order_release); });
        });
    }
    std::size_t count() const { return count_.load(std::memory_order_acquire); }

private:
    InputPort<bool> in_;
    std::atomic<std::size_t> count_{0};
};
OP_REGISTER_NODE("Bench.Counter", BenchCounter);

int main() {
    constexpr std::size_t N = 200'000;
    Graph g;
    auto& reg = NodeRegistry::instance();
    g.add_node(reg.create("Bench.Source",  "src", {}));
    g.add_node(reg.create("Transform.Threshold", "t", nlohmann::json{
        {"inputType", "flowboard::Int64"}, {"op", ">"}, {"value", 0}}));
    g.add_node(reg.create("Bench.Counter", "snk", {}));
    // src(int64_t) -> t(int64_t in / bool out) -> snk(bool in)
    g.connect("src", "out", "t",   "in",  N + 1, BackpressurePolicy::Block);
    g.connect("t",   "out", "snk", "in",  N + 1, BackpressurePolicy::Block);
    g.start();

    auto* src = static_cast<BenchSource*>(g.node("src"));
    auto* snk = static_cast<BenchCounter*>(g.node("snk"));

    auto t0 = std::chrono::steady_clock::now();
    src->emit_n(N);
    while (snk->count() < N && std::chrono::steady_clock::now() - t0 < 30s)
        std::this_thread::sleep_for(1ms);

    auto t1 = std::chrono::steady_clock::now();
    g.stop();

    auto secs = std::chrono::duration<double>(t1 - t0).count();
    double rate = static_cast<double>(N) / secs;
    std::cout << "processed " << N << " msgs in " << secs << " s => "
              << rate << " msg/s\n";
    return rate >= 100'000.0 ? 0 : 1;
}
