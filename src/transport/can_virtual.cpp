// SPDX-License-Identifier: MIT
#include "flowboard/can_adapter.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace flowboard {
namespace {

// One shared bus per unique `bus` name. Every VirtualBus instance opened with
// the same name joins the same hub; send() fans out to every other instance.
class VirtualBus;

struct Hub {
    std::mutex mu;
    std::unordered_set<VirtualBus*> buses;
};

Hub& hub_for(std::string const& name) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::unique_ptr<Hub>> hubs;
    std::scoped_lock lk(mu);
    auto& slot = hubs[name];
    if (!slot) slot = std::make_unique<Hub>();
    return *slot;
}

class VirtualBus : public ICanBus {
public:
    VirtualBus(std::string name, CanRxCallback cb)
        : name_(std::move(name)), cb_(std::move(cb)), hub_(&hub_for(name_)) {
        std::scoped_lock lk(hub_->mu);
        hub_->buses.insert(this);
    }
    ~VirtualBus() override {
        std::scoped_lock lk(hub_->mu);
        hub_->buses.erase(this);
    }

    bool send(CanFrame const& frame) override {
        CanFrame stamped = frame;
        stamped.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::vector<VirtualBus*> peers;
        {   std::scoped_lock lk(hub_->mu);
            peers.reserve(hub_->buses.size());
            for (auto* b : hub_->buses) if (b != this) peers.push_back(b);
        }
        for (auto* b : peers) if (b->cb_.on_frame) b->cb_.on_frame(stamped);
        return true;
    }

private:
    std::string   name_;
    CanRxCallback cb_;
    Hub*          hub_;
};

class VirtualAdapter : public ICanAdapter {
public:
    std::string_view name() const override { return "virtual"; }
    std::unique_ptr<ICanBus> open(CanBusConfig const& cfg, CanRxCallback cb) override {
        return std::make_unique<VirtualBus>(cfg.bus.empty() ? "default" : cfg.bus, std::move(cb));
    }
};

const bool _virtual_registered = [] {
    register_can_adapter(std::make_unique<VirtualAdapter>());
    return true;
}();

}  // namespace
}  // namespace flowboard
