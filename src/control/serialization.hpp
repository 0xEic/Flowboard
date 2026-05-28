// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>

namespace flowboard {
class Graph;
namespace control {

// Probes the NodeRegistry: for each registered name, instantiates a node with an
// empty config and inspects its port shape. Returns an array of
// { typeName, inputs: [...], outputs: [...] }.
nlohmann::json catalog_to_json();

// Serializes the live graph: nodes with their port shapes.
// Edge list comes from Graph::source_json (added in Task 6); this function
// only returns nodes.
nlohmann::json graph_to_json(Graph const& g);

// Returns per-edge pushed/dropped counters for the live graph.
nlohmann::json stats_to_json(Graph const& g);

}  // namespace control
}  // namespace flowboard
