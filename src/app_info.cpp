// SPDX-License-Identifier: MIT
#include "flowboard/app_info.hpp"

namespace flowboard {

AppInfo& AppInfo::instance() {
    static AppInfo inst;
    return inst;
}

void AppInfo::set_workflow(std::string file, std::string name, std::string description,
                           std::size_t node_count, std::size_t edge_count) {
    std::scoped_lock lk(mu_);
    snap_.workflow_file        = std::move(file);
    snap_.workflow_name        = std::move(name);
    snap_.workflow_description = std::move(description);
    snap_.node_count           = node_count;
    snap_.edge_count           = edge_count;
}

void AppInfo::set_control(std::string bind_host, std::uint16_t port, bool enabled) {
    std::scoped_lock lk(mu_);
    snap_.bind_host        = std::move(bind_host);
    snap_.control_port     = port;
    snap_.control_enabled  = enabled;
}

void AppInfo::mark_start() {
    std::scoped_lock lk(mu_);
    snap_.start_time = std::chrono::steady_clock::now();
    snap_.started    = true;
}

AppInfo::Snapshot AppInfo::get() const {
    std::scoped_lock lk(mu_);
    return snap_;
}

long long AppInfo::uptime_seconds() const {
    std::scoped_lock lk(mu_);
    if (!snap_.started) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - snap_.start_time).count();
}

}  // namespace flowboard
