// SPDX-License-Identifier: MIT
//
// Test-only CAN adapter plugin: registers the "echo" adapter, which echoes
// every send() back to its own RX callback. Used to verify the ABI-2
// flowboard_plugin_register_can_adapter entry point end-to-end.

#include "flowboard/plugin.hpp"
#include "flowboard/can_adapter.hpp"

namespace {

class EchoBus : public flowboard::ICanBus {
public:
    explicit EchoBus(flowboard::CanRxCallback cb) : cb_(std::move(cb)) {}
    bool send(flowboard::CanFrame const& f) override {
        if (cb_.on_frame) cb_.on_frame(f);
        return true;
    }
private:
    flowboard::CanRxCallback cb_;
};

class EchoAdapter : public flowboard::ICanAdapter {
public:
    std::string_view name() const override { return "echo"; }
    std::unique_ptr<flowboard::ICanBus> open(
            flowboard::CanBusConfig const&,
            flowboard::CanRxCallback cb) override {
        return std::make_unique<EchoBus>(std::move(cb));
    }
};

}  // namespace

FLOWBOARD_DECLARE_PLUGIN("Echo CAN Adapter (test)")

// Node-registration entry point: this plugin contributes no node types.
extern "C" FLOWBOARD_PLUGIN_EXPORT
void flowboard_plugin_register(flowboard::NodeRegistry& reg) { (void)reg; }

FLOWBOARD_REGISTER_CAN_ADAPTER(EchoAdapter)
