// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include "flowboard/graph.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/node.hpp"
#include "builtin_types.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace flowboard;

class SourceNode : public Node {
public:
    SourceNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.Source"), out_("out") {
        register_output(&out_);
    }
    void emit(int64_t v) { out_.emit(std::make_shared<const int64_t>(v)); }
private:
    OutputPort<int64_t> out_;
};
OP_REGISTER_NODE("Test.Source", SourceNode);

class SinkNode : public Node {
public:
    SinkNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.Sink"), in_("in") {
        register_input(&in_);
    }
    void on_start() override {
        in_.set_internal_sink([this](auto v) {
            enqueue([this, v] { received_.store(*v); });
        });
    }
    std::atomic<int64_t> received_{0};
private:
    InputPort<int64_t> in_;
};
OP_REGISTER_NODE("Test.Sink", SinkNode);

TEST_CASE("Graph connects two nodes and propagates a value") {
    Graph g;
    auto& reg = NodeRegistry::instance();
    auto src_uptr  = reg.create("Test.Source", "src",  {});
    auto sink_uptr = reg.create("Test.Sink",   "sink", {});
    auto* src  = static_cast<SourceNode*>(src_uptr.get());
    auto* sink = static_cast<SinkNode*>(sink_uptr.get());
    g.add_node(std::move(src_uptr));
    g.add_node(std::move(sink_uptr));

    g.connect("src", "out", "sink", "in",
              /*capacity=*/16, BackpressurePolicy::Block);

    g.start();
    src->emit(123);

    for (int i = 0; i < 100 && sink->received_.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(sink->received_.load() == 123);

    g.stop();
}

TEST_CASE("Graph rejects connect with mismatched type tags") {
    Graph g;
    auto& reg = NodeRegistry::instance();
    g.add_node(reg.create("Test.Source", "s",  {}));
    g.add_node(reg.create("Test.Sink",   "sk", {}));
    auto err = g.connect_checked("s", "out", "sk", "in_does_not_exist",
                                 16, BackpressurePolicy::Block);
    CHECK(!err.empty());
}

class BoolSinkNode : public Node {
public:
    BoolSinkNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.BoolSink"), in_("in") { register_input(&in_); }
    void on_start() override {
        in_.set_internal_sink([this](auto v) { enqueue([this, v] {
            ++count_; last_.store(v && *v);
        }); });
    }
    std::atomic<int>  count_{0};
    std::atomic<bool> last_{false};
private:
    InputPort<bool> in_;
};
OP_REGISTER_NODE("Test.BoolSink", BoolSinkNode);

class StringSinkNode : public Node {
public:
    StringSinkNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Test.StringSink"), in_("in") { register_input(&in_); }
private:
    InputPort<std::string> in_;
};
OP_REGISTER_NODE("Test.StringSink", StringSinkNode);

TEST_CASE("Graph allows a signal edge (any-typed output -> Bool input) and pulses true") {
    Graph g;
    auto& reg = NodeRegistry::instance();
    auto src_uptr  = reg.create("Test.Source",   "src",  {});
    auto sink_uptr = reg.create("Test.BoolSink",  "sig",  {});
    auto* src  = static_cast<SourceNode*>(src_uptr.get());
    auto* sink = static_cast<BoolSinkNode*>(sink_uptr.get());
    g.add_node(std::move(src_uptr));
    g.add_node(std::move(sink_uptr));

    // Int64 output -> Bool input is a legit signal edge (no type-mismatch error).
    CHECK(g.connect_checked("src", "out", "sig", "in", 16, BackpressurePolicy::Block).empty());
    g.connect("src", "out", "sig", "in", 16, BackpressurePolicy::Block);

    g.start();
    src->emit(0);   // value is 0, but the signal must still pulse `true`
    for (int i = 0; i < 100 && sink->count_.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(sink->count_.load() >= 1);
    CHECK(sink->last_.load() == true);
    g.stop();
}

TEST_CASE("Graph still rejects a real (non-Bool) type mismatch") {
    Graph g;
    auto& reg = NodeRegistry::instance();
    g.add_node(reg.create("Test.Source",     "s",  {}));
    g.add_node(reg.create("Test.StringSink", "ss", {}));
    auto err = g.connect_checked("s", "out", "ss", "in", 16, BackpressurePolicy::Block);
    CHECK(!err.empty());  // Int64 -> String is not a signal edge
}
