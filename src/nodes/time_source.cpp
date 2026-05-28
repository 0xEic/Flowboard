// SPDX-License-Identifier: MIT
//
// Sources.TimeSource — a settable clock.
//   Properties: baseIso / baseEpochMs (the time to start from), updateMs cadence.
//   Input  running (bool): while true the clock advances in real time; while
//                          false it is frozen at its current value.
//   Outputs epoch (int64 ms), iso (string, UTC), commonTime (M_Common::TimeType).
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"
#include "M_Common_types.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>

namespace flowboard::nodes {

namespace {
// Build epoch milliseconds (UTC) from the discrete date/time config fields.
std::int64_t epoch_ms_from_fields(::nlohmann::json const& cfg) {
    std::tm tm{};
    tm.tm_year  = cfg.value("year",  2026) - 1900;
    tm.tm_mon   = cfg.value("month", 1) - 1;
    tm.tm_mday  = cfg.value("day",   1);
    tm.tm_hour  = cfg.value("hour",  0);
    tm.tm_min   = cfg.value("minute", 0);
    tm.tm_sec   = cfg.value("second", 0);
    tm.tm_isdst = 0;
#if defined(_WIN32)
    std::time_t secs = _mkgmtime(&tm);
#else
    std::time_t secs = timegm(&tm);
#endif
    if (secs == static_cast<std::time_t>(-1)) return 0;
    std::int64_t ms = static_cast<std::int64_t>(cfg.value("millisecond", 0));
    return static_cast<std::int64_t>(secs) * 1000 + ms;
}

std::string format_iso(std::int64_t epoch_ms) {
    std::time_t secs = static_cast<std::time_t>(epoch_ms / 1000);
    int ms_part = static_cast<int>(((epoch_ms % 1000) + 1000) % 1000);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &secs);
#else
    gmtime_r(&secs, &utc);
#endif
    std::ostringstream os;
    os << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms_part << 'Z';
    return os.str();
}
}  // namespace

class TimeSourceNode : public Node {
public:
    TimeSourceNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Sources.TimeSource"),
          running_in_("running"),
          out_epoch_("epoch"), out_iso_("iso"), out_common_("commonTime"),
          update_ms_(cfg.value("updateMs", std::int64_t{1000})) {
        if (update_ms_ <= 0) update_ms_ = 1;
        base_ms_ = epoch_ms_from_fields(cfg);
        register_input(&running_in_);
        register_output(&out_epoch_);
        register_output(&out_iso_);
        register_output(&out_common_);
    }

    void on_start() override {
        running_in_.set_internal_sink([this](InputPort<bool>::Value v) {
            set_running(v && *v);
        });
        worker_ = std::jthread([this](std::stop_token st) { loop(st); });
    }
    void on_stop() override {
        worker_.request_stop();
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    void set_running(bool r) {
        {
            std::scoped_lock lk(mu_);
            if (running_ == r) return;
            if (r) {
                anchor_ = std::chrono::steady_clock::now();  // resume: keep base_ms_
            } else {
                base_ms_ = sim_now_ms_unlocked();            // freeze at current value
            }
            running_ = r;
            ++generation_;
        }
        cv_.notify_all();
    }

    std::int64_t sim_now_ms_unlocked() const {
        using namespace std::chrono;
        if (!running_) return base_ms_;
        auto elapsed = duration_cast<milliseconds>(steady_clock::now() - anchor_).count();
        return base_ms_ + static_cast<std::int64_t>(elapsed);
    }

    void emit(std::int64_t ms) {
        out_epoch_.emit(std::make_shared<const std::int64_t>(ms));
        out_iso_.emit(std::make_shared<const std::string>(format_iso(ms)));
        ::M_Common::TimeType t{};
        t.Seconds     = ms / 1000;
        t.Nanoseconds = static_cast<std::int32_t>((((ms % 1000) + 1000) % 1000) * 1000000);
        out_common_.emit(std::make_shared<const ::M_Common::TimeType>(t));
    }

    void loop(std::stop_token st) {
        using namespace std::chrono;
        std::unique_lock lk(mu_);
        emit_unlocked(lk);  // publish the base time once on start
        while (!st.stop_requested()) {
            if (!running_) {
                std::uint64_t gen = generation_;
                cv_.wait(lk, [&] { return st.stop_requested() || generation_ != gen; });
                if (st.stop_requested()) break;
                emit_unlocked(lk);  // toggled (started/frozen) → publish
                continue;
            }
            std::uint64_t gen = generation_;
            cv_.wait_for(lk, milliseconds(update_ms_),
                         [&] { return st.stop_requested() || generation_ != gen; });
            if (st.stop_requested()) break;
            emit_unlocked(lk);  // either cadence tick or a toggle
        }
    }
    void emit_unlocked(std::unique_lock<std::mutex>& lk) {
        std::int64_t ms = sim_now_ms_unlocked();
        lk.unlock();
        emit(ms);
        lk.lock();
    }

    InputPort<bool>                  running_in_;
    OutputPort<std::int64_t>         out_epoch_;
    OutputPort<std::string>          out_iso_;
    OutputPort<::M_Common::TimeType> out_common_;
    std::int64_t                     update_ms_;

    std::mutex               mu_;
    std::condition_variable  cv_;
    bool                     running_{false};
    std::int64_t             base_ms_{0};
    std::chrono::steady_clock::time_point anchor_{};
    std::uint64_t            generation_{0};
    std::jthread             worker_;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sources.TimeSource",
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<TimeSourceNode>(std::move(id), cfg);
    },
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "year":  {"type":"integer","title":"Year","default":2026},
        "month": {"type":"integer","title":"Month","minimum":1,"maximum":12,"default":1},
        "day":   {"type":"integer","title":"Day","minimum":1,"maximum":31,"default":1},
        "hour":  {"type":"integer","title":"Hour","minimum":0,"maximum":23,"default":0},
        "minute":{"type":"integer","title":"Minute","minimum":0,"maximum":59,"default":0},
        "second":{"type":"integer","title":"Second","minimum":0,"maximum":59,"default":0},
        "millisecond":{"type":"integer","title":"Millisecond","minimum":0,"maximum":999,"default":0},
        "updateMs":{"type":"integer","title":"Update interval (ms)","minimum":1,"default":1000,
                    "description":"How often the outputs refresh while running (UTC base time set above)."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"year":2026,"month":1,"day":1,"hour":0,"minute":0,"second":0,"millisecond":0,"updateMs":1000})JSON",
    timesource
)

}  // namespace flowboard::nodes
