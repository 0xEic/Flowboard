// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/port_factory_registry.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace flowboard::nodes {
namespace {

// Type-agnostic delay line: re-emits each received value `delayMs` after it
// arrived. Generic over any registered type tag via port_factory_registry —
// values round-trip through JSON between the input sink and the timed output,
// the same mechanism Python.Script / Factory.* / Bytes.Pack use. A single
// dedicated thread owns the timing and is the sole producer on `out` (the
// engine requires single-producer outputs, as in Sources.Timer).
class DelayNode final : public Node {
public:
    DelayNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Delay"),
          delay_ms_(cfg.value("delayMs", std::int64_t{1000})) {
        if (delay_ms_ < 0) delay_ms_ = 0;
        auto input_type = cfg.value("inputType", std::string{"flowboard::Double"});
        auto const* entry = lookup_port_factory(input_type);
        if (!entry)
            throw std::runtime_error("Transform.Delay: unsupported inputType '" + input_type + "'");
        emit_from_json_ = entry->emit_from_json;

        auto in = entry->make_input("in", [this](::nlohmann::json j) {
            auto due = std::chrono::steady_clock::now()
                     + std::chrono::milliseconds(delay_ms_);
            {
                std::scoped_lock lk(mu_);
                pending_.push_back(Pending{due, std::move(j)});
            }
            cv_.notify_all();
        });
        auto out = entry->make_output("out");
        out_ = out.get();
        register_input(in.get());
        register_output(out.get());
        in_storage_  = std::move(in);
        out_storage_ = std::move(out);
    }

    void on_start() override {
        worker_ = std::jthread([this](std::stop_token st) { loop(st); });
    }

    void on_stop() override {
        worker_.request_stop();
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    struct Pending {
        std::chrono::steady_clock::time_point due;
        ::nlohmann::json value;
    };

    void loop(std::stop_token st) {
        using namespace std::chrono;
        std::unique_lock lk(mu_);
        bool was_paused = false;
        steady_clock::time_point pause_start{};

        while (!st.stop_requested()) {
            bool paused = is_paused();
            // Freeze the countdown across a pause: shift every pending due-time
            // forward by the paused duration on resume so the remaining delay is
            // preserved and nothing is emitted while paused.
            if (paused && !was_paused) { pause_start = steady_clock::now(); was_paused = true; }
            if (!paused && was_paused) {
                auto d = steady_clock::now() - pause_start;
                for (auto& p : pending_) p.due += d;
                was_paused = false;
            }
            if (paused) {
                cv_.wait_for(lk, milliseconds(50), [&] { return st.stop_requested(); });
                continue;
            }
            if (pending_.empty()) {
                cv_.wait(lk, [&] { return st.stop_requested() || !pending_.empty(); });
                continue;
            }
            auto due = pending_.front().due;
            auto now = steady_clock::now();
            if (now >= due) {
                auto item = std::move(pending_.front());
                pending_.pop_front();
                lk.unlock();
                emit_from_json_(out_, item.value);   // sole producer on `out`
                lk.lock();
                continue;
            }
            // Wait until due or a wake (new push / stop). Cap the wait at 50 ms
            // so a pause that arrives mid-wait is observed promptly.
            auto wait = std::min(duration_cast<milliseconds>(due - now), milliseconds(50));
            cv_.wait_for(lk, wait, [&] { return st.stop_requested(); });
        }
        pending_.clear();  // drop pending on stop
    }

    std::int64_t                 delay_ms_;
    EmitFromJsonFn               emit_from_json_{nullptr};
    IOutputPort*                 out_{nullptr};
    std::unique_ptr<IInputPort>  in_storage_;
    std::unique_ptr<IOutputPort> out_storage_;

    std::mutex                   mu_;
    std::condition_variable      cv_;
    std::deque<Pending>          pending_;
    std::jthread                 worker_;
};

static auto _delay_factory =
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<DelayNode>(std::move(id), cfg);
    };

}  // namespace

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Delay",
    _delay_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object","title":"Delay",
      "description":"Re-emits each received value on 'out' a fixed time after it arrived. Works for any registered type; values are queued, so several arriving within one window are each delayed independently and emitted in order.",
      "required":["inputType","delayMs"],
      "properties":{
        "inputType":{"type":"string","title":"Type (in, out)","default":"flowboard::Double"},
        "delayMs":{"type":"integer","title":"Delay (ms)","minimum":0,"default":1000,
                   "description":"Milliseconds to hold each value before emitting it."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","delayMs":1000})JSON",
    delay
)

}  // namespace flowboard::nodes
