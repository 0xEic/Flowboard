// SPDX-License-Identifier: MIT
#pragma once
#include <crow.h>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace flowboard::control {

// Streams port live-values to subscribed WebSocket clients.
//
// publish() is called from engine worker threads on every port emission. It
// only records the LATEST value per port (cheap, non-blocking) — it never
// touches the socket, so a slow/backed-up WS connection can't stall an engine
// thread. A background flush thread sends the coalesced latest value per port
// at a fixed cadence, so fast producers don't build up a delayed WS backlog.
class WsHub {
public:
    WsHub();
    ~WsHub();

    void on_open  (crow::websocket::connection* conn);
    void on_close (crow::websocket::connection* conn);
    void on_message(crow::websocket::connection* conn, std::string const& msg);

    // Records the latest JSON value for `port_key` (coalesced; sent on next flush).
    void publish(std::string const& port_key, std::string const& json_value);

    std::unordered_set<std::string> subscribed_ports() const;

private:
    void flush_loop(std::stop_token st);

    mutable std::mutex mu_;
    std::unordered_map<crow::websocket::connection*,
                       std::unordered_set<std::string>> subs_;
    std::unordered_map<std::string, std::string> latest_;  // port_key -> latest JSON value
    std::jthread flush_thread_;
};

}  // namespace flowboard::control
