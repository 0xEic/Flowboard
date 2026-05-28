// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Type-agnostic barrier/join node that latches one value per input and releases them together.

namespace detail {
/// \brief Normalise the configured emission order into a permutation of [0, n).
inline std::vector<std::size_t> synchronize_parse_order(::nlohmann::json const& cfg, std::size_t n) {
    std::vector<std::size_t> order;
    std::vector<bool> seen(n, false);
    if (auto it = cfg.find("order"); it != cfg.end() && it->is_array()) {
        for (auto const& e : *it) {
            if (!e.is_number_integer()) continue;
            long long v = e.get<long long>();
            if (v >= 0 && static_cast<std::size_t>(v) < n && !seen[v]) {
                order.push_back(static_cast<std::size_t>(v));
                seen[v] = true;
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) if (!seen[i]) order.push_back(i);  // append any missing
    return order;
}
}  // namespace detail

/// \brief Barrier / join: waits until every input port has received a value (since the
/// last release or graph start), then releases the latched values together:
///   1. emit `beforeOutput` (true)
///   2. emit each input's last value on its matching output, in the configured order
///   3. emit `afterOutput` (true)
/// then resets and waits for a fresh value on every input again.
///
/// Type-agnostic — works for any T (primitives and generated onboardapi structs),
/// so the same template backs both the per-primitive registrations in
/// sync_node.cpp and the per-struct registrations emitted into *_nodes.cpp.
/// \tparam T Element type carried on every input/output pair.
template <typename T>
class SynchronizeT : public Node {
public:
    SynchronizeT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Synchronize") {
        n_ = static_cast<std::size_t>(std::clamp<long long>(cfg.value("inputCount", 2), 1, 64));
        order_ = detail::synchronize_parse_order(cfg, n_);
        for (std::size_t i = 0; i < n_; ++i) {
            ins_.push_back(std::make_unique<InputPort<T>>("in" + std::to_string(i)));
            outs_.push_back(std::make_unique<OutputPort<T>>("out" + std::to_string(i)));
            register_input(ins_.back().get());
            register_output(outs_.back().get());
        }
        before_ = std::make_unique<OutputPort<bool>>("beforeOutput");
        after_  = std::make_unique<OutputPort<bool>>("afterOutput");
        register_output(before_.get());
        register_output(after_.get());
        last_.resize(n_);
        received_.assign(n_, false);
    }

    void on_start() override {
        for (std::size_t i = 0; i < n_; ++i) {
            ins_[i]->set_internal_sink([this, i](typename InputPort<T>::Value v) {
                enqueue([this, i, v] { on_input(i, v); });
            });
        }
    }

private:
    /// \brief Runs on the node's single worker thread (enqueue serialises all inputs),
    /// so the barrier state needs no extra locking.
    void on_input(std::size_t i, typename InputPort<T>::Value v) {
        last_[i] = std::move(v);
        received_[i] = true;
        for (bool r : received_) if (!r) return;  // still waiting on some input
        release();
    }

    void release() {
        before_->emit(std::make_shared<const bool>(true));
        for (std::size_t idx : order_)
            if (last_[idx]) outs_[idx]->emit(last_[idx]);
        after_->emit(std::make_shared<const bool>(true));
        std::fill(received_.begin(), received_.end(), false);  // arm for the next cycle
    }

    std::size_t n_{0};
    std::vector<std::size_t> order_;
    std::vector<std::unique_ptr<InputPort<T>>>  ins_;
    std::vector<std::unique_ptr<OutputPort<T>>> outs_;
    std::unique_ptr<OutputPort<bool>> before_, after_;
    std::vector<typename InputPort<T>::Value> last_;
    std::vector<bool> received_;
};

}  // namespace flowboard
