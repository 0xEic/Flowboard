// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace flowboard {

class Node;

/// \file
/// \brief Type-keyed registry of flowboard::SynchronizeT factories for building Transform.Synchronize nodes.

/// \brief Per-type Transform.Synchronize factory. Closed over a specific element type at
/// registration (in sync_node.cpp for primitives, in generated *_nodes.cpp for
/// onboardapi structs). Receives the full node config so it can read inputCount
/// and order alongside inputType.
using SynchronizeFactoryFn =
    std::unique_ptr<Node>(*)(std::string id, ::nlohmann::json const& cfg);

/// \brief Registers a Synchronize factory under the given element type tag.
void register_synchronize_factory(std::string type_name, SynchronizeFactoryFn fn);

/// \brief Returns nullptr if no factory was registered for type_name.
SynchronizeFactoryFn lookup_synchronize_factory(std::string_view type_name);

/// \brief Snapshot of currently registered type tags (primitives + structs), used by the
/// web Inspector to populate the Synchronize value-type dropdown.
std::vector<std::string> registered_synchronize_types();

}  // namespace flowboard

#define OP_SYNC_CAT_(a, b) a##b
#define OP_SYNC_CAT(a, b)  OP_SYNC_CAT_(a, b)

/// \brief Registers a Synchronize factory for a type at static-init time.
#define OP_REGISTER_SYNCHRONIZE_FACTORY(TypeNameStr, FactoryFn)                    \
    namespace {                                                                    \
    const bool OP_SYNC_CAT(_op_sync_reg_, __COUNTER__) = [] {                     \
        ::flowboard::register_synchronize_factory((TypeNameStr), (FactoryFn));  \
        return true;                                                               \
    }();                                                                           \
    }
