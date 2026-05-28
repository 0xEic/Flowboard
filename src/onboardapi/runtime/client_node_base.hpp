// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include "flowboard/node.hpp"
#include "flowboard/onboardapi_discovery.hpp"

namespace flowboard::onboardapi {

// Base for generated M_Foo_Client_Node classes.
// Same shape as ServiceNodeBase — separated by type for clarity / future divergence.
class ClientNodeBase : public Node {
public:
    ClientNodeBase(std::string id, std::string type_name, nlohmann::json const& cfg)
        : Node(std::move(id), std::move(type_name)),
          domain_id_(cfg.value("domainId", 1)),
          service_name_(cfg.value("serviceName", std::string{"default"})) {
        // "__probe__" is the throwaway catalog-inspection instance; not open.
        if (this->id() != "__probe__")
            disco_token_ = OnboardApiDiscovery::instance().open(
                domain_id_, onboardapi_interface_type(this->type_name()),
                "Client", service_name_);
    }

    ~ClientNodeBase() override {
        OnboardApiDiscovery::instance().close(disco_token_);
    }

protected:
    int         domain_id_;
    std::string service_name_;

private:
    OnboardApiDiscovery::Token disco_token_{0};
};

}  // namespace flowboard::onboardapi
