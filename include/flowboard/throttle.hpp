// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Type-agnostic rate-limiting node that forwards values no more often than a minimum period.

/// \brief Rate-limiter: forwards a value only when at least `minPeriodMs` has elapsed
/// since the last forwarded value, otherwise drops it. Type-agnostic — works for
/// any T (primitives and generated onboardapi structs), so the same template
/// backs both the per-primitive registrations in throttle.cpp and the per-struct
/// registrations emitted into the generated *_nodes.cpp.
/// \tparam T Element type carried on the in/out ports.
template <typename T>
class ThrottleT : public Node {
public:
    ThrottleT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Throttle"),
          in_("in"), out_("out"),
          period_(cfg.value("minPeriodMs", std::int64_t{100})) {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] {
                auto now = std::chrono::steady_clock::now();
                if (now - last_ < std::chrono::milliseconds(period_)) return;
                last_ = now;
                out_.emit(v);
            });
        });
    }
private:
    InputPort<T>  in_;
    OutputPort<T> out_;
    std::int64_t  period_;
    std::chrono::steady_clock::time_point last_{};
};

}  // namespace flowboard
