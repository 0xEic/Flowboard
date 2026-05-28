// SPDX-License-Identifier: MIT
#include "flowboard/graph.hpp"
#include "builtin_types.hpp"
#include "flowboard/wire_registry.hpp"
#include <set>
#include <stdexcept>
#include <sstream>

namespace flowboard {

void Graph::add_node(std::unique_ptr<Node> node) {
    by_id_[std::string(node->id())] = node.get();
    nodes_.push_back(std::move(node));
}

Node* Graph::node(std::string_view id) const {
    auto it = by_id_.find(std::string(id));
    return it == by_id_.end() ? nullptr : it->second;
}

std::string Graph::connect_checked(std::string_view src_id, std::string_view src_port,
                                   std::string_view dst_id, std::string_view dst_port,
                                   std::size_t capacity, BackpressurePolicy policy) {
    (void)capacity; (void)policy;
    auto* src = node(src_id);
    auto* dst = node(dst_id);
    if (!src) return std::string("unknown source node: ") + std::string(src_id);
    if (!dst) return std::string("unknown destination node: ") + std::string(dst_id);

    auto* sport = src->output(src_port);
    auto* dport = dst->input(dst_port);
    if (!sport) return std::string("unknown output port: ")
                       + std::string(src_id) + "." + std::string(src_port);
    if (!dport) return std::string("unknown input port: ")
                       + std::string(dst_id) + "." + std::string(dst_port);
    if (sport->type_tag() != dport->type_tag()) {
        // Any-typed output -> Bool input is a legitimate "signal" edge: it fires
        // a `true` pulse on every source emission (a common trigger-on-arrival
        // pattern). Anything else is a real type mismatch.
        if (dport->type_tag() == "flowboard::Bool") return {};
        std::ostringstream os;
        os << "type mismatch on edge "
           << src_id << "." << src_port << " (" << sport->type_tag() << ") -> "
           << dst_id << "." << dst_port << " (" << dport->type_tag() << ")";
        return os.str();
    }
    return {};
}

void Graph::connect(std::string_view src_id, std::string_view src_port,
                    std::string_view dst_id, std::string_view dst_port,
                    std::size_t capacity, BackpressurePolicy policy) {
    auto err = connect_checked(src_id, src_port, dst_id, dst_port, capacity, policy);
    if (!err.empty()) throw std::runtime_error(err);

    auto* src   = node(src_id);
    auto* dst   = node(dst_id);
    auto* sport = src->output(src_port);
    auto* dport = dst->input(dst_port);

    // Signal edge: any-typed output -> Bool input (validated above). Each source
    // emission becomes a `true` pulse; no per-type wire factory is needed.
    if (sport->type_tag() != dport->type_tag()) {
        wire_signal_edge(sport, dport,
                         std::string(src_id), std::string(src_port),
                         std::string(dst_id), std::string(dst_port),
                         capacity, policy);
        return;
    }

    auto tag = sport->type_tag();
    auto fn = lookup_wire_factory(tag);
    if (!fn) throw std::runtime_error(
        std::string("connect: unsupported type tag at runtime: ") + std::string(tag) +
        " (no wire factory registered — check that the generating module's "
        "OP_REGISTER_WIRE_FACTORY is reachable at link time)");

    fn(*this, sport, dport,
       std::string(src_id), std::string(src_port),
       std::string(dst_id), std::string(dst_port),
       capacity, policy);
}

void Graph::wire_signal_edge(IOutputPort* sport, IInputPort* dport,
                             std::string src_node_id, std::string src_port_name,
                             std::string dst_node_id, std::string dst_port_name,
                             std::size_t capacity, BackpressurePolicy policy) {
    auto holder = std::make_unique<EdgeHolder<bool>>(capacity, policy);
    holder->src_node = std::move(src_node_id);
    holder->src_port = std::move(src_port_name);
    holder->dst_node = std::move(dst_node_id);
    holder->dst_port = std::move(dst_port_name);

    auto* edge = &holder->edge;
    auto* bin  = static_cast<InputPort<bool>*>(dport);
    holder->deliver = [bin](std::shared_ptr<const bool> v) { bin->deliver(std::move(v)); };
    // Ignore the source value; every emission is a `true` pulse.
    sport->attach_signal_sink([edge] { edge->push(std::make_shared<const bool>(true)); });
    edges_.push_back(std::move(holder));
}

void Graph::start() {
    for (auto& n : nodes_) n->start();
    for (auto& h : edges_) h->start_pump();

    // Seed inline defaults into primitive input ports that have no incoming
    // edge. Done after start so node sinks (set in on_start) are wired.
    std::set<std::string> connected;
    for (auto& h : edges_) connected.insert(h->dst_node + "." + h->dst_port);
    for (auto& n : nodes_) {
        std::string id(n->id());
        n->seed_input_defaults([&](std::string const& port) {
            return connected.count(id + "." + port) > 0;
        });
    }
}

void Graph::stop() {
    // Stop nodes first (producers stop emitting; consumers stop processing).
    // Then drain pumps — remaining ring items are forwarded to consumer queues
    // after node workers have joined (those items go out with destruction).
    for (auto& n : nodes_) n->stop();
    for (auto& h : edges_) h->stop_pump();
}

std::vector<Graph::EdgeInfo> Graph::edge_stats() const {
    std::vector<EdgeInfo> out;
    out.reserve(edges_.size());
    for (auto const& h : edges_) {
        EdgeInfo ei;
        ei.src_node = h->src_node;
        ei.src_port = h->src_port;
        ei.dst_node = h->dst_node;
        ei.dst_port = h->dst_port;
        ei.pushed   = h->pushed();
        ei.dropped  = h->dropped();
        out.push_back(std::move(ei));
    }
    return out;
}

}  // namespace flowboard
