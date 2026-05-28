// SPDX-License-Identifier: MIT
#include "flowboard/graph_holder.hpp"
#include "flowboard/live_taps.hpp"
#include "flowboard/loader.hpp"
#include "flowboard/log.hpp"

namespace flowboard {

void GraphHolder::install(std::unique_ptr<Graph> g) {
    std::scoped_lock lock(mu_);
    if (graph_) graph_->stop();
    graph_ = std::move(g);
    if (graph_ && live_publish_) attach_live_taps(*graph_, live_publish_);
    if (graph_) graph_->start();
}

std::vector<std::string> GraphHolder::reload(nlohmann::json const& doc) {
    auto result = config::load(doc);
    if (!result.errors.empty()) return result.errors;
    install(std::move(result.graph));
    return {};
}

void GraphHolder::stop() {
    std::scoped_lock lock(mu_);
    if (graph_) graph_->stop();
    graph_.reset();
}

}  // namespace flowboard
