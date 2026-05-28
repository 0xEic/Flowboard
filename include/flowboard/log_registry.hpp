// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

/// \file
/// \brief Registry mapping input type tags to per-type Sinks.Log node factories.

namespace flowboard {

class Node;

/// \brief Per-type Sinks.Log factory. Closed over a specific input type at codegen
/// (or in log_node.cpp for primitives); receives the full node config so it
/// can read `prefix` and `path` alongside `inputType`.
using LogFactoryFn =
    std::unique_ptr<Node>(*)(std::string id, ::nlohmann::json const& cfg);

/// \brief Register a Sinks.Log factory for the given input type tag.
void register_log_factory(std::string type_name, LogFactoryFn fn);

/// \brief Look up the Sinks.Log factory for an input type tag.
/// \details Returns nullptr if no factory was registered for type_name.
LogFactoryFn lookup_log_factory(std::string_view type_name);

/// \brief Return a snapshot of currently registered log input type tags in arbitrary order.
/// \details Used by the web Inspector to populate the inputType dropdown without
/// having to scrape every catalog port.
std::vector<std::string> registered_log_types();

}  // namespace flowboard

#define OP_LOG_CAT_(a, b) a##b
#define OP_LOG_CAT(a, b)  OP_LOG_CAT_(a, b)

#define OP_REGISTER_LOG_FACTORY(TypeNameStr, FactoryFn)                          \
    namespace {                                                                   \
    const bool OP_LOG_CAT(_op_log_reg_, __COUNTER__) = [] {                      \
        ::flowboard::register_log_factory((TypeNameStr), (FactoryFn));        \
        return true;                                                              \
    }();                                                                          \
    }
