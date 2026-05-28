// SPDX-License-Identifier: MIT
#pragma once
#include "flowboard/tap_registry.hpp"  // provides LivePublishFn

/// \file
/// \brief Attaches live taps to a graph's output ports to forward port values as JSON.

namespace flowboard {

class Graph;

/// \brief Walks every node's output port and, for each type tag with a registered tap
/// factory (all primitives + ListValue + every generated struct), attaches a
/// sink that serializes the value to JSON and forwards it via fn.
void attach_live_taps(Graph& g, LivePublishFn fn);

}  // namespace flowboard
