// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include "flowboard/edge.hpp"
#include "flowboard/graph.hpp"
#include "flowboard/port.hpp"

namespace flowboard {

/// \file
/// \brief Type-keyed registry of wire factories that create typed flowboard::Graph edges between ports.

/// \brief Wire factory: knows the static element type bound at registration time.
/// Drives `Graph::wire_typed_edge<T>(...)` for that T.
using WireFactoryFn = void(*)(Graph& g,
                              IOutputPort* sport, IInputPort* dport,
                              std::string src_node, std::string src_port,
                              std::string dst_node, std::string dst_port,
                              std::size_t capacity, BackpressurePolicy policy);

/// \brief Registers a wire factory under the given element type tag.
void register_wire_factory(std::string type_tag, WireFactoryFn fn);
/// \brief Returns nullptr if no factory was registered for type_tag.
WireFactoryFn lookup_wire_factory(std::string_view type_tag);

/// \brief Templated helper used by every registration site. Captures T at compile time
/// and forwards to the Graph's templated wire member.
/// \tparam T Element type carried across the wired edge.
template <typename T>
inline void wire_typed_thunk(Graph& g,
                             IOutputPort* sport, IInputPort* dport,
                             std::string src_node, std::string src_port,
                             std::string dst_node, std::string dst_port,
                             std::size_t capacity, BackpressurePolicy policy) {
    g.wire_typed_edge<T>(sport, dport,
                          std::move(src_node), std::move(src_port),
                          std::move(dst_node), std::move(dst_port),
                          capacity, policy);
}

}  // namespace flowboard

#define OP_WIRE_CAT_(a, b) a##b
#define OP_WIRE_CAT(a, b)  OP_WIRE_CAT_(a, b)

/// \brief Register a wire factory for a single type. The type must have OP_DECLARE_TYPE
/// applied and an InputPort<T>/OutputPort<T> already exposed by whatever node
/// emits/consumes it.
#define OP_REGISTER_WIRE_FACTORY(TypeNameStr, CppType)                          \
    namespace {                                                                  \
    const bool OP_WIRE_CAT(_op_wire_reg_, __COUNTER__) = [] {                   \
        ::flowboard::register_wire_factory(                                   \
            (TypeNameStr),                                                       \
            &::flowboard::wire_typed_thunk<CppType>);                         \
        return true;                                                             \
    }();                                                                         \
    }
