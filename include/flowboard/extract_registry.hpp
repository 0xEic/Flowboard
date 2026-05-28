// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include <string_view>

namespace flowboard {

/// \file
/// \brief Registry of factories that build extract nodes for a given bound input type.

class Node;

/// \brief Factory signature: takes a node id and a field name; returns a Node, or nullptr
/// if the field is unknown for the bound input type. The factory is responsible for
/// knowing its bound input type (closed over at codegen time).
using ExtractFactoryFn =
    std::unique_ptr<Node>(*)(std::string id, std::string field);

/// \brief Register an extract factory under a type name.
void register_extract_factory(std::string type_name, ExtractFactoryFn fn);

/// \brief Look up a registered extract factory; returns nullptr if no factory was registered for type_name.
ExtractFactoryFn lookup_extract_factory(std::string_view type_name);

}  // namespace flowboard

// Helper for token-pasting __COUNTER__ correctly through two macro expansions.
#define OP_EXTRACT_CAT_(a, b) a##b
#define OP_EXTRACT_CAT(a, b)  OP_EXTRACT_CAT_(a, b)

// Static-init registration. Place at file scope inside a generated _nodes.cpp.
#define OP_REGISTER_EXTRACT_FACTORY(TypeNameStr, FactoryFn)                          \
    namespace {                                                                       \
    const bool OP_EXTRACT_CAT(_op_extract_reg_, __COUNTER__) = [] {                  \
        ::flowboard::register_extract_factory((TypeNameStr), (FactoryFn));        \
        return true;                                                                  \
    }();                                                                              \
    }
