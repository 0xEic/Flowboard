// SPDX-License-Identifier: MIT
// In-process pub/sub for the stub SDK. Subscribers register
// (domainId, serviceName, opName) -> callback(void const* args).
// No threading guarantees beyond "callback runs on caller's thread".
#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace onboardapi::stub {

using Callback = std::function<void(void const*)>;

class Bus {
public:
    static Bus& instance();

    int  subscribe(int domain, std::string svc, std::string op, Callback cb);
    void unsubscribe(int subscription_id);
    void publish(int domain, std::string const& svc, std::string const& op,
                 void const* args) const;

private:
    Bus() = default;
    mutable std::mutex mu_;
    struct Sub { int id; int domain; std::string svc; std::string op; Callback cb; };
    std::vector<Sub> subs_;
    int next_id_ = 1;
};

}  // namespace onboardapi::stub
