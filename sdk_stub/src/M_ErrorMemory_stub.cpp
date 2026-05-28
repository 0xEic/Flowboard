// SPDX-License-Identifier: MIT
#include "onboardapi/M_ErrorMemory.hpp"
#include "stub_bus.hpp"

namespace M_ErrorMemory {

Client::Client(int /*domain*/, std::string /*svc*/, IClient& /*cb*/) {}
Client::~Client() = default;
std::unique_ptr<Client> Client::create(int domainId, std::string serviceName, IClient& cb) {
    return std::unique_ptr<Client>(new Client(domainId, std::move(serviceName), cb));
}

Service::Service(int /*domain*/, std::string /*svc*/, IService& /*cb*/) {}
Service::~Service() = default;
std::unique_ptr<Service> Service::create(int domainId, std::string serviceName, IService& cb) {
    return std::unique_ptr<Service>(new Service(domainId, std::move(serviceName), cb));
}

}  // namespace M_ErrorMemory
