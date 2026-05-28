// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "flowboard/json_helpers.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Type-keyed registry of live-tap factories that stream output-port emissions as JSON.

/// \brief Receives (port_key like "node-id.port-name", JSON-serialized value as string).
using LivePublishFn = std::function<void(std::string const&, std::string const&)>;

/// \brief Tap factory: attaches a live-value sink to an output port of a statically
/// known element type, serializing each emission to JSON. Registered per type
/// (primitives + ListValue in builtin_types.cpp; every struct in generated
/// *_nodes.cpp) so attach_live_taps() can dispatch without a hardcoded list.
using TapFactoryFn = void(*)(IOutputPort* port, std::string const& key,
                             LivePublishFn const& fn);

/// \brief Registers a tap factory under the given element type tag.
void register_tap_factory(std::string type_tag, TapFactoryFn fn);
/// \brief Returns nullptr if no factory was registered for type_tag.
TapFactoryFn lookup_tap_factory(std::string_view type_tag);

/// \brief Captures T at compile time; serializes via to_json_or_passthrough so the same
/// thunk works for primitives, enums, ListValue, and every onboardapi struct.
/// \tparam T Element type carried on the output port being tapped.
template <typename T>
inline void tap_typed_thunk(IOutputPort* port, std::string const& key,
                            LivePublishFn const& fn) {
    auto* p = static_cast<OutputPort<T>*>(port);
    p->attach_sink([key, fn](std::shared_ptr<const T> v) {
        fn(key, ::flowboard::to_json_or_passthrough(*v).dump());
    });
}

}  // namespace flowboard

#define OP_TAP_CAT_(a, b) a##b
#define OP_TAP_CAT(a, b)  OP_TAP_CAT_(a, b)

/// \brief Register a live-tap factory for a single type. The type must have
/// OP_DECLARE_TYPE applied and an OutputPort<T> exposed by some node.
#define OP_REGISTER_TAP_FACTORY(TypeNameStr, CppType)                           \
    namespace {                                                                  \
    const bool OP_TAP_CAT(_op_tap_reg_, __COUNTER__) = [] {                     \
        ::flowboard::register_tap_factory(                                    \
            (TypeNameStr),                                                       \
            &::flowboard::tap_typed_thunk<CppType>);                          \
        return true;                                                             \
    }();                                                                         \
    }
