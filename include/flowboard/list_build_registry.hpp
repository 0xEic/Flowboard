// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include <string_view>

namespace flowboard {

/// \file
/// \brief Registry of factories that build Transform.List.Build /
/// Transform.List.Accumulate nodes for a given element type. Primitive element
/// types are handled directly in the node factories; struct element types are
/// registered here by the per-struct code pipegen emits (mirroring the
/// extract-factory registry), so these nodes can assemble/accumulate a list of
/// any OnboardAPI struct.

class Node;

/// \brief Factory signature: (node id, N) -> Node, where N is the item count
/// (Build) or buffer cap (Accumulate). Closes over the element type chosen at
/// codegen time.
using ListElemFactoryFn = std::unique_ptr<Node>(*)(std::string id, int n);

/// \brief Register the Build + Accumulate factories for an element type tag.
void register_list_elem_factories(std::string element_tag,
                                  ListElemFactoryFn build, ListElemFactoryFn accumulate);

/// \brief Look up the Build factory for an element type tag; nullptr if none.
ListElemFactoryFn lookup_list_build_factory(std::string_view element_tag);
/// \brief Look up the Accumulate factory for an element type tag; nullptr if none.
ListElemFactoryFn lookup_list_accumulate_factory(std::string_view element_tag);

}  // namespace flowboard

#define OP_LISTELEM_CAT_(a, b) a##b
#define OP_LISTELEM_CAT(a, b)  OP_LISTELEM_CAT_(a, b)

// Static-init registration. Place at file scope inside a generated _nodes.cpp.
#define OP_REGISTER_LIST_ELEM_FACTORIES(TypeNameStr, BuildFn, AccumFn)               \
    namespace {                                                                       \
    const bool OP_LISTELEM_CAT(_op_listelem_reg_, __COUNTER__) = [] {                 \
        ::flowboard::register_list_elem_factories((TypeNameStr), (BuildFn), (AccumFn));\
        return true;                                                                  \
    }();                                                                              \
    }
