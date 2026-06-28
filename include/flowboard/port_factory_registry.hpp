// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "flowboard/port.hpp"
#include "flowboard/json_helpers.hpp"

/// \file
/// \brief Per-type-tag registry of port factories with JSON marshalling, so a
///        dynamic-port node (Python.Script) can spin up typed ports — and
///        round-trip values through JSON — for any type the engine knows
///        about, including codegen-emitted OnboardAPI structs.

namespace flowboard {

/// Build an InputPort<T> named \p name whose internal sink converts each value
/// to JSON (via the type's `to_json` ADL overload) and hands it to \p on_json.
using MakeInputWithJsonSinkFn =
    std::unique_ptr<IInputPort>(*)(std::string name,
                                   std::function<void(::nlohmann::json)> on_json);

/// Build an OutputPort<T> named \p name. The caller wires sinks itself.
using MakeOutputFn = std::unique_ptr<IOutputPort>(*)(std::string name);

/// Convert \p j to T (via `from_json` ADL) and emit on \p port. Returns false
/// if the conversion fails or the port isn't actually an OutputPort<T>.
using EmitFromJsonFn = bool(*)(IOutputPort* port, ::nlohmann::json const& j);

/// All three function pointers for a single port type.
struct PortFactoryEntry {
    MakeInputWithJsonSinkFn make_input;
    MakeOutputFn            make_output;
    EmitFromJsonFn          emit_from_json;
};

/// Register a factory under \p tag. Last writer wins.
void register_port_factory(std::string tag, PortFactoryEntry entry);
/// Look up a factory by tag. Returns nullptr if none is registered.
PortFactoryEntry const* lookup_port_factory(std::string_view tag);

// --- Template glue: turn a (TagString, CppType) pair into a PortFactoryEntry.
//     Used by OP_REGISTER_PORT_FACTORY below; relies on nlohmann::json having
//     to_json/from_json ADL overloads for CppType (true for every built-in
//     type and every pipegen-emitted struct).

template <typename T>
inline std::unique_ptr<IInputPort> _make_input_with_json_sink(
        std::string name,
        std::function<void(::nlohmann::json)> on_json) {
    auto port = std::make_unique<InputPort<T>>(std::move(name));
    port->set_internal_sink(
        [cb = std::move(on_json)](typename InputPort<T>::Value v) {
            if (!v) return;
            // Use to_json_or_passthrough so pipegen's 1-arg ADL to_json overloads
            // are honoured (nlohmann::json(T) alone only finds the 2-arg form).
            try { cb(::flowboard::to_json_or_passthrough(*v)); } catch (...) { /* drop */ }
        });
    return port;
}

template <typename T>
inline std::unique_ptr<IOutputPort> _make_output(std::string name) {
    return std::make_unique<OutputPort<T>>(std::move(name));
}

template <typename T>
inline bool _emit_from_json(IOutputPort* port, ::nlohmann::json const& j) {
    auto* typed = static_cast<OutputPort<T>*>(port);
    try {
        typed->emit(std::make_shared<const T>(j.get<T>()));
        return true;
    } catch (...) {
        return false;
    }
}

/// Static-init helper used by OP_REGISTER_PORT_FACTORY.
struct PortFactoryRegistrar {
    PortFactoryRegistrar(std::string tag, PortFactoryEntry entry) {
        register_port_factory(std::move(tag), entry);
    }
};

}  // namespace flowboard

#define OP_PORTFACT_CAT_(a, b) a##b
#define OP_PORTFACT_CAT(a, b)  OP_PORTFACT_CAT_(a, b)

/// \brief Register a typed port factory + JSON marshallers for \p CppType under
///        the tag \p TagString. \p CppType must have an `OP_DECLARE_TYPE` and
///        ADL `to_json`/`from_json` overloads (pipegen emits both for every
///        OnboardAPI struct; builtin scalars + List use nlohmann's built-ins).
#define OP_REGISTER_PORT_FACTORY(TagString, CppType)                            \
    namespace {                                                                  \
    const ::flowboard::PortFactoryRegistrar                                      \
        OP_PORTFACT_CAT(_op_port_factory_reg_, __COUNTER__){                    \
            (TagString),                                                         \
            ::flowboard::PortFactoryEntry{                                       \
                &::flowboard::_make_input_with_json_sink<CppType>,              \
                &::flowboard::_make_output<CppType>,                            \
                &::flowboard::_emit_from_json<CppType>}};                       \
    }
