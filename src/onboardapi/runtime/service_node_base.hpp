// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include "flowboard/node.hpp"
#include "flowboard/onboardapi_discovery.hpp"

namespace flowboard::onboardapi {

// Base for generated M_Foo_Service_Node classes.
//
// Reads `domainId` and `serviceName` from the JSON config. Subclasses (generated)
// construct an SDK Service handle in on_start() and tear it down in on_stop().
// Registers itself as an open endpoint in OnboardApiDiscovery for the lifetime
// of the node so the `OnboardApi.Discovery` node can enumerate it.
class ServiceNodeBase : public Node {
public:
    ServiceNodeBase(std::string id, std::string type_name, nlohmann::json const& cfg)
        : Node(std::move(id), std::move(type_name)),
          domain_id_(cfg.value("domainId", 1)),
          service_name_(cfg.value("serviceName", std::string{"default"})) {
        // "__probe__" is the throwaway instance the control plane builds to
        // inspect port shape for the catalog; it must not count as open.
        if (this->id() != "__probe__")
            disco_token_ = OnboardApiDiscovery::instance().open(
                domain_id_, onboardapi_interface_type(this->type_name()),
                "Service", service_name_);
    }

    ~ServiceNodeBase() override {
        OnboardApiDiscovery::instance().close(disco_token_);
    }

protected:
    int         domain_id_;
    std::string service_name_;

private:
    OnboardApiDiscovery::Token disco_token_{0};
};

}  // namespace flowboard::onboardapi
