// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace flowboard {

class ISyncCell;

/// \file
/// \brief Type-keyed registry of SyncCellT factories for Transform.Synchronize.

/// \brief Builds one latched in/out pair (cell) of a specific element type.
/// Registered per primitive in sync_node.cpp and per struct in generated
/// *_nodes.cpp. The node names the ports (in{i}/out{i}).
using SynchronizeFactoryFn =
    std::unique_ptr<ISyncCell>(*)(std::string in_name, std::string out_name);

/// \brief Registers a Synchronize cell factory under the given element type tag.
void register_synchronize_factory(std::string type_name, SynchronizeFactoryFn fn);

/// \brief Returns nullptr if no factory was registered for type_name.
SynchronizeFactoryFn lookup_synchronize_factory(std::string_view type_name);

/// \brief Snapshot of registered type tags (primitives + structs), used by the
/// web Inspector to populate the Synchronize per-input type dropdowns.
std::vector<std::string> registered_synchronize_types();

}  // namespace flowboard

#define OP_SYNC_CAT_(a, b) a##b
#define OP_SYNC_CAT(a, b)  OP_SYNC_CAT_(a, b)

/// \brief Registers a Synchronize cell factory for a type at static-init time.
#define OP_REGISTER_SYNCHRONIZE_FACTORY(TypeNameStr, FactoryFn)                    \
    namespace {                                                                    \
    const bool OP_SYNC_CAT(_op_sync_reg_, __COUNTER__) = [] {                     \
        ::flowboard::register_synchronize_factory((TypeNameStr), (FactoryFn));  \
        return true;                                                               \
    }();                                                                           \
    }
