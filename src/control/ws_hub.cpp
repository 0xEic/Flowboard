// SPDX-License-Identifier: MIT
#include "ws_hub.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <utility>

namespace flowboard::control {

namespace { constexpr int kFlushIntervalMs = 33; }  // ~30 Hz live-value cadence

WsHub::WsHub() {
    flush_thread_ = std::jthread([this](std::stop_token st) { flush_loop(st); });
}

WsHub::~WsHub() {
    flush_thread_.request_stop();
    if (flush_thread_.joinable()) flush_thread_.join();
}

void WsHub::on_open(crow::websocket::connection* conn) {
    std::scoped_lock lock(mu_);
    subs_[conn] = {};
}

void WsHub::on_close(crow::websocket::connection* conn) {
    std::scoped_lock lock(mu_);
    subs_.erase(conn);
}

void WsHub::on_message(crow::websocket::connection* conn, std::string const& msg) {
    nlohmann::json doc;
    try { doc = nlohmann::json::parse(msg); }
    catch (...) { conn->send_text("{\"error\":\"bad json\"}"); return; }

    auto op = doc.value("op", std::string{});
    auto ports = doc.value("ports", std::vector<std::string>{});

    std::scoped_lock lock(mu_);
    auto& set = subs_[conn];
    if (op == "subscribe") {
        for (auto& p : ports) set.insert(p);
    } else if (op == "unsubscribe") {
        for (auto& p : ports) set.erase(p);
    } else {
        conn->send_text("{\"error\":\"unknown op\"}");
    }
}

void WsHub::publish(std::string const& port_key, std::string const& json_value) {
    // Coalesce: keep only the most recent value per port. Cheap + non-blocking
    // so engine worker threads are never stalled on WS I/O.
    std::scoped_lock lock(mu_);
    latest_[port_key] = json_value;
}

void WsHub::flush_loop(std::stop_token st) {
    using namespace std::chrono;
    while (!st.stop_requested()) {
        std::this_thread::sleep_for(milliseconds(kFlushIntervalMs));

        std::unordered_map<std::string, std::string> pending;
        std::vector<std::pair<crow::websocket::connection*, std::unordered_set<std::string>>> subs_copy;
        {
            std::scoped_lock lock(mu_);
            if (latest_.empty()) continue;
            pending.swap(latest_);
            subs_copy.reserve(subs_.size());
            for (auto& [conn, set] : subs_) subs_copy.emplace_back(conn, set);
        }

        auto now_ms = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        for (auto& [port_key, json_value] : pending) {
            std::string frame = std::string{"{\"port\":\""} + port_key
                              + std::string{"\",\"value\":"} + json_value
                              + std::string{",\"ts\":"} + std::to_string(now_ms) + std::string{"}"};
            for (auto& [conn, set] : subs_copy)
                if (set.count(port_key)) conn->send_text(frame);
        }
    }
}

std::unordered_set<std::string> WsHub::subscribed_ports() const {
    std::scoped_lock lock(mu_);
    std::unordered_set<std::string> all;
    for (auto& [conn, set] : subs_)
        for (auto& p : set) all.insert(p);
    return all;
}

}  // namespace flowboard::control
