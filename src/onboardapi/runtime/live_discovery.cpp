// SPDX-License-Identifier: MIT
//
// Real-SDK backend for OnboardApiDiscovery live scanning. Uses the SDK's
// ddkit MonitorSession to enumerate the Service/Client endpoints actually
// present on the DDS bus for a domain — including ones that are not nodes in
// our graph. Installs itself as the live scanner at static-init time.
//
// Compiled into onboardapi_generated. Active only in real-SDK builds; under
// the stub SDK (FLOWBOARD_REAL_SDK undefined) this translation unit is
// intentionally empty, so OnboardApiDiscovery::scan_live() stays a no-op.
#if defined(FLOWBOARD_REAL_SDK)

#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <ddkit/actors/MonitorSession.hpp>
#include <ddkit/actors/SessionFactory.hpp>

#include "flowboard/onboardapi_discovery.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::onboardapi {
namespace {

// Distinct OnboardAPI module strings ("M_Mount", "M_Alert", …) derived from the
// generated Service/Client node type names registered in the catalog. This
// keeps the scan in sync with whatever interfaces pipegen emitted.
std::set<std::string> known_modules() {
    std::set<std::string> mods;
    for (auto const& name : NodeRegistry::instance().registered_names()) {
        if (name.rfind("M_", 0) != 0) continue;
        auto dot = name.rfind('.');
        if (dot == std::string::npos) continue;
        auto suffix = name.substr(dot + 1);
        if (suffix == "Service" || suffix == "Client")
            mods.insert(name.substr(0, dot));
    }
    return mods;
}

DiscoveredEndpoint to_endpoint(ddkit::actors::ActorInfo const& info,
                               std::string const& direction) {
    DiscoveredEndpoint e;
    e.domain_id      = info.config.domain;
    e.interface_type = onboardapi_interface_type(info.config.module);
    e.direction      = direction;
    e.service_name   = info.config.serviceName;
    e.host_name      = info.hostName;
    e.proc_name      = info.procName;
    e.process_id     = info.processId;
    e.actor_id       = info.actorId;
    return e;
}

// One MonitorSession per domain, kept alive for the process lifetime.
//
// This is essential for discovery to work: the SDK SessionFactory only holds a
// weak_ptr, so a session that is created and dropped per scan starts DDS
// discovery from scratch every time. findServices() then returns only the
// participants discovery has seen so far — effectively just this process's own
// (same-host) actors, never the remote ones that need the discovery handshake
// to complete. Holding the session alive keeps discovery warm so remote
// Services/Clients show up on subsequent scans.
std::shared_ptr<ddkit::actors::MonitorSession> session_for(int domain_id) {
    static std::mutex mu;
    static std::map<int, std::shared_ptr<ddkit::actors::MonitorSession>> sessions;
    std::scoped_lock l(mu);
    auto& s = sessions[domain_id];
    if (!s) s = ddkit::actors::SessionFactory::instance().getMonitorSession(domain_id);
    return s;
}

std::vector<DiscoveredEndpoint> scan(int domain_id) {
    // Serialize scans: scan_live() may be invoked from both the control-plane
    // HTTP thread and the OnboardApi.Discovery node's worker thread, and the
    // SDK MonitorSession is not assumed thread-safe.
    static std::mutex scan_mu;
    std::scoped_lock scan_lock(scan_mu);

    std::vector<DiscoveredEndpoint> out;
    try {
        auto session = session_for(domain_id);
        if (!session) return out;
        for (auto const& mod : known_modules()) {
            try {
                for (auto const& s : session->findServices(mod))
                    out.push_back(to_endpoint(s, "Service"));
                for (auto const& c : session->findClients(mod))
                    out.push_back(to_endpoint(c, "Client"));
            } catch (std::exception const& e) {
                spdlog::warn("OnboardApi.Discovery: live scan of module '{}' on domain {} failed: {}",
                             mod, domain_id, e.what());
            }
        }
    } catch (std::exception const& e) {
        spdlog::warn("OnboardApi.Discovery: live scan on domain {} failed: {}", domain_id, e.what());
    }
    return out;
}

struct Installer {
    Installer() { OnboardApiDiscovery::instance().set_live_scanner(&scan); }
};
Installer const _installer{};

}  // namespace
}  // namespace flowboard::onboardapi

#endif  // FLOWBOARD_REAL_SDK
