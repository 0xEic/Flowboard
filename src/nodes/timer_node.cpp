// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace flowboard::nodes {

// Periodic tick emitter with run-state control and interval progress.
//
// Outputs:
//   tick      (bool)  — emits `true` every intervalMs while running.
//   isRunning (bool)  — emits the run state whenever it changes.
//   timeLeft  (int64) — ms remaining until the next tick (progress cadence).
//   timePast  (int64) — ms elapsed in the current interval (progress cadence).
// Inputs:
//   start (bool) — a true value starts the timer (if stopped).
//   reset (bool) — a true value restarts the current interval from now.
// Config: intervalMs (1000), repeat (true), autostart (true).
//
// A single dedicated thread owns the cadence and is the sole producer on every
// output port (the engine's edges are single-producer). It wakes at a progress
// cadence (<=100 ms) to publish timeLeft/timePast and fires tick at the interval
// boundary. start/reset/stop arrive via a condition variable.
class TimerNode : public Node {
public:
    TimerNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Sources.Timer"),
          tick_("tick"), is_running_("isRunning"),
          time_left_("timeLeft"), time_past_("timePast"),
          start_("start"), reset_("reset"),
          interval_ms_(cfg.value("intervalMs", std::int64_t{1000})),
          repeat_(cfg.value("repeat", true)),
          autostart_(cfg.value("autostart", true)) {
        if (interval_ms_ <= 0) interval_ms_ = 1;
        register_input(&start_);
        register_input(&reset_);
        register_output(&tick_);
        register_output(&is_running_);
        register_output(&time_left_);
        register_output(&time_past_);
    }

    void on_start() override {
        start_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) request_running(true);
        });
        reset_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) bump_generation();
        });
        { std::scoped_lock lk(mu_); running_ = autostart_; }
        worker_ = std::jthread([this](std::stop_token st) { loop(st); });
    }

    void on_stop() override {
        worker_.request_stop();
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    void request_running(bool r) {
        { std::scoped_lock lk(mu_); if (running_ != r) { running_ = r; ++generation_; } }
        cv_.notify_all();
    }
    void bump_generation() {
        { std::scoped_lock lk(mu_); ++generation_; }
        cv_.notify_all();
    }

    void emit_bool(OutputPort<bool>& p, bool v) { p.emit(std::make_shared<const bool>(v)); }
    void emit_i64(OutputPort<std::int64_t>& p, std::int64_t v) {
        p.emit(std::make_shared<const std::int64_t>(v));
    }
    void emit_progress(std::int64_t past) {
        std::int64_t left = std::max<std::int64_t>(0, interval_ms_ - past);
        emit_i64(time_past_, past);
        emit_i64(time_left_, left);
    }

    void loop(std::stop_token st) {
        using namespace std::chrono;
        auto const progress = milliseconds(std::min<std::int64_t>(interval_ms_, 100));
        std::unique_lock lk(mu_);
        bool last_running = false;
        bool emitted_once = false;
        std::uint64_t gen = generation_;
        steady_clock::time_point start_tp{};

        while (!st.stop_requested()) {
            // Publish run-state transitions (sole emitter of isRunning).
            if (!emitted_once || running_ != last_running) {
                last_running = running_;
                emitted_once = true;
                bool r = running_;
                gen = generation_;
                start_tp = steady_clock::now();
                lk.unlock();
                emit_bool(is_running_, r);
                if (r) emit_progress(0);
                lk.lock();
                continue;
            }
            if (!running_) {
                cv_.wait(lk, [&] { return running_ != last_running || st.stop_requested(); });
                continue;
            }

            auto interval_end = start_tp + milliseconds(interval_ms_);
            auto wake = std::min(interval_end, steady_clock::now() + progress);
            cv_.wait_until(lk, wake, [&] {
                return st.stop_requested() || running_ != last_running || generation_ != gen;
            });
            if (st.stop_requested()) break;
            if (running_ != last_running) continue;       // -> emits isRunning next iter
            if (generation_ != gen) {                      // reset: restart interval
                gen = generation_;
                start_tp = steady_clock::now();
                lk.unlock();
                emit_progress(0);
                lk.lock();
                continue;
            }

            auto now = steady_clock::now();
            if (now >= interval_end) {
                lk.unlock();
                emit_bool(tick_, true);
                emit_progress(0);
                lk.lock();
                start_tp = interval_end;  // anchor to boundary to avoid drift
                if (!repeat_) { running_ = false; ++generation_; }  // one-shot: stop
            } else {
                std::int64_t past = duration_cast<milliseconds>(now - start_tp).count();
                lk.unlock();
                emit_progress(past);
                lk.lock();
            }
        }
    }

    OutputPort<bool>         tick_;
    OutputPort<bool>         is_running_;
    OutputPort<std::int64_t> time_left_;
    OutputPort<std::int64_t> time_past_;
    InputPort<bool>          start_;
    InputPort<bool>          reset_;
    std::int64_t             interval_ms_;
    bool                     repeat_;
    bool                     autostart_;

    std::mutex               mu_;
    std::condition_variable  cv_;
    bool                     running_{false};
    std::uint64_t            generation_{0};
    std::jthread             worker_;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sources.Timer",
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<TimerNode>(std::move(id), cfg);
    },
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "intervalMs":{"type":"integer","title":"Interval (ms)","minimum":1,"default":1000,
                      "description":"Milliseconds between successive tick emissions."},
        "repeat":{"type":"boolean","title":"Repeat","default":true,
                  "description":"When false the timer fires once then stops (one-shot)."},
        "autostart":{"type":"boolean","title":"Auto-start","default":true,
                     "description":"Start running as soon as the graph starts. When false, wait for the start input."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"intervalMs":1000,"repeat":true,"autostart":true})JSON",
    timer
)

}  // namespace flowboard::nodes
