// SPDX-License-Identifier: MIT
#include "flowboard/onboardapi_discovery.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace flowboard {

OnboardApiDiscovery& OnboardApiDiscovery::instance() {
    static OnboardApiDiscovery d;
    return d;
}

OnboardApiDiscovery::Token OnboardApiDiscovery::open(int domain_id,
                                                     std::string interface_type,
                                                     std::string direction,
                                                     std::string service_name) {
    std::scoped_lock l(mu_);
    Token t = next_++;
    open_.emplace(t, DiscoveredEndpoint{domain_id, std::move(interface_type),
                                        std::move(direction), std::move(service_name)});
    return t;
}

void OnboardApiDiscovery::close(Token token) {
    if (token == 0) return;
    std::scoped_lock l(mu_);
    open_.erase(token);
}

std::vector<DiscoveredEndpoint> OnboardApiDiscovery::snapshot() const {
    std::scoped_lock l(mu_);
    std::vector<DiscoveredEndpoint> out;
    out.reserve(open_.size());
    for (auto const& [t, e] : open_) out.push_back(e);
    return out;
}

std::vector<DiscoveredEndpoint> OnboardApiDiscovery::snapshot(int domain_id) const {
    std::scoped_lock l(mu_);
    std::vector<DiscoveredEndpoint> out;
    for (auto const& [t, e] : open_)
        if (e.domain_id == domain_id) out.push_back(e);
    return out;
}

void OnboardApiDiscovery::set_live_scanner(LiveScanFn fn) {
    std::scoped_lock l(mu_);
    live_scanner_ = std::move(fn);
}

bool OnboardApiDiscovery::has_live_scanner() const {
    std::scoped_lock l(mu_);
    return static_cast<bool>(live_scanner_);
}

std::vector<DiscoveredEndpoint> OnboardApiDiscovery::scan_live(int domain_id) const {
    LiveScanFn fn;
    {
        std::scoped_lock l(mu_);
        fn = live_scanner_;
    }
    if (!fn) return {};
    return fn(domain_id);  // called outside the lock: the SDK scan may block on DDS
}

std::vector<ResolvedEndpoint> OnboardApiDiscovery::resolve(int domain_id, bool all_domains,
                                                           std::string_view source) const {
    struct Key {
        int domain; std::string iface, direction, service;
        bool operator<(Key const& o) const {
            return std::tie(domain, iface, direction, service)
                 < std::tie(o.domain, o.iface, o.direction, o.service);
        }
    };
    auto key = [](DiscoveredEndpoint const& e) {
        return Key{e.domain_id, e.interface_type, e.direction, e.service_name};
    };

    struct Merged { DiscoveredEndpoint ep; bool in_graph{false}, in_live{false}; };
    std::map<Key, Merged> merged;

    if (source != "live") {
        auto graph = all_domains ? snapshot() : snapshot(domain_id);
        for (auto& e : graph) {
            auto& m = merged[key(e)];
            m.ep = e;
            m.in_graph = true;
        }
    }
    if (source != "graph") {
        for (auto& e : scan_live(domain_id)) {
            auto& m = merged[key(e)];
            if (!m.in_graph) {
                m.ep = e;
            } else {  // keep graph's identity, take live's enrichment
                m.ep.host_name  = e.host_name;
                m.ep.proc_name  = e.proc_name;
                m.ep.process_id = e.process_id;
                m.ep.actor_id   = e.actor_id;
            }
            m.in_live = true;
        }
    }

    std::vector<ResolvedEndpoint> out;
    out.reserve(merged.size());
    for (auto const& [k, m] : merged) {
        out.push_back({m.ep, (m.in_graph && m.in_live) ? "both"
                            : m.in_live                 ? "live"
                                                        : "graph"});
    }
    return out;  // already sorted by std::map key order
}

::nlohmann::json to_json(ResolvedEndpoint const& r) {
    ::nlohmann::json item;
    item["interfaceType"] = r.ep.interface_type;
    item["direction"]     = r.ep.direction;
    item["serviceName"]   = r.ep.service_name;
    item["domainId"]      = r.ep.domain_id;
    item["source"]        = r.source;
    if (!r.ep.actor_id.empty() || !r.ep.host_name.empty()) {
        item["hostName"]  = r.ep.host_name;
        item["procName"]  = r.ep.proc_name;
        item["processId"] = r.ep.process_id;
        item["actorId"]   = r.ep.actor_id;
    }
    return item;
}

}  // namespace flowboard
