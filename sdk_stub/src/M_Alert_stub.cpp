// SPDX-License-Identifier: MIT
#include "onboardapi/M_Alert.hpp"
#include "stub_bus.hpp"

namespace M_Alert {

Service::Service(int domain, std::string svc, IService& cb)
    : sub_id_(onboardapi::stub::Bus::instance().subscribe(
        domain, std::move(svc), "CmdRaiseAlarm",
        [&cb](void const* args) {
            auto const* c = static_cast<AlarmConditionInfoType const*>(args);
            cb.CmdRaiseAlarm(*c);
        })) {}

Service::~Service() {
    onboardapi::stub::Bus::instance().unsubscribe(sub_id_);
}

std::unique_ptr<Service> Service::create(int domainId, std::string serviceName, IService& cb) {
    return std::unique_ptr<Service>(new Service(domainId, std::move(serviceName), cb));
}

// Client — receives callbacks from the Alert service and can also publish commands.
Client::Client(int domain, std::string svc, IClient& /*cb*/)
    : domain_(domain), svc_(std::move(svc)) {
    // The stub does not deliver Alert client callbacks; the in-process bus
    // exercises the command/publish path only.
}

Client::~Client() = default;

std::unique_ptr<Client> Client::create(int domainId, std::string serviceName, IClient& cb) {
    return std::unique_ptr<Client>(new Client(domainId, std::move(serviceName), cb));
}

void Client::CmdRaiseAlarm(AlarmConditionInfoType const& c) {
    onboardapi::stub::Bus::instance().publish(domain_, svc_, "CmdRaiseAlarm", &c);
}

void Client::CmdResolveAlarm(AlarmConditionInfoType const& c) {
    onboardapi::stub::Bus::instance().publish(domain_, svc_, "CmdResolveAlarm", &c);
}

void Client::CmdOverrideAlarmCondition(AlarmConditionInfoType const& c) {
    onboardapi::stub::Bus::instance().publish(domain_, svc_, "CmdOverrideAlarmCondition", &c);
}

void Client::CmdUnoverrideAlarmCondition(AlarmConditionInfoType const& c) {
    onboardapi::stub::Bus::instance().publish(domain_, svc_, "CmdUnoverrideAlarmCondition", &c);
}

}  // namespace M_Alert
