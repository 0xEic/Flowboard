// SPDX-License-Identifier: MIT
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/node.hpp"
#include "flowboard/onboardapi_discovery.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Emits the list of OnboardAPI Service/Client endpoints when its `trigger`
// fires. Each list item is a JSON object:
//   { "interfaceType": "Mount", "direction": "Service"|"Client",
//     "serviceName": "...", "domainId": N, "source": "graph"|"live"|"both" }
// Live items additionally carry "hostName", "procName", "processId", "actorId".
// The list flows on an `flowboard::List` port so it composes with the
// Transform.List.* nodes.
//
// `source` config selects where endpoints come from:
//   - "graph" : the Service/Client nodes present in the loaded graph.
//   - "live"  : endpoints actually on the DDS bus (real SDK only; includes
//               services/clients that are NOT in our graph). Empty under stub.
//   - "both"  : the union of the two, de-duplicated (default).
class OnboardApiDiscoveryNode final : public Node {
public:
    OnboardApiDiscoveryNode(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "OnboardApi.Discovery"),
          trigger_("trigger"), out_("out"),
          domain_id_(cfg.value("domainId", 1)),
          all_domains_(cfg.value("allDomains", false)),
          source_(cfg.value("source", std::string{"both"})) {
        register_input(&trigger_);
        register_output(&out_);
    }

    void on_start() override {
        trigger_.set_internal_sink([this](InputPort<bool>::Value t) {
            enqueue([this, t] {
                if (!t || !*t) return;
                emit_snapshot();
            });
        });
    }

private:
    void emit_snapshot() {
        auto resolved = OnboardApiDiscovery::instance().resolve(domain_id_, all_domains_, source_);
        auto lv = std::make_shared<ListValue>();
        lv->element_type_tag = "flowboard::OnboardApiEndpoint";
        lv->items.reserve(resolved.size());
        for (auto const& r : resolved)
            lv->items.push_back(to_json(r));
        out_.emit(std::shared_ptr<const ListValue>(std::move(lv)));
    }

    InputPort<bool>       trigger_;
    OutputPort<ListValue> out_;
    int                   domain_id_;
    bool                  all_domains_;
    std::string           source_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "OnboardApi.Discovery", OnboardApiDiscoveryNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "domainId":{"type":"integer","title":"Domain","minimum":0,"default":1,
                    "description":"Domain whose endpoints are listed (ignored for the graph source when allDomains is true)."},
        "allDomains":{"type":"boolean","title":"All domains","default":false,
                      "description":"List graph endpoints across every domain (live discovery is always per-domain)."},
        "source":{"type":"string","title":"Source","enum":["graph","live","both"],"default":"both",
                  "description":"graph = nodes in this graph; live = endpoints on the DDS bus incl. ones not in the graph (real SDK only); both = union."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"domainId":1,"allDomains":false,"source":"both"})JSON")

}  // namespace flowboard::nodes
