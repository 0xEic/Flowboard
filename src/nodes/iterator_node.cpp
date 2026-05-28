// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <type_traits>

namespace flowboard::nodes {
namespace {

constexpr double kPi = 3.14159265358979323846;

// 10 easing "characters". Each is defined as an ease-IN shape (slow start);
// the ease-OUT variant is its mirror. Used to reshape the linear sweep fraction.
enum class Anim { Linear, Quad, Cubic, Quint, Sine, Expo, Circ, Back, Elastic, Bounce };

Anim parse_anim(std::string const& s) {
    if (s == "quad")    return Anim::Quad;
    if (s == "cubic")   return Anim::Cubic;
    if (s == "quint")   return Anim::Quint;
    if (s == "sine")    return Anim::Sine;
    if (s == "expo")    return Anim::Expo;
    if (s == "circ")    return Anim::Circ;
    if (s == "back")    return Anim::Back;
    if (s == "elastic") return Anim::Elastic;
    if (s == "bounce")  return Anim::Bounce;
    return Anim::Linear;
}

double ease_out_bounce(double x) {
    const double n1 = 7.5625, d1 = 2.75;
    if (x < 1.0 / d1)       return n1 * x * x;
    if (x < 2.0 / d1)       { x -= 1.5 / d1;   return n1 * x * x + 0.75; }
    if (x < 2.5 / d1)       { x -= 2.25 / d1;  return n1 * x * x + 0.9375; }
    /* else */              { x -= 2.625 / d1; return n1 * x * x + 0.984375; }
}

double ease_in(Anim a, double x) {
    switch (a) {
        case Anim::Linear: return x;
        case Anim::Quad:   return x * x;
        case Anim::Cubic:  return x * x * x;
        case Anim::Quint:  return x * x * x * x * x;
        case Anim::Sine:   return 1.0 - std::cos((x * kPi) / 2.0);
        case Anim::Expo:   return x <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * x - 10.0);
        case Anim::Circ:   return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x));
        case Anim::Back:   { const double c1 = 1.70158, c3 = c1 + 1.0; return c3 * x * x * x - c1 * x * x; }
        case Anim::Elastic: {
            if (x <= 0.0) return 0.0;
            if (x >= 1.0) return 1.0;
            const double c4 = (2.0 * kPi) / 3.0;
            return -std::pow(2.0, 10.0 * x - 10.0) * std::sin((x * 10.0 - 10.75) * c4);
        }
        case Anim::Bounce: return 1.0 - ease_out_bounce(1.0 - x);
    }
    return x;
}
double ease_out(Anim a, double x) { return 1.0 - ease_in(a, 1.0 - x); }

// Custom ease-in-out: `in` shapes the first half (near the start value), `out`
// the second half (near the end value). The two halves can use different types.
double combined_ease(double t, Anim in, Anim out) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    if (t < 0.5)  return 0.5 * ease_in(in, t * 2.0);
    return 0.5 + 0.5 * ease_out(out, t * 2.0 - 1.0);
}

// Steps a `value` from startValue to endValue over time, controllable via
// start/stop/pause/reset triggers, with loop and auto-reverse (sweep back and
// forth) modes and per-direction easing. A dedicated worker thread owns the
// cadence and is the sole producer on every output port (engine edges are
// single-producer); triggers/settings arrive via a condition variable.
//
// The number of steps N = round(|end-start| / stepSize); each step advances a
// counter and waits timeBetweenStepsMs. The emitted value is
// start + ease(k/N) * (end-start), where the easing is chosen per direction:
//   increasing → specialStart{Start,End}Value, decreasing → specialEnd{Start,End}Value
//   (*StartValue shapes the curve near the start value, *EndValue near the end).
// Templated on the value type T (any primitive number): the startValue,
// endValue, stepSize input ports and the `value` output port are all T, while
// the sweep/easing math runs in double internally (cast to T on emit).
template <typename T>
class IteratorT : public Node {
public:
    IteratorT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Sources.Iterator"),
          value_out_("value"), running_out_("running"), end_out_("endReached"),
          started_out_("started"), inc_out_("increasing"), dec_out_("decreasing"),
          in_start_value_("startValue"), in_end_value_("endValue"),
          in_step_size_("stepSize"), in_time_between_("timeBetweenStepsMs"),
          in_loop_("loop"), in_auto_reverse_("autoReverse"),
          trig_start_("start"), trig_stop_("stop"), trig_pause_("pause"), trig_reset_("reset"),
          start_(cfg.value("startValue", 0.0)),
          end_(cfg.value("endValue", 1.0)),
          step_(cfg.value("stepSize", 0.1)),
          time_between_(cfg.value("timeBetweenStepsMs", std::int64_t{100})),
          loop_(cfg.value("loop", false)),
          auto_reverse_(cfg.value("autoReverse", false)),
          autostart_(cfg.value("autostart", false)),
          anim_rise_in_(parse_anim(cfg.value("specialStartStartValue", std::string{"linear"}))),
          anim_rise_out_(parse_anim(cfg.value("specialStartEndValue", std::string{"linear"}))),
          anim_fall_in_(parse_anim(cfg.value("specialEndStartValue", std::string{"linear"}))),
          anim_fall_out_(parse_anim(cfg.value("specialEndEndValue", std::string{"linear"}))) {
        if (time_between_ <= 0) time_between_ = 1;
        value_ = start_;
        register_input(&in_start_value_);   register_input(&in_end_value_);
        register_input(&in_step_size_);     register_input(&in_time_between_);
        register_input(&in_loop_);          register_input(&in_auto_reverse_);
        register_input(&trig_start_);       register_input(&trig_stop_);
        register_input(&trig_pause_);       register_input(&trig_reset_);
        register_output(&value_out_);   register_output(&running_out_);
        register_output(&end_out_);     register_output(&started_out_);
        register_output(&inc_out_);     register_output(&dec_out_);
    }

    void on_start() override {
        // Setting inputs update the cached value and nudge the worker.
        in_start_value_.set_internal_sink([this](typename InputPort<T>::Value v) {
            if (v) { std::scoped_lock lk(mu_); start_ = static_cast<double>(*v); settings_changed_ = true; } cv_.notify_all();
        });
        in_end_value_.set_internal_sink([this](typename InputPort<T>::Value v) {
            if (v) { std::scoped_lock lk(mu_); end_ = static_cast<double>(*v); settings_changed_ = true; } cv_.notify_all();
        });
        in_step_size_.set_internal_sink([this](typename InputPort<T>::Value v) {
            if (v) { std::scoped_lock lk(mu_); step_ = static_cast<double>(*v); settings_changed_ = true; } cv_.notify_all();
        });
        in_time_between_.set_internal_sink([this](InputPort<std::int64_t>::Value v) {
            if (v) { std::scoped_lock lk(mu_); time_between_ = std::max<std::int64_t>(1, *v); settings_changed_ = true; } cv_.notify_all();
        });
        in_loop_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v) { std::scoped_lock lk(mu_); loop_ = *v; } cv_.notify_all();
        });
        in_auto_reverse_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v) { std::scoped_lock lk(mu_); auto_reverse_ = *v; } cv_.notify_all();
        });
        // Triggers fire on a true value.
        trig_start_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) { { std::scoped_lock lk(mu_); start_req_ = true; } cv_.notify_all(); }
        });
        trig_stop_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) { { std::scoped_lock lk(mu_); stop_req_ = true; } cv_.notify_all(); }
        });
        trig_pause_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) { { std::scoped_lock lk(mu_); pause_req_ = true; } cv_.notify_all(); }
        });
        trig_reset_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) { { std::scoped_lock lk(mu_); reset_req_ = true; } cv_.notify_all(); }
        });
        worker_ = std::jthread([this](std::stop_token st) { loop(st); });
    }

    void on_stop() override {
        worker_.request_stop();
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    enum class Phase { Idle, Running, Paused };

    // Bundle of pending emissions computed under the lock, flushed without it.
    struct Em {
        std::optional<double> value;
        std::optional<bool>   running, started, inc, dec, end;
    };

    // Cast the internal double sweep value to the configured output type T
    // (rounded for integer types so a ramp lands on whole numbers).
    static T to_value(double v) {
        if constexpr (std::is_integral_v<T>) return static_cast<T>(std::llround(v));
        else return static_cast<T>(v);
    }
    void emit_value(double v) { value_out_.emit(std::make_shared<const T>(to_value(v))); }
    void emit_b(OutputPort<bool>& p, std::optional<bool>& last, bool v) {
        if (last && *last == v) return;  // emit-on-change (worker-thread-only state)
        last = v;
        p.emit(std::make_shared<const bool>(v));
    }
    void flush(Em const& e) {
        if (e.value)   emit_value(*e.value);
        if (e.running) emit_b(running_out_, last_run_, *e.running);
        if (e.started) emit_b(started_out_, last_started_, *e.started);
        if (e.inc)     emit_b(inc_out_, last_inc_, *e.inc);
        if (e.dec)     emit_b(dec_out_, last_dec_, *e.dec);
        if (e.end)     emit_b(end_out_, last_end_, *e.end);
    }

    void anchor_next() {
        next_tp_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(time_between_);
    }

    // Apply any pending triggers (priority: stop > reset > pause > start). lk held.
    Em handle_commands() {
        Em em;
        if (stop_req_) {
            stop_req_ = false;
            if (phase_ != Phase::Idle || started_) {
                phase_ = Phase::Idle; started_ = false;
                em.running = false; em.started = false; em.inc = false; em.dec = false;
            }
        }
        if (reset_req_) {
            reset_req_ = false;
            k_ = 0; dir_ = 1; value_ = start_;
            em.value = start_; em.end = false;
            if (phase_ == Phase::Running) { em.inc = true; em.dec = false; }
        }
        if (pause_req_) {
            pause_req_ = false;
            if (phase_ == Phase::Running) { phase_ = Phase::Paused; em.running = false; }
        }
        if (start_req_) {
            start_req_ = false;
            if (phase_ == Phase::Paused) {
                phase_ = Phase::Running; em.running = true; anchor_next();
            } else if (phase_ != Phase::Running) {  // Idle → fresh sweep from start
                k_ = 0; dir_ = 1; value_ = start_; started_ = true; phase_ = Phase::Running;
                em.started = true; em.running = true; em.value = start_;
                em.inc = true; em.dec = false; em.end = false;
                anchor_next();
            }
        }
        return em;
    }

    // Advance one step (lk held; only called while Running and the interval elapsed).
    Em do_step() {
        Em em;
        const double start = start_, end = end_, step = step_;
        const bool loop = loop_, auto_reverse = auto_reverse_;
        const long long N = (step > 0.0)
            ? std::max<long long>(1, std::llround(std::fabs(end - start) / step)) : 1;
        const int dir = dir_;
        long long k = k_ + dir;
        bool reached_end = false, reached_start = false;
        if (dir > 0 && k >= N)      { k = N; reached_end = true; }
        else if (dir < 0 && k <= 0) { k = 0; reached_start = true; }
        k_ = k;

        const double t = (N > 0) ? static_cast<double>(k) / static_cast<double>(N) : 1.0;
        const double e = (dir > 0) ? combined_ease(t, anim_rise_in_, anim_rise_out_)
                                   : combined_ease(t, anim_fall_in_, anim_fall_out_);
        value_ = start + e * (end - start);
        em.value = value_;
        em.end = false;  // default; emit-on-change resets it after leaving the end

        // On natural completion the sweep stops (running=false) but stays
        // "started" — only an explicit stop/reset clears `started`.
        if (reached_end) {
            em.end = true;
            if (auto_reverse) { dir_ = -1; em.inc = false; em.dec = true; }
            else if (loop)  { k_ = 0; }  // sawtooth: wrap to start
            else            { phase_ = Phase::Idle; em.running = false; }  // stop at end
        } else if (reached_start) {  // only reachable when auto-reversing
            if (loop)       { dir_ = 1; em.inc = true; em.dec = false; }
            else            { phase_ = Phase::Idle; em.running = false; }  // auto-reverse cycle done
        }
        next_tp_ += std::chrono::milliseconds(time_between_);
        return em;
    }

    void loop(std::stop_token st) {
        using namespace std::chrono;
        std::unique_lock lk(mu_);
        Em init; init.value = value_; init.running = false; init.started = false;
        init.inc = false; init.dec = false; init.end = false;
        if (autostart_) start_req_ = true;
        lk.unlock(); flush(init); lk.lock();
        next_tp_ = steady_clock::now();

        auto pending = [&] {
            return st.stop_requested() || stop_req_ || reset_req_ || pause_req_ || start_req_;
        };
        while (!st.stop_requested()) {
            if (pending() && !st.stop_requested()) {
                Em em = handle_commands();
                lk.unlock(); flush(em); lk.lock();
                continue;
            }
            if (phase_ != Phase::Running) {
                cv_.wait(lk, [&] { return pending(); });
                continue;
            }
            if (settings_changed_) {
                settings_changed_ = false;
                next_tp_ = steady_clock::now() + milliseconds(time_between_);
                continue;
            }
            cv_.wait_until(lk, next_tp_, [&] { return pending() || settings_changed_; });
            if (st.stop_requested()) break;
            if (pending() || settings_changed_) continue;
            if (steady_clock::now() < next_tp_) continue;
            Em em = do_step();
            lk.unlock(); flush(em); lk.lock();
        }
    }

    OutputPort<T>      value_out_;
    OutputPort<bool>   running_out_, end_out_, started_out_, inc_out_, dec_out_;
    InputPort<T>       in_start_value_, in_end_value_, in_step_size_;
    InputPort<std::int64_t> in_time_between_;
    InputPort<bool>    in_loop_, in_auto_reverse_;
    InputPort<bool>    trig_start_, trig_stop_, trig_pause_, trig_reset_;

    // Easing types are config-only (no ports) → const after construction.
    const Anim anim_rise_in_, anim_rise_out_, anim_fall_in_, anim_fall_out_;

    std::mutex              mu_;
    std::condition_variable cv_;
    // Settings (cached; updated by input sinks).
    double       start_, end_, step_;
    std::int64_t time_between_;
    bool         loop_, auto_reverse_;
    bool         autostart_;
    // Worker run-state.
    Phase        phase_{Phase::Idle};
    long long    k_{0};
    int          dir_{1};
    double       value_{0.0};
    bool         started_{false};
    bool         settings_changed_{false};
    bool         start_req_{false}, stop_req_{false}, pause_req_{false}, reset_req_{false};
    std::chrono::steady_clock::time_point next_tp_{};
    // Emit-on-change cache (worker thread only).
    std::optional<bool> last_run_, last_started_, last_inc_, last_dec_, last_end_;
    std::jthread worker_;
};

}  // namespace

static auto _iterator_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("valueType", std::string{"flowboard::Double"});
    if (t == "flowboard::Double") return std::make_unique<IteratorT<double>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<IteratorT<float>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<IteratorT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<IteratorT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<IteratorT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<IteratorT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<IteratorT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<IteratorT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<IteratorT<std::uint8_t>>(std::move(id), cfg);
    throw std::runtime_error("Sources.Iterator: unsupported valueType '" + t + "' (primitive numbers only)");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sources.Iterator",
    _iterator_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "valueType":{"type":"string","title":"Value type","default":"flowboard::Double",
                     "enum":["flowboard::Double","flowboard::Float","flowboard::Int64","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8"],
                     "description":"Numeric type of startValue/endValue/stepSize and the value output."},
        "startValue":{"type":"number","title":"Start value","default":0,
                      "description":"Default for the startValue input port."},
        "endValue":{"type":"number","title":"End value","default":1,
                    "description":"Default for the endValue input port."},
        "stepSize":{"type":"number","title":"Step size","default":0.1,"exclusiveMinimum":0,
                    "description":"Value granularity: number of steps = round(|end-start|/stepSize)."},
        "timeBetweenStepsMs":{"type":"integer","title":"Time between steps (ms)","minimum":1,"default":100},
        "loop":{"type":"boolean","title":"Loop","default":false,
                "description":"Repeat after reaching the end (sawtooth), or after a full auto-reverse cycle."},
        "autoReverse":{"type":"boolean","title":"Auto-reverse (sweep back and forth)","default":false,
                    "description":"Increase to the end value then decrease back to the start (a.k.a. ping-pong / yoyo)."},
        "autostart":{"type":"boolean","title":"Auto-start","default":false,
                     "description":"Begin sweeping as soon as the graph starts (else wait for the start trigger)."},
        "specialStartStartValue":{"type":"string","title":"Easing — increasing, near start","default":"linear","enum":["linear","quad","cubic","quint","sine","expo","circ","back","elastic","bounce"]},
        "specialStartEndValue":{"type":"string","title":"Easing — increasing, near end","default":"linear","enum":["linear","quad","cubic","quint","sine","expo","circ","back","elastic","bounce"]},
        "specialEndStartValue":{"type":"string","title":"Easing — decreasing, near start","default":"linear","enum":["linear","quad","cubic","quint","sine","expo","circ","back","elastic","bounce"]},
        "specialEndEndValue":{"type":"string","title":"Easing — decreasing, near end","default":"linear","enum":["linear","quad","cubic","quint","sine","expo","circ","back","elastic","bounce"]}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"valueType":"flowboard::Double","startValue":0,"endValue":1,"stepSize":0.1,"timeBetweenStepsMs":100,"loop":false,"autoReverse":false,"autostart":false,"specialStartStartValue":"linear","specialStartEndValue":"linear","specialEndStartValue":"linear","specialEndEndValue":"linear"})JSON",
    iterator
)

}  // namespace flowboard::nodes
