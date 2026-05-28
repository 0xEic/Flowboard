// SPDX-License-Identifier: MIT
#include "flowboard/loader.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/log.hpp"
#include "flowboard/reliability.hpp"
#include <sstream>

namespace flowboard::config {

namespace {

struct PortRef { std::string node_id; std::string port_name; };

bool parse_port_ref(std::string_view s, PortRef& out, std::string& err) {
    auto dot = s.find('.');
    if (dot == std::string_view::npos) {
        err = std::string("malformed port reference: ") + std::string(s);
        return false;
    }
    out.node_id   = std::string(s.substr(0, dot));
    out.port_name = std::string(s.substr(dot + 1));
    return true;
}

BackpressurePolicy parse_policy(std::string const& s, std::string& err) {
    if (s == "block")        return BackpressurePolicy::Block;
    if (s == "drop_oldest")  return BackpressurePolicy::DropOldest;
    err = "unknown backpressure policy: " + s;
    return BackpressurePolicy::Block;
}

}  // namespace

LoadResult load(nlohmann::json const& doc) {
    LoadResult result;

    if (!doc.is_object()) {
        result.errors.emplace_back("root must be an object");
        return result;
    }
    auto version = doc.value("version", std::string{});
    if (version != "1.0") {
        result.errors.push_back("unsupported version (expected \"1.0\", got \"" + version + "\")");
        return result;
    }
    auto& reg = NodeRegistry::instance();
    auto graph = std::make_unique<Graph>();

    if (!doc.contains("nodes") || !doc["nodes"].is_array()) {
        result.errors.emplace_back("missing or invalid \"nodes\" array");
        return result;
    }
    for (auto const& entry : doc["nodes"]) {
        auto id   = entry.value("id",   std::string{});
        auto type = entry.value("type", std::string{});
        auto cfg  = entry.value("config", nlohmann::json::object());
        if (id.empty() || type.empty()) {
            result.errors.push_back("node entry missing id or type");
            continue;
        }
        // Canvas-only annotation / grouping nodes have no engine representation.
        // The web UI strips them before reload; tolerate them here too so a graph
        // authored in the editor (e.g. with Note comments) loads directly via CLI.
        if (type == "Note" || type == "Flow.Group" ||
            type == "Group.Input" || type == "Group.Output") {
            continue;
        }
        std::unique_ptr<Node> node;
        try {
            node = reg.create(type, id, cfg);
        } catch (std::exception const& e) {
            result.errors.push_back("node " + id + " (" + type + "): " + e.what());
            continue;
        }
        if (!node) {
            result.errors.push_back("unknown node type '" + type + "' for node '" + id + "'");
            continue;
        }
        // Inline per-port defaults for unconnected primitive inputs.
        if (auto it = cfg.find("defaults"); it != cfg.end() && it->is_object())
            node->set_input_defaults(*it);
        graph->add_node(std::move(node));
    }

    // edges key is optional; if present it must be an array
    if (doc.contains("edges") && !doc["edges"].is_array()) {
        result.errors.emplace_back("\"edges\" must be an array");
        return result;
    }
    auto const& edges_arr = doc.contains("edges") ? doc["edges"]
                                                   : nlohmann::json::array();
    for (auto const& e : edges_arr) {
        auto from = e.value("from", std::string{});
        auto to   = e.value("to",   std::string{});
        auto cap  = e.value("capacity", static_cast<std::size_t>(256));
        std::string perr;
        PortRef src_ref, dst_ref;
        if (!parse_port_ref(from, src_ref, perr)) { result.errors.push_back(perr); continue; }
        if (!parse_port_ref(to,   dst_ref, perr)) { result.errors.push_back(perr); continue; }
        // Explicit policy wins; otherwise derive from the source port's op prefix.
        BackpressurePolicy policy;
        if (e.contains("policy")) {
            policy = parse_policy(e.value("policy", std::string{"drop_oldest"}), perr);
            if (!perr.empty()) { result.errors.push_back(perr); continue; }
        } else {
            policy = default_edge_policy(src_ref.port_name);
        }
        auto err = graph->connect_checked(src_ref.node_id, src_ref.port_name,
                                          dst_ref.node_id, dst_ref.port_name,
                                          cap, policy);
        if (!err.empty()) { result.errors.push_back(err); continue; }
        try {
            graph->connect(src_ref.node_id, src_ref.port_name,
                           dst_ref.node_id, dst_ref.port_name, cap, policy);
        } catch (std::exception const& ex) {
            result.errors.emplace_back(std::string("connect: ") + ex.what());
        }
    }
    if (!result.errors.empty()) return result;
    graph->set_source_json(doc);
    result.graph = std::move(graph);
    return result;
}

}  // namespace flowboard::config
