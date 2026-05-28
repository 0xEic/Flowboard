// SPDX-License-Identifier: MIT
#include "serialization.hpp"
#include "flowboard/control_plane.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/graph.hpp"
#include "flowboard/node.hpp"
#include "flowboard/log.hpp"

namespace flowboard::control {

std::string catalog_json_string() { return catalog_to_json().dump(2); }

namespace {

nlohmann::json port_to_json_in(IInputPort const& p) {
    return { {"name", std::string(p.name())},
             {"typeTag", std::string(p.type_tag())},
             {"direction", "input"} };
}
nlohmann::json port_to_json_out(IOutputPort const& p) {
    return { {"name", std::string(p.name())},
             {"typeTag", std::string(p.type_tag())},
             {"direction", "output"} };
}

}  // namespace

nlohmann::json catalog_to_json() {
    auto& reg = NodeRegistry::instance();
    nlohmann::json out = nlohmann::json::array();
    for (auto const& name : reg.registered_names()) {
        auto schema_opt = reg.schema_for(name);
        nlohmann::json defaults_obj = nlohmann::json::object();
        if (schema_opt && !schema_opt->defaults_json.empty()) {
            try { defaults_obj = nlohmann::json::parse(schema_opt->defaults_json); }
            catch (...) { /* fall through with empty defaults */ }
        }

        // Probe with defaults first; on failure fall back to empty.
        std::unique_ptr<Node> probe;
        try {
            probe = reg.create(name, "__probe__", defaults_obj);
        } catch (std::exception const&) {
            try { probe = reg.create(name, "__probe__", nlohmann::json::object()); }
            catch (std::exception const& e) {
                spdlog::warn("catalog: cannot probe {} even with defaults ({})", name, e.what());
            }
        }

        nlohmann::json entry = {
            {"typeName", name},
            {"inputs",  nlohmann::json::array()},
            {"outputs", nlohmann::json::array()},
        };
        if (probe) {
            for (auto const& [k, v] : probe->inputs())  entry["inputs"] .push_back(port_to_json_in(*v));
            for (auto const& [k, v] : probe->outputs()) entry["outputs"].push_back(port_to_json_out(*v));
        }
        if (schema_opt) {
            try { entry["configSchema"] = nlohmann::json::parse(schema_opt->schema_json); }
            catch (...) { entry["configSchema"] = nlohmann::json::object(); }
            entry["configDefaults"] = defaults_obj;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

nlohmann::json graph_to_json(Graph const& g) {
    nlohmann::json nodes = nlohmann::json::array();
    for (auto const& uptr : g.nodes()) {
        nlohmann::json n = {
            {"id",       std::string(uptr->id())},
            {"type",     std::string(uptr->type_name())},
            {"inputs",   nlohmann::json::array()},
            {"outputs",  nlohmann::json::array()},
        };
        for (auto const& [k, v] : uptr->inputs())  n["inputs"] .push_back(port_to_json_in(*v));
        for (auto const& [k, v] : uptr->outputs()) n["outputs"].push_back(port_to_json_out(*v));
        nodes.push_back(std::move(n));
    }
    return { {"version", "1.0"}, {"nodes", nodes}, {"edges", nlohmann::json::array()} };
}

nlohmann::json stats_to_json(Graph const& g) {
    nlohmann::json edges = nlohmann::json::array();
    for (auto const& e : g.edge_stats()) {
        edges.push_back({
            {"from",    e.src_node + "." + e.src_port},
            {"to",      e.dst_node + "." + e.dst_port},
            {"pushed",  e.pushed},
            {"dropped", e.dropped},
        });
    }
    return { {"edges", std::move(edges)} };
}

}  // namespace flowboard::control
