// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "flowboard/graph.hpp"
#include "flowboard/live_taps.hpp"

/// \file
/// \brief Owns the live Graph and supports atomic hot-swap of configurations.

namespace flowboard {

/// \brief Holds the currently running Graph behind a mutex.
///
/// The control plane uses this to hot-swap configurations: reload() validates a
/// new JSON config, builds a fresh Graph, and replaces the live one — rolling
/// back on any validation error so a bad config never takes down a running
/// pipeline. Callers that touch the raw graph must hold mu().
class GraphHolder {
public:
    /// \brief Install a new graph, stopping and replacing any previous one.
    /// \param g The graph to take ownership of and start.
    void install(std::unique_ptr<Graph> g);

    /// \brief Validate a config, then atomically replace and start the graph.
    /// \param doc The new graph configuration.
    /// \return Validation errors; empty on success (the swap only happens then).
    std::vector<std::string> reload(nlohmann::json const& doc);

    /// \brief Stop the live graph, if any.
    void stop();

    /// \brief Install the callback used to stream live port values to clients.
    void set_live_publish(LivePublishFn fn) { live_publish_ = std::move(fn); }

    /// \brief Raw pointer to the live graph; caller must hold mu().
    Graph* graph_unlocked() { return graph_.get(); }
    /// \brief The mutex guarding the live graph.
    std::mutex& mu() { return mu_; }

private:
    std::mutex mu_;
    std::unique_ptr<Graph> graph_;
    LivePublishFn live_publish_;
};

}  // namespace flowboard
