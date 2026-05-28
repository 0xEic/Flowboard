// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Time-derivative node: emits the rate of change (per second) of a numeric input.

/// \brief Computes d(value)/dt from successive samples.
///
/// On each input it emits `(value - previousValue) / (seconds since the previous
/// sample)`, using wall-clock arrival times (a steady clock). The first sample
/// only primes the state (no output, since a rate needs two points); samples
/// with a non-positive time delta are skipped. The output is always `double`
/// (units of the input per second) — e.g. an azimuth position stream becomes an
/// azimuth rate.
/// \tparam T Numeric element type carried on the input port.
template <typename T>
class DerivativeT : public Node {
public:
    DerivativeT(std::string id, ::nlohmann::json const& /*cfg*/)
        : Node(std::move(id), "Transform.Derivative"),
          in_("in"), out_("out") {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            if (!v) return;
            enqueue([this, v] {
                auto now = std::chrono::steady_clock::now();
                double cur = static_cast<double>(*v);
                if (have_prev_) {
                    double dt = std::chrono::duration<double>(now - prev_t_).count();
                    if (dt > 0.0)
                        out_.emit(std::make_shared<const double>((cur - prev_v_) / dt));
                }
                prev_v_    = cur;
                prev_t_    = now;
                have_prev_ = true;
            });
        });
    }

private:
    InputPort<T>       in_;
    OutputPort<double> out_;
    double             prev_v_ = 0.0;
    std::chrono::steady_clock::time_point prev_t_{};
    bool               have_prev_ = false;
};

}  // namespace flowboard
