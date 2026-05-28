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
/// \brief Type-keyed registry of flowboard::ThrottleT factories for building Transform.Throttle nodes.

/// \brief Per-type Transform.Throttle factory. Closed over a specific element type at
/// registration (in throttle.cpp for primitives, in generated *_nodes.cpp for
/// onboardapi structs). Receives the full node config so it can read
/// `minPeriodMs` alongside `inputType`.
using ThrottleFactoryFn =
    std::unique_ptr<Node>(*)(std::string id, ::nlohmann::json const& cfg);

/// \brief Registers a Throttle factory under the given element type tag.
void register_throttle_factory(std::string type_name, ThrottleFactoryFn fn);

/// \brief Returns nullptr if no factory was registered for type_name.
ThrottleFactoryFn lookup_throttle_factory(std::string_view type_name);

/// \brief Snapshot of currently registered type tags (primitives + structs), used by
/// the web Inspector to populate the Throttle inputType dropdown.
std::vector<std::string> registered_throttle_types();

}  // namespace flowboard

#define OP_THROTTLE_CAT_(a, b) a##b
#define OP_THROTTLE_CAT(a, b)  OP_THROTTLE_CAT_(a, b)

/// \brief Registers a Throttle factory for a type at static-init time.
#define OP_REGISTER_THROTTLE_FACTORY(TypeNameStr, FactoryFn)                       \
    namespace {                                                                    \
    const bool OP_THROTTLE_CAT(_op_throttle_reg_, __COUNTER__) = [] {             \
        ::flowboard::register_throttle_factory((TypeNameStr), (FactoryFn));     \
        return true;                                                               \
    }();                                                                           \
    }
