// SPDX-License-Identifier: MIT
#include "stub_bus.hpp"
#include <algorithm>

namespace onboardapi::stub {

Bus& Bus::instance() {
    static Bus b;
    return b;
}

int Bus::subscribe(int domain, std::string svc, std::string op, Callback cb) {
    std::scoped_lock l(mu_);
    int id = next_id_++;
    subs_.push_back({id, domain, std::move(svc), std::move(op), std::move(cb)});
    return id;
}

void Bus::unsubscribe(int id) {
    std::scoped_lock l(mu_);
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
        [&](auto const& s) { return s.id == id; }), subs_.end());
}

void Bus::publish(int domain, std::string const& svc, std::string const& op,
                  void const* args) const {
    std::vector<Callback> matches;
    {
        std::scoped_lock l(mu_);
        for (auto const& s : subs_)
            if (s.domain == domain && s.svc == svc && s.op == op)
                matches.push_back(s.cb);
    }
    for (auto& cb : matches) cb(args);
}

}  // namespace onboardapi::stub
