// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace flowboard {

/// \file
/// \brief HTTP control-plane server that exposes a running graph to the web UI.

class Graph;
class GraphHolder;

namespace control {

/// \brief The node-type catalog (every registered node with its port shape,
/// config schema, and defaults) serialized as a pretty-printed JSON string.
///
/// Pure registry introspection — used by the `--dump-nodes` CLI flag and the
/// node-documentation generator so the docs are derived from the real registry.
std::string catalog_json_string();

/// \brief Configuration for the control-plane server's bind address, port, and asset serving.
struct Config {
    std::string   bind_host    = "127.0.0.1";
    std::uint16_t port         = 8765;
    bool          serve_assets = true;
};

/// \brief HTTP server that attaches to a flowboard::GraphHolder and serves the control plane.
class Server {
public:
    Server();
    ~Server();
    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;

    /// \brief Bind the graph holder the server will inspect and control.
    void attach_holder(GraphHolder* holder);
    /// \brief Start listening using the given configuration.
    void start(Config const& cfg);
    /// \brief Stop listening and shut the server down.
    void stop();

    bool running() const { return running_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool>     running_{false};
};

}  // namespace control
}  // namespace flowboard
