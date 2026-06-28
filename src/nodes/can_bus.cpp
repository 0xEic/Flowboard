// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/can_frame.hpp"
#include "flowboard/can_adapter.hpp"
#include "builtin_types.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace flowboard::nodes {

namespace {

// Split "name:bus" into ("name", "bus"). Missing colon -> ("name", "").
std::pair<std::string, std::string> split_adapter_spec(std::string_view spec) {
    auto colon = spec.find(':');
    if (colon == std::string_view::npos) return { std::string(spec), {} };
    return { std::string(spec.substr(0, colon)), std::string(spec.substr(colon + 1)) };
}

}  // namespace

class CanBusNode : public Node {
public:
    CanBusNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Can.Bus"),
          tx_("tx"), rx_("rx"), sent_("sent"), connected_("connected"),
          adapter_spec_(cfg.value("adapter", std::string{"virtual:default"})),
          bitrate_(cfg.value("bitrate",  std::uint32_t{500'000})),
          listen_only_(cfg.value("listenOnly", false)),
          auto_reconnect_(cfg.value("autoReconnect", true)),
          reconnect_ms_(cfg.value("reconnectIntervalMs", std::uint32_t{1000})) {
        register_input(&tx_);
        register_output(&rx_);
        register_output(&sent_);
        register_output(&connected_);
    }

    ~CanBusNode() override { /* base Node::stop() invokes on_stop */ }

protected:
    void on_start() override {
        tx_.set_internal_sink([this](InputPort<CanFrame>::Value v) {
            enqueue([this, v] { do_send(*v); });
        });
        bus_running_.store(true);
        open_worker_ = std::jthread([this](std::stop_token st){ open_loop(st); });
    }

    void on_stop() override {
        bus_running_.store(false);
        bus_alive_.store(false);
        if (open_worker_.joinable()) { open_worker_.request_stop(); open_worker_.join(); }
        enqueue([this]{ if (bus_) bus_.reset(); });
    }

private:
    void do_send(CanFrame const& f) {
        if (listen_only_) {
            // listen_only is a software-level promise this node won't transmit.
            // True hardware listen-only mode (bus controller in passive state)
            // must be configured at the netlink level, e.g.
            //   ip link set can0 type can bitrate 500000 listen-only on
            // We drop the frame, surface the drop via sent=false, and warn
            // throttled so a graph wired to a listen-only Can.Bus is visible.
            sent_.emit(std::make_shared<const bool>(false));
            auto now = std::chrono::steady_clock::now();
            if (now - last_send_warn_ > std::chrono::seconds(1)) {
                spdlog::warn("[{}] Can.Bus: dropped tx (listenOnly=true)", std::string(id()));
                last_send_warn_ = now;
            }
            return;
        }
        bool ok = false;
        if (bus_) { try { ok = bus_->send(f); } catch (...) { ok = false; } }
        sent_.emit(std::make_shared<const bool>(ok));
        if (!ok) {
            auto now = std::chrono::steady_clock::now();
            if (now - last_send_warn_ > std::chrono::seconds(1)) {
                spdlog::warn("[{}] Can.Bus: send failed", std::string(id()));
                last_send_warn_ = now;
            }
        }
    }

    void open_loop(std::stop_token st) {
        auto [name, bus] = split_adapter_spec(adapter_spec_);
        while (!st.stop_requested() && bus_running_.load()) {
            auto* adapter = find_can_adapter(name);
            if (!adapter) {
                spdlog::error("[{}] Can.Bus: adapter '{}' not registered", std::string(id()), name);
                emit_connected(false);
                if (!auto_reconnect_) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
                continue;
            }
            CanBusConfig cfg{ bus, bitrate_, listen_only_, false, 0 };
            CanRxCallback cb{
                [this](CanFrame const& f) {
                    auto sp = std::make_shared<const CanFrame>(f);
                    enqueue([this, sp]{ rx_.emit(sp); });
                },
                [this](std::string_view msg) {
                    spdlog::warn("[{}] Can.Bus: adapter error: {}", std::string(id()), std::string(msg));
                    enqueue([this]{ if (bus_) bus_.reset(); bus_alive_.store(false); emit_connected(false); });
                }};
            auto opened = adapter->open(cfg, std::move(cb));
            if (!opened) {
                emit_connected(false);
                if (!auto_reconnect_) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
                continue;
            }
            // unique_ptr is move-only; wrap in shared_ptr so the lambda is copy-constructible
            // (std::function requires it). The shared_ptr is consumed once on the worker thread.
            auto sp = std::shared_ptr<ICanBus>(std::move(opened));
            enqueue([this, sp]() mutable {
                bus_ = std::move(sp);
                bus_alive_.store(true);
                emit_connected(true);
            });
            // The adapter owns its own threads now; wait until either we're stopped
            // or the bus is reset. Use the atomic flag rather than reading bus_
            // directly (which would race with the Node worker's writes).
            while (!st.stop_requested() && bus_running_.load() && bus_alive_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!st.stop_requested() && bus_running_.load() && auto_reconnect_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_ms_));
            } else {
                return;
            }
        }
    }

    void emit_connected(bool v) { connected_.emit(std::make_shared<const bool>(v)); }

    InputPort<CanFrame>  tx_;
    OutputPort<CanFrame> rx_;
    OutputPort<bool>     sent_;
    OutputPort<bool>     connected_;
    std::string          adapter_spec_;
    std::uint32_t        bitrate_;
    bool                 listen_only_;
    bool                 auto_reconnect_;
    std::uint32_t        reconnect_ms_;

    std::shared_ptr<ICanBus>         bus_;       // owned + mutated by the Node worker only
    std::atomic<bool>                bus_alive_{false};  // open_loop reads this instead of bus_
    std::atomic<bool>                bus_running_{false};
    std::jthread                     open_worker_;
    std::chrono::steady_clock::time_point last_send_warn_{};
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Can.Bus",
    CanBusNode,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "adapter":{"type":"string","title":"Adapter spec",
          "default":"virtual:default",
          "description":"<adapter-name>:<bus-id>, e.g. 'socketcan:can0', 'virtual:t1', 'peak:0'."},
        "bitrate":{"type":"integer","title":"Bitrate (bps)","minimum":0,"default":500000},
        "listenOnly":{"type":"boolean","title":"Listen-only","default":false},
        "autoReconnect":{"type":"boolean","title":"Auto-reconnect","default":true},
        "reconnectIntervalMs":{"type":"integer","title":"Reconnect interval (ms)","minimum":50,"default":1000}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"adapter":"virtual:default","bitrate":500000,"listenOnly":false,"autoReconnect":true,"reconnectIntervalMs":1000})JSON"
)

}  // namespace flowboard::nodes
