// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include "flowboard/log.hpp"
#include "flowboard/port.hpp"

/// \file
/// \brief Base class for every processing element in a graph.

namespace flowboard {

/// \brief A processing element with its own worker thread.
///
/// Each node owns a `std::jthread` worker fed by a counting semaphore: port
/// sinks call enqueue(), the worker wakes and runs the queued task. Per-node
/// threading isolates failures and lets independent stages run in parallel.
///
/// Subclasses register their ports in the constructor (register_input() /
/// register_output()) and override on_start() / on_stop(). Nodes are
/// non-copyable; the destructor stops the worker.
class Node {
public:
    /// \brief Construct a node.
    /// \param id        Graph-unique node id (from the JSON config).
    /// \param type_name Registry type name (e.g. `"Transform.Threshold"`).
    Node(std::string id, std::string type_name)
        : id_(std::move(id)), type_name_(std::move(type_name)) {}

    virtual ~Node() { stop(); }

    Node(Node const&) = delete;
    Node& operator=(Node const&) = delete;

    /// \brief This node's graph-unique id.
    std::string_view id()        const { return id_; }
    /// \brief This node's registry type name.
    std::string_view type_name() const { return type_name_; }

    /// \brief Look up an input port by name, or `nullptr` if absent.
    IInputPort*  input (std::string_view name) const;
    /// \brief Look up an output port by name, or `nullptr` if absent.
    IOutputPort* output(std::string_view name) const;

    /// \brief All input ports, keyed by name.
    std::unordered_map<std::string, IInputPort*>  const& inputs()  const { return inputs_; }
    /// \brief All output ports, keyed by name.
    std::unordered_map<std::string, IOutputPort*> const& outputs() const { return outputs_; }

    /// \brief Start the worker thread.
    void start();
    /// \brief Stop and join the worker thread (idempotent).
    void stop();

    /// \brief Provide JSON default values for unconnected primitive inputs.
    ///
    /// Keyed by port name (from the config's `"defaults"` block); seeded once at
    /// graph start for inputs that have no incoming edge.
    void set_input_defaults(nlohmann::json defaults) { input_defaults_ = std::move(defaults); }
    /// \brief Seed defaults into inputs that are not connected.
    /// \param is_connected Predicate returning whether a port name has an edge.
    void seed_input_defaults(std::function<bool(std::string const&)> const& is_connected);

    /// \brief Pause the worker loop (messages still queue).
    void pause()           { paused_.store(true); }
    /// \brief Resume a paused worker.
    void resume()          { paused_.store(false); event_.release(); }
    /// \brief Whether the worker is currently paused.
    bool is_paused() const { return paused_.load(); }

    /// \brief Enqueue a task onto the worker queue (called from port sinks).
    void enqueue(std::function<void()> task);

protected:
    /// \brief Register an input port so the graph can find and wire it.
    template <typename T>
    void register_input(InputPort<T>* port) {
        inputs_.emplace(std::string(port->name()), port);
    }
    /// \brief Register an output port so the graph can find and wire it.
    template <typename T>
    void register_output(OutputPort<T>* port) {
        outputs_.emplace(std::string(port->name()), port);
    }

    /// \brief Hook run when the node starts; wire input sinks here.
    virtual void on_start() {}
    /// \brief Hook run when the node stops.
    virtual void on_stop()  {}

private:
    void worker_loop(std::stop_token st);

    std::string id_;
    std::string type_name_;
    nlohmann::json input_defaults_;
    std::unordered_map<std::string, IInputPort*>  inputs_;
    std::unordered_map<std::string, IOutputPort*> outputs_;

    using NodeEvent = std::counting_semaphore<std::numeric_limits<std::ptrdiff_t>::max()>;

    std::mutex queue_mu_;
    std::queue<std::function<void()>> queue_;
    NodeEvent event_{0};
    std::jthread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};

}  // namespace flowboard
