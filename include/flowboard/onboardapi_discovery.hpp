// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace flowboard {

/// \file
/// \brief Process-wide registry and live-bus discovery of OnboardAPI Service/Client endpoints.

/// \brief One OnboardAPI endpoint: a Service or Client interface bound to a
/// (domainId, serviceName) on a given interface type (e.g. "Mount").
///
/// The first four fields are always populated. The remaining fields carry
/// extra metadata that is only available for endpoints discovered live on the
/// DDS bus (via the SDK MonitorSession); they stay empty/0 for endpoints that
/// are merely present as nodes in the loaded graph.
struct DiscoveredEndpoint {
    int         domain_id{};
    std::string interface_type;   // "Mount", "Alert", …  (no "M_" prefix)
    std::string direction;        // "Service" | "Client"
    std::string service_name;
    // live-only enrichment:
    std::string host_name;
    std::string proc_name;
    std::int32_t process_id{};
    std::string actor_id;
};

/// \brief An endpoint after merging the graph and live views, tagged with where it was
/// seen: "graph" (a node in the loaded graph), "live" (on the DDS bus only), or
/// "both".
struct ResolvedEndpoint {
    DiscoveredEndpoint ep;
    std::string        source;
};

/// \brief Serialises a flowboard::ResolvedEndpoint to the JSON shape shared by the
/// OnboardApi.Discovery node's list items and the /api/discovery control-plane endpoint.
///
/// JSON shape:
///   { interfaceType, direction, serviceName, domainId, source,
///     [hostName, procName, processId, actorId] }   (live-only fields omitted
///   when empty).
::nlohmann::json to_json(ResolvedEndpoint const& r);

/// \brief Process-wide registry of currently open OnboardAPI Service/Client endpoints.
///
/// Generated M_Foo_{Service,Client}_Node instances `open()` themselves here on
/// construction and `close()` on destruction (see Service/ClientNodeBase), so
/// the snapshot reflects the endpoints present in the loaded graph. The
/// `OnboardApi.Discovery` node reads it to emit a list of available services.
class OnboardApiDiscovery {
public:
    using Token = std::uint64_t;

    static OnboardApiDiscovery& instance();

    /// \brief Registers an open endpoint; returns a token used to close it. Token 0 is
    /// never handed out and is treated as "not registered" by close().
    Token open(int domain_id, std::string interface_type,
               std::string direction, std::string service_name);
    void  close(Token token);

    std::vector<DiscoveredEndpoint> snapshot() const;              // all domains
    std::vector<DiscoveredEndpoint> snapshot(int domain_id) const; // one domain

    /// \brief Live discovery: enumerate endpoints actually present on the DDS bus for a
    /// domain — including ones that are NOT nodes in our graph. The real SDK
    /// backend installs a scanner at startup (via the SDK MonitorSession); under
    /// the stub SDK no scanner is installed and scan_live() returns empty.
    using LiveScanFn = std::function<std::vector<DiscoveredEndpoint>(int domain_id)>;
    void set_live_scanner(LiveScanFn fn);
    bool has_live_scanner() const;
    std::vector<DiscoveredEndpoint> scan_live(int domain_id) const;

    /// \brief Merges the graph and/or live views into a de-duplicated, sorted list.
    /// `source` is "graph", "live", or "both". For "both"/"live" the live scan
    /// is always per `domain_id` (the DDS MonitorSession is per-domain); the
    /// graph view honours `all_domains`.
    std::vector<ResolvedEndpoint> resolve(int domain_id, bool all_domains,
                                          std::string_view source) const;

private:
    OnboardApiDiscovery() = default;
    mutable std::mutex                            mu_;
    std::unordered_map<Token, DiscoveredEndpoint> open_;
    Token                                         next_{1};
    LiveScanFn                                    live_scanner_;
};

/// \brief "M_Mount.Service" -> "Mount". Strips a leading "M_" and anything from the
/// first '.' onward. Returns the input unchanged if neither marker is present.
inline std::string onboardapi_interface_type(std::string_view type_name) {
    std::string_view s = type_name;
    if (s.size() >= 2 && s[0] == 'M' && s[1] == '_') s.remove_prefix(2);
    if (auto dot = s.find('.'); dot != std::string_view::npos) s = s.substr(0, dot);
    return std::string(s);
}

}  // namespace flowboard
