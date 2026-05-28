// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include <spdlog/spdlog.h>

namespace flowboard {

IInputPort* Node::input(std::string_view name) const {
    auto it = inputs_.find(std::string(name));
    return it == inputs_.end() ? nullptr : it->second;
}

IOutputPort* Node::output(std::string_view name) const {
    auto it = outputs_.find(std::string(name));
    return it == outputs_.end() ? nullptr : it->second;
}

void Node::enqueue(std::function<void()> task) {
    {
        std::scoped_lock lock(queue_mu_);
        queue_.push(std::move(task));
    }
    event_.release();
}

void Node::worker_loop(std::stop_token st) {
    while (!st.stop_requested()) {
        event_.acquire();
        if (st.stop_requested()) break;
        if (paused_.load()) continue;  // skip work but re-block on the next event

        std::queue<std::function<void()>> batch;
        {
            std::scoped_lock lock(queue_mu_);
            std::swap(batch, queue_);
        }
        while (!batch.empty()) {
            try {
                batch.front()();
            } catch (std::exception const& e) {
                spdlog::error("[{}] {}", id_, e.what());
            }
            batch.pop();
        }
    }
}

void Node::start() {
    if (running_.exchange(true)) return;
    on_start();
    worker_ = std::jthread([this](std::stop_token st) { worker_loop(st); });
}

void Node::seed_input_defaults(std::function<bool(std::string const&)> const& is_connected) {
    if (!input_defaults_.is_object()) return;
    for (auto const& [name, port] : inputs_) {
        if (is_connected(name)) continue;          // an edge will drive this port
        auto it = input_defaults_.find(name);
        if (it == input_defaults_.end() || it->is_null()) continue;
        port->deliver_json(*it);
    }
}

void Node::stop() {
    if (!running_.exchange(false)) return;
    worker_.request_stop();
    event_.release();  // wake the worker so it observes stop_requested
    if (worker_.joinable()) worker_.join();
    on_stop();
}

}  // namespace flowboard
