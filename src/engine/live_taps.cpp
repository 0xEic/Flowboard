// SPDX-License-Identifier: MIT
#include "flowboard/live_taps.hpp"
#include "flowboard/graph.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/tap_registry.hpp"

namespace flowboard {

// Attach a live-value sink to every output port whose type tag has a registered
// tap factory. Primitives + ListValue are registered in builtin_types.cpp;
// every onboardapi struct registers one from its generated *_nodes.cpp. Ports
// whose type has no factory (e.g. as-yet-unregistered types) are simply not
// tapped rather than silently dropped at a hardcoded switch.
void attach_live_taps(Graph& g, LivePublishFn fn) {
    for (auto const& uptr : g.nodes()) {
        for (auto const& [name, port] : uptr->outputs()) {
            (void)name;
            auto f = lookup_tap_factory(port->type_tag());
            if (!f) continue;
            std::string key = std::string(uptr->id()) + "." + std::string(port->name());
            f(port, key, fn);
        }
    }
}

}  // namespace flowboard
