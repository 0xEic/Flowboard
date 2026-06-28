// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/synchronize_registry.hpp"

namespace flowboard {

/// \file
/// \brief Barrier/join node whose in/out pairs may each carry a different type,
///        plus a forceOutput trigger that emits current-or-default values now.

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

/// \brief One latched input/output pair of a single concrete type, type-erased so
/// SynchronizeNode can hold pairs of different types. Re-emits the exact value it
/// latched (zero-copy passthrough), or a configured default when nothing latched.
class ISyncCell {
public:
    virtual ~ISyncCell() = default;
    virtual IInputPort*  input()  = 0;   ///< the in{i} port (owned by the cell)
    virtual IOutputPort* output() = 0;   ///< the out{i} port (owned by the cell)
    virtual bool received() const = 0;   ///< has a value arrived since the last reset?
    virtual void reset()          = 0;   ///< clear the received flag (arm for next cycle)
    virtual void emit_latched()   = 0;   ///< emit current value, else default, else nothing
    /// Set the fallback emitted by emit_latched() when no value was latched
    /// (used by forceOutput). Converts \p j to the cell's element type.
    virtual void set_default_json(::nlohmann::json const& j) = 0;
    /// Wire the input's internal sink. On each incoming value the cell calls
    /// `post(closure)` — `post` runs `closure` on the node's worker thread — and
    /// the closure latches the value, sets received, then calls `on_arrived`.
    /// Lifetime invariant: the cell and the SynchronizeNode that owns it (whose
    /// `enqueue`/`maybe_release` back `post`/`on_arrived`) must outlive any armed
    /// sink. Node::stop() joins the worker before destruction, which guarantees
    /// this — no posted closure runs after teardown.
    virtual void arm(std::function<void(std::function<void()>)> post,
                     std::function<void()> on_arrived) = 0;
};

/// \brief Concrete cell for element type T.
template <typename T>
class SyncCellT final : public ISyncCell {
public:
    SyncCellT(std::string in_name, std::string out_name)
        : in_(std::move(in_name)), out_(std::move(out_name)) {}

    IInputPort*  input()  override { return &in_; }
    IOutputPort* output() override { return &out_; }
    bool received() const override { return got_; }
    void reset()          override { got_ = false; }

    // Current value if one was ever latched, else the configured default, else
    // nothing. The normal barrier release always has last_ set, so it never
    // reaches default_; forceOutput is the only path that can.
    void emit_latched() override {
        if (last_)         out_.emit(last_);
        else if (default_) out_.emit(default_);
    }

    void set_default_json(::nlohmann::json const& j) override {
        try { default_ = std::make_shared<const T>(j.get<T>()); }
        catch (...) { /* ignore an unconvertible default */ }
    }

    void arm(std::function<void(std::function<void()>)> post,
             std::function<void()> on_arrived) override {
        in_.set_internal_sink(
            [this, post = std::move(post), on_arrived = std::move(on_arrived)]
            (typename InputPort<T>::Value v) {
                // Latch on the node's worker so all barrier state stays serialized.
                post([this, v = std::move(v), on_arrived] {
                    last_ = v;
                    got_  = true;
                    on_arrived();
                });
            });
    }

private:
    InputPort<T>  in_;
    OutputPort<T> out_;
    typename InputPort<T>::Value last_{};
    typename InputPort<T>::Value default_{};
    bool got_{false};
};

/// \brief Barrier/join with per-pair types. Two ways to emit:
///   - the all-received barrier: when every input has received a value, emit
///     beforeOutput, each out{i} (in order), afterOutput, then re-arm;
///   - forceOutput (Bool input): on `true`, emit beforeOutput, each out{i}'s
///     current-or-default value (in order), afterOutput — independent of the
///     barrier, leaving the barrier's received flags untouched.
/// Per-input defaults come from config.defaults["in{i}"] (the inline editors on
/// the canvas node). They are held on the cells and feed forceOutput; they do
/// NOT arm the barrier (seed_input_defaults is overridden to a no-op).
class SynchronizeNode final : public Node {
public:
    SynchronizeNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Synchronize") {
        std::string fallback = cfg.value("inputType", std::string{"flowboard::Double"});

        std::vector<std::string> types;
        if (auto it = cfg.find("inputTypes"); it != cfg.end() && it->is_array()) {
            for (auto const& e : *it)
                types.push_back(e.is_string() ? e.get<std::string>() : fallback);
        }

        std::size_t n;
        if (!types.empty()) {
            n = std::min<std::size_t>(types.size(), 64);
            types.resize(n);
        } else {
            n = static_cast<std::size_t>(std::clamp<long long>(cfg.value("inputCount", 2), 1, 64));
            types.assign(n, fallback);
        }
        for (auto& t : types) if (t.empty()) t = fallback;

        order_ = detail::synchronize_parse_order(cfg, n);

        for (std::size_t i = 0; i < n; ++i) {
            auto make = lookup_synchronize_factory(types[i]);
            if (!make)
                throw std::runtime_error("Transform.Synchronize: unsupported type '" + types[i] + "'");
            auto cell = make("in" + std::to_string(i), "out" + std::to_string(i));
            register_input(cell->input());
            register_output(cell->output());
            cells_.push_back(std::move(cell));
        }

        // Per-input default values (used by forceOutput when an input has no
        // current value), sourced from the inline default editors on the canvas
        // node (config.defaults["in{i}"]).
        if (auto it = cfg.find("defaults"); it != cfg.end() && it->is_object()) {
            for (std::size_t i = 0; i < cells_.size(); ++i) {
                auto dit = it->find("in" + std::to_string(i));
                if (dit != it->end() && !dit->is_null())
                    cells_[i]->set_default_json(*dit);
            }
        }

        before_ = std::make_unique<OutputPort<bool>>("beforeOutput");
        after_  = std::make_unique<OutputPort<bool>>("afterOutput");
        register_output(before_.get());
        register_output(after_.get());

        // forceOutput: a trigger that emits current-or-default values now,
        // independent of the all-received barrier.
        force_ = std::make_unique<InputPort<bool>>("forceOutput");
        register_input(force_.get());
    }

    void on_start() override {
        auto post = [this](std::function<void()> f) { enqueue(std::move(f)); };
        for (auto& c : cells_)
            c->arm(post, [this] { maybe_release(); });
        force_->set_internal_sink([this](InputPort<bool>::Value v) {
            if (v && *v) enqueue([this] { force_release(); });
        });
    }

    // Defaults are held on the cells (set in the constructor) and consumed by
    // forceOutput; do NOT deliver them as values here — that would arm the
    // barrier and fire a spurious release at graph start.
    void seed_input_defaults(std::function<bool(std::string const&)> const&) override {}

private:
    // Emit beforeOutput, each out{i} in order, afterOutput. Shared by the
    // barrier release and forceOutput.
    void emit_all() {
        before_->emit(std::make_shared<const bool>(true));
        for (std::size_t idx : order_) cells_[idx]->emit_latched();
        after_->emit(std::make_shared<const bool>(true));
    }

    void maybe_release() {
        for (auto const& c : cells_) if (!c->received()) return;  // still waiting
        emit_all();
        for (auto& c : cells_) c->reset();  // arm for the next cycle
    }

    void force_release() { emit_all(); }  // independent of the barrier; no reset

    std::vector<std::size_t> order_;
    std::vector<std::unique_ptr<ISyncCell>> cells_;
    std::unique_ptr<OutputPort<bool>> before_, after_;
    std::unique_ptr<InputPort<bool>>  force_;
};

}  // namespace flowboard
