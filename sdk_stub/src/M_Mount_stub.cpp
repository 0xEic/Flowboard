// SPDX-License-Identifier: MIT
#include "onboardapi/M_Mount.hpp"
#include "stub_bus.hpp"

namespace M_Mount {

Client::Client(int domain, std::string svc, IClient& cb)
    : sub_id_(onboardapi::stub::Bus::instance().subscribe(
        domain, std::move(svc), "ReportMountPosition",
        [&cb](void const* args) {
            auto const* p = static_cast<MountPositionType const*>(args);
            cb.ReportMountPosition(*p);
        })) {}

Client::~Client() {
    onboardapi::stub::Bus::instance().unsubscribe(sub_id_);
}

std::unique_ptr<Client> Client::create(int domainId, std::string serviceName, IClient& cb) {
    return std::unique_ptr<Client>(new Client(domainId, std::move(serviceName), cb));
}

Service::Service(int /*domain*/, std::string /*svc*/, IService& /*cb*/) {}
Service::~Service() = default;
std::unique_ptr<Service> Service::create(int domainId, std::string serviceName, IService& cb) {
    return std::unique_ptr<Service>(new Service(domainId, std::move(serviceName), cb));
}

void stub_publish_position(int domainId, std::string const& serviceName,
                           MountPositionType const& pos) {
    onboardapi::stub::Bus::instance().publish(
        domainId, serviceName, "ReportMountPosition", &pos);
}

}  // namespace M_Mount
