// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <nlohmann/json.hpp>
#include "flowboard/type_tag.hpp"

/// \file
/// \brief Strongly-typed input/output port endpoints.

namespace flowboard {

/// \brief Type-erased interface to an input port.
///
/// Lets the graph and loader inspect and feed a port without knowing its
/// message type. Concrete behavior lives in InputPort.
class IInputPort {
public:
    virtual ~IInputPort() = default;
    /// \brief The port's name, unique within its owning node.
    virtual std::string_view name() const = 0;
    /// \brief The message type tag (see flowboard::type_tag_v).
    virtual std::string_view type_tag() const = 0;
    /// \brief Deliver a JSON-encoded default value.
    ///
    /// Used to seed unconnected primitive inputs at graph start.
    /// \return `false` for non-primitive types or on a conversion error,
    ///         leaving the port untouched; `true` if a value was delivered.
    virtual bool deliver_json(::nlohmann::json const& j) = 0;
};

/// \brief Type-erased interface to an output port.
class IOutputPort {
public:
    virtual ~IOutputPort() = default;
    /// \brief The port's name, unique within its owning node.
    virtual std::string_view name() const = 0;
    /// \brief The message type tag (see flowboard::type_tag_v).
    virtual std::string_view type_tag() const = 0;
    /// \brief Attach a value-ignoring sink that runs \p on_emit for every emission.
    ///
    /// Type-erased (no knowledge of the port's `T`): used by "signal" edges,
    /// where any-typed output drives a Bool trigger input — each emission, of
    /// any value, fires the callback (which delivers a `true` pulse).
    virtual void attach_signal_sink(std::function<void()> on_emit) = 0;
};

/// \brief A typed input endpoint that forwards delivered values into its node.
/// \tparam T The message type carried by this port.
template <typename T>
class InputPort final : public IInputPort {
public:
    /// \brief Immutable, shareable message value type.
    using Value = std::shared_ptr<const T>;
    /// \brief Callback invoked for each delivered value.
    using Sink  = std::function<void(Value)>;

    explicit InputPort(std::string name) : name_(std::move(name)) {}

    std::string_view name() const override { return name_; }
    std::string_view type_tag() const override { return type_tag_v<T>; }

    /// \brief Install the sink the owning node uses to receive values.
    ///
    /// The node sets this to push values into its event-driven worker.
    void set_internal_sink(Sink sink) { internal_sink_ = std::move(sink); }
    /// \brief Deliver a value to the owning node (no-op if no sink is set).
    void deliver(Value v) const { if (internal_sink_) internal_sink_(std::move(v)); }

    bool deliver_json(::nlohmann::json const& j) override {
        // Only primitive scalars + string have a meaningful JSON default; struct
        // and container inputs are not seeded (no editor is offered for them).
        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            try { deliver(std::make_shared<const T>(j.get<T>())); return true; }
            catch (...) { return false; }
        } else {
            (void)j; return false;
        }
    }

private:
    std::string name_;
    Sink internal_sink_;
};

/// \brief A typed output endpoint that fans a value out to attached sinks.
/// \tparam T The message type carried by this port.
template <typename T>
class OutputPort final : public IOutputPort {
public:
    /// \brief Immutable, shareable message value type.
    using Value = std::shared_ptr<const T>;
    /// \brief Callback invoked for each emitted value.
    using Sink  = std::function<void(Value)>;

    explicit OutputPort(std::string name) : name_(std::move(name)) {}

    std::string_view name() const override { return name_; }
    std::string_view type_tag() const override { return type_tag_v<T>; }

    /// \brief Attach a sink; one edge installs one sink here.
    void attach_sink(Sink sink) { sinks_.push_back(std::move(sink)); }
    /// \brief Attach a value-ignoring sink (signal edge): fires on every emit.
    void attach_signal_sink(std::function<void()> on_emit) override {
        sinks_.push_back([fn = std::move(on_emit)](Value) { fn(); });
    }
    /// \brief Emit a value to every attached sink (fan-out).
    void emit(Value v) const {
        for (auto const& sink : sinks_) sink(v);
    }

private:
    std::string name_;
    std::vector<Sink> sinks_;
};

}  // namespace flowboard
