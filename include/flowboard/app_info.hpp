// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

/// \file
/// \brief Process-wide snapshot of "what is this app currently running" facts.
///
/// Populated by main() at startup and read by introspection nodes (notably
/// OnboardApi.DeviceReport) so they can report runtime facts — loaded workflow,
/// node/edge counts, control-plane endpoint, uptime — without coupling to the
/// control plane or GraphHolder. All access is mutex-guarded.

namespace flowboard {

class AppInfo {
public:
    struct Snapshot {
        std::string   workflow_file;
        std::string   workflow_name;
        std::string   workflow_description;
        std::size_t   node_count = 0;
        std::size_t   edge_count = 0;
        std::string   bind_host;
        std::uint16_t control_port    = 0;
        bool          control_enabled = false;
        std::chrono::steady_clock::time_point start_time{};
        bool          started = false;
    };

    static AppInfo& instance();

    void set_workflow(std::string file, std::string name, std::string description,
                      std::size_t node_count, std::size_t edge_count);
    void set_control(std::string bind_host, std::uint16_t port, bool enabled);
    void mark_start();

    Snapshot  get() const;
    long long uptime_seconds() const;

private:
    AppInfo() = default;
    mutable std::mutex mu_;
    Snapshot snap_;
};

}  // namespace flowboard
