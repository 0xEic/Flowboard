// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "flowboard/edge.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

/// \file
/// \brief Owns the nodes and edges of a workflow and drives its lifecycle.

namespace flowboard {

/// \brief A runnable workflow: a set of nodes connected by type-checked edges.
///
/// The graph owns every Node and every edge. Edges are templated on their
/// message type, so the graph stores them behind a type-erased holder and
/// builds them through wire_typed_edge(). When started, each edge gets its own
/// pump `std::jthread` that drains the SPSC ring into the consumer's input.
class Graph {
public:
    /// \brief Take ownership of a node and add it to the graph.
    void add_node(std::unique_ptr<Node> node);

    /// \brief Connect two ports by name, creating a buffered edge.
    /// \param src_node Source node id.
    /// \param src_port Output port name on the source node.
    /// \param dst_node Destination node id.
    /// \param dst_port Input port name on the destination node.
    /// \param capacity Ring capacity for the edge.
    /// \param policy   Backpressure policy for the edge.
    /// \throws std::runtime_error on a type-tag mismatch or an unknown port.
    void connect(std::string_view src_node, std::string_view src_port,
                 std::string_view dst_node, std::string_view dst_port,
                 std::size_t capacity, BackpressurePolicy policy);

    /// \brief Non-throwing variant of connect().
    /// \return Empty string on success, otherwise a human-readable error.
    std::string connect_checked(std::string_view src_node, std::string_view src_port,
                                std::string_view dst_node, std::string_view dst_port,
                                std::size_t capacity, BackpressurePolicy policy);

    /// \brief Build and store an edge of element type `T` between two resolved ports.
    ///
    /// Public so the wire-factory registry (`wire_registry`) can drive edge
    /// construction from a runtime type tag without Graph knowing every struct
    /// type up front. `pipegen` registers a wire factory per struct alongside
    /// its log/extract factories.
    /// \tparam T The message type carried by the edge.
    template <typename T>
    void wire_typed_edge(IOutputPort* sport, IInputPort* dport,
                         std::string src_node_id, std::string src_port_name,
                         std::string dst_node_id, std::string dst_port_name,
                         std::size_t capacity, BackpressurePolicy policy);

    /// \brief Wire a "signal" edge: any-typed output → a Bool input.
    ///
    /// Carries no data — every emission from \p sport (of any value) pushes a
    /// single `true` onto a Bool edge delivered to \p dport. Used to trigger a
    /// node when data arrives on an upstream port. \p dport must be a Bool input.
    void wire_signal_edge(IOutputPort* sport, IInputPort* dport,
                          std::string src_node_id, std::string src_port_name,
                          std::string dst_node_id, std::string dst_port_name,
                          std::size_t capacity, BackpressurePolicy policy);

    /// \brief Start every node worker and every edge pump thread.
    void start();
    /// \brief Stop every pump and node worker, draining edges so producers unblock.
    void stop();

    /// \brief Look up a node by id, or `nullptr` if absent.
    Node* node(std::string_view id) const;

    /// \brief A snapshot of one edge's endpoints and counters.
    struct EdgeInfo {
        std::string src_node, src_port;  ///< Source node id and port name.
        std::string dst_node, dst_port;  ///< Destination node id and port name.
        std::size_t pushed  = 0;         ///< Messages pushed onto the edge.
        std::size_t dropped = 0;         ///< Messages dropped (DropOldest policy).
    };

    /// \brief Per-edge pushed/dropped counters (backs the `/api/stats` endpoint).
    std::vector<EdgeInfo> edge_stats() const;

    /// \brief All nodes owned by this graph.
    std::vector<std::unique_ptr<Node>> const& nodes() const { return nodes_; }

    /// \brief Store the JSON document this graph was built from.
    void set_source_json(nlohmann::json doc) { source_json_ = std::move(doc); }
    /// \brief The JSON document this graph was built from (backs `GET /api/graph`).
    nlohmann::json const& source_json() const { return source_json_; }

private:
    // Type-erased edge holder so Graph can own edges of any T.
    struct IEdgeHolder {
        virtual ~IEdgeHolder() = default;
        virtual std::size_t pushed()  const = 0;
        virtual std::size_t dropped() const = 0;
        virtual void start_pump() = 0;
        virtual void stop_pump()  = 0;
        std::string src_node, src_port, dst_node, dst_port;
    };
    template <typename T>
    struct EdgeHolder : IEdgeHolder {
        EdgeHolder(std::size_t cap, BackpressurePolicy pol)
            : edge(cap, pol, /*event=*/nullptr) {}
        std::size_t pushed()  const override { return edge.pushed(); }
        std::size_t dropped() const override { return edge.dropped(); }

        void start_pump() override {
            pump = std::jthread([this](std::stop_token st) {
                std::shared_ptr<const T> v;
                using namespace std::chrono_literals;
                while (!st.stop_requested()) {
                    if (edge.try_pop(v)) {
                        if (deliver) deliver(std::move(v));
                    } else {
                        std::this_thread::sleep_for(50us);
                    }
                }
                // Drain any remaining items so producers aren't blocked at shutdown.
                while (edge.try_pop(v)) if (deliver) deliver(std::move(v));
            });
        }
        void stop_pump() override {
            pump.request_stop();
            if (pump.joinable()) pump.join();
        }

        Edge<T> edge;
        std::function<void(std::shared_ptr<const T>)> deliver;
        std::jthread pump;
    };

    std::vector<std::unique_ptr<Node>> nodes_;
    std::unordered_map<std::string, Node*> by_id_;
    std::vector<std::unique_ptr<IEdgeHolder>> edges_;
    nlohmann::json source_json_;
};

template <typename T>
inline void Graph::wire_typed_edge(IOutputPort* sport, IInputPort* dport,
                                   std::string src_node_id, std::string src_port_name,
                                   std::string dst_node_id, std::string dst_port_name,
                                   std::size_t capacity, BackpressurePolicy policy) {
    auto holder = std::make_unique<EdgeHolder<T>>(capacity, policy);
    holder->src_node = std::move(src_node_id);
    holder->src_port = std::move(src_port_name);
    holder->dst_node = std::move(dst_node_id);
    holder->dst_port = std::move(dst_port_name);

    auto* edge = &holder->edge;
    auto* sout = static_cast<OutputPort<T>*>(sport);
    auto* sin  = static_cast<InputPort<T>*>(dport);
    holder->deliver = [sin](std::shared_ptr<const T> v) { sin->deliver(std::move(v)); };
    sout->attach_sink([edge](std::shared_ptr<const T> v) { edge->push(std::move(v)); });
    edges_.push_back(std::move(holder));
}

}  // namespace flowboard
