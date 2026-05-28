// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "flowboard/graph.hpp"

/// \file
/// \brief Loads a graph definition from a JSON document into an executable flowboard::Graph.

namespace flowboard::config {

/// \brief Outcome of loading a graph: the built graph and any errors encountered.
struct LoadResult {
    std::unique_ptr<Graph> graph;            ///< null on failure
    std::vector<std::string> errors;         ///< empty on success
};

/// \brief Parse and build a flowboard::Graph from a JSON graph definition.
LoadResult load(nlohmann::json const& doc);

}  // namespace flowboard::config
