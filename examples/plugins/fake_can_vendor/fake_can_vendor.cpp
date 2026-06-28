// SPDX-License-Identifier: MIT
//
// FakeVirtualVendor — a reference vendor-shaped CAN-adapter plugin.
//
// This plugin is a *fake*: it never talks to real hardware. Every frame sent on
// a bus is echoed back to that bus's own RX callback, and frames from one
// client are also delivered to every other client opened on the same channel
// (matching how a real shared CAN bus looks to multiple processes / sockets).
//
// It exists for two reasons:
//
//   1. **Integration substrate.** The engine can exercise the full plugin
//      pathway — DLL load + ABI 2 entry point + registrar dispatch +
//      `Can.Bus` open/send/receive + multi-client fan-out — without an actual
//      CAN dongle. The Virtual in-tree adapter exists too but is *not* shaped
//      like a plugin; this one is.
//
//   2. **Template.** Real vendor adapters (PEAK / Kvaser / Vector / IXXAT)
//      follow the same shape. Copy this folder and replace the body of
//      `FakeVendorBus::send()` plus the constructor with calls into the
//      vendor SDK. The plugin SDK contract is unchanged.
//
// Adapter name is `"fakevendor"`; bus identifier (after the colon in a node's
// `adapter` config) names the channel — multiple clients sharing the same
// channel name share traffic. Example node configs:
//
//     "adapter": "fakevendor:0"        // channel 0
//     "adapter": "fakevendor:engine"   // a named channel

#include "flowboard/plugin.hpp"
#include "flowboard/can_adapter.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

class FakeVendorBus;

// One channel per unique bus name. Each FakeVendorBus opened with the same
// name joins that channel's hub; send() fans out to every bus on the channel.
struct Channel {
    std::mutex mu;
    std::unordered_set<FakeVendorBus*> buses;
    std::uint32_t bitrate = 500'000;  // last writer wins; informational
};

Channel& channel_for(std::string const& name) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::unique_ptr<Channel>> channels;
    std::scoped_lock lk(mu);
    auto& slot = channels[name];
    if (!slot) slot = std::make_unique<Channel>();
    return *slot;
}

class FakeVendorBus : public flowboard::ICanBus {
public:
    FakeVendorBus(std::string name, std::uint32_t bitrate, flowboard::CanRxCallback cb)
        : name_(std::move(name)), cb_(std::move(cb)), channel_(&channel_for(name_)) {
        std::scoped_lock lk(channel_->mu);
        channel_->bitrate = bitrate;
        channel_->buses.insert(this);
    }
    ~FakeVendorBus() override {
        std::scoped_lock lk(channel_->mu);
        channel_->buses.erase(this);
    }

    bool send(flowboard::CanFrame const& frame) override {
        flowboard::CanFrame stamped = frame;
        stamped.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        // Snapshot peers under the channel lock, then deliver outside it so a
        // callback that opens/closes another bus on the same channel can't
        // deadlock against us.
        std::vector<FakeVendorBus*> peers;
        {   std::scoped_lock lk(channel_->mu);
            peers.assign(channel_->buses.begin(), channel_->buses.end());
        }
        // Echo semantics: every bus on this channel sees the frame, **including
        // the sender**. Mirrors a real CAN controller with loopback enabled.
        for (auto* b : peers) if (b->cb_.on_frame) b->cb_.on_frame(stamped);
        return true;
    }

private:
    std::string              name_;
    flowboard::CanRxCallback cb_;
    Channel*                 channel_;
};

class FakeVendorAdapter : public flowboard::ICanAdapter {
public:
    std::string_view name() const override { return "fakevendor"; }

    std::unique_ptr<flowboard::ICanBus> open(
            flowboard::CanBusConfig const& cfg,
            flowboard::CanRxCallback cb) override {
        auto bus_name = cfg.bus.empty() ? std::string{"0"} : cfg.bus;
        return std::make_unique<FakeVendorBus>(std::move(bus_name), cfg.bitrate, std::move(cb));
    }
};

}  // namespace

// --- Plugin entry points ---------------------------------------------------

FLOWBOARD_DECLARE_PLUGIN("Fake Virtual Vendor")

// Required entry point. This plugin contributes no node types, so it's a no-op.
extern "C" FLOWBOARD_PLUGIN_EXPORT
void flowboard_plugin_register(flowboard::NodeRegistry& reg) { (void)reg; }

// ABI 2 entry point — registers our adapter into the host's CanAdapter registry
// via the vtable-dispatching registrar, sidestepping cross-DLL singleton dup.
FLOWBOARD_REGISTER_CAN_ADAPTER(FakeVendorAdapter)
