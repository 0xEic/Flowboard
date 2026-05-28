// SPDX-License-Identifier: MIT
#include "flowboard/control_plane.hpp"
#include "flowboard/graph_holder.hpp"
#include "flowboard/log.hpp"
#include "flowboard/graph.hpp"
#include "flowboard/log_registry.hpp"
#include "flowboard/throttle_registry.hpp"
#include "flowboard/synchronize_registry.hpp"
#include "flowboard/onboardapi_discovery.hpp"
#include "flowboard/button_node.hpp"
#include "serialization.hpp"
#include "ws_hub.hpp"
#include <algorithm>
#include <crow.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#if defined(FLOWBOARD_HAS_WEB)
#  include "routes_static.hpp"
#endif

namespace flowboard::control {

struct Server::Impl {
    crow::SimpleApp     app;
    std::thread         thread;
    std::atomic<bool>   thread_done{false};
    GraphHolder*        holder = nullptr;
    WsHub               hub;
};

Server::Server() : impl_(std::make_unique<Impl>()) {}

Server::~Server() { stop(); }

void Server::attach_holder(GraphHolder* holder) {
    impl_->holder = holder;
    holder->set_live_publish([this](std::string const& k, std::string const& v) {
        impl_->hub.publish(k, v);
    });
}

void Server::start(Config const& cfg) {
    if (running_.exchange(true)) return;

    CROW_ROUTE(impl_->app, "/api/health")([] {
        return crow::response{200, "application/json", std::string{"{\"ok\":true}"}};
    });

    CROW_ROUTE(impl_->app, "/api/types")([] {
        auto j = catalog_to_json();
        return crow::response{200, "application/json", j.dump()};
    });

    // List of every type tag that has a Sinks.Log factory registered —
    // primitives + every onboardapi struct. Used by the Inspector's
    // Sinks.Log form to populate the inputType dropdown.
    CROW_ROUTE(impl_->app, "/api/log_types")([] {
        auto types = ::flowboard::registered_log_types();
        std::sort(types.begin(), types.end());
        return crow::response{200, "application/json",
                              nlohmann::json(types).dump()};
    });

    // List of every type tag that has a Transform.Throttle factory registered —
    // primitives + every onboardapi struct. Used by the Inspector's Throttle
    // form to populate the inputType dropdown.
    CROW_ROUTE(impl_->app, "/api/throttle_types")([] {
        auto types = ::flowboard::registered_throttle_types();
        std::sort(types.begin(), types.end());
        return crow::response{200, "application/json",
                              nlohmann::json(types).dump()};
    });

    // Type tags with a Transform.Synchronize factory (primitives + onboardapi
    // structs). Drives the Synchronize form's value-type dropdown.
    CROW_ROUTE(impl_->app, "/api/synchronize_types")([] {
        auto types = ::flowboard::registered_synchronize_types();
        std::sort(types.begin(), types.end());
        return crow::response{200, "application/json",
                              nlohmann::json(types).dump()};
    });

    // Discover OnboardAPI Service/Client endpoints for the quick-add menu.
    //   ?domain=<int>      (default 1)
    //   ?source=graph|live|both   (default both)
    //   ?allDomains=true   (graph view only; live is per-domain)
    // Returns {"domain":N,"source":"...","endpoints":[ {interfaceType,direction,
    //   serviceName,domainId,source,[hostName,procName,processId,actorId]} ]}.
    CROW_ROUTE(impl_->app, "/api/discovery")([](crow::request const& req) {
        int domain = 1;
        if (auto const* d = req.url_params.get("domain")) {
            try { domain = std::stoi(d); } catch (...) {}
        }
        std::string source = "both";
        if (auto const* s = req.url_params.get("source")) {
            std::string v = s;
            if (v == "graph" || v == "live" || v == "both") source = v;
        }
        bool all_domains = false;
        if (auto const* a = req.url_params.get("allDomains")) {
            std::string v = a;
            all_domains = (v == "1" || v == "true" || v == "TRUE");
        }
        auto resolved = ::flowboard::OnboardApiDiscovery::instance()
                            .resolve(domain, all_domains, source);
        nlohmann::json eps = nlohmann::json::array();
        for (auto const& r : resolved) eps.push_back(::flowboard::to_json(r));
        nlohmann::json out = {{"domain", domain}, {"source", source}, {"endpoints", std::move(eps)}};
        return crow::response{200, "application/json", out.dump()};
    });

    CROW_ROUTE(impl_->app, "/api/graph").methods(crow::HTTPMethod::GET)([this] {
        auto* h = impl_->holder;
        if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
        std::scoped_lock lock(h->mu());
        auto* g = h->graph_unlocked();
        if (!g) return crow::response{503, "application/json", "{\"error\":\"no graph\"}"};
        return crow::response{200, "application/json", g->source_json().dump()};
    });

    CROW_ROUTE(impl_->app, "/api/stats")([this] {
        auto* h = impl_->holder;
        if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
        std::scoped_lock lock(h->mu());
        auto* g = h->graph_unlocked();
        if (!g) return crow::response{503, "application/json", "{\"error\":\"no graph\"}"};
        return crow::response{200, "application/json", stats_to_json(*g).dump()};
    });

    CROW_ROUTE(impl_->app, "/api/graph").methods(crow::HTTPMethod::POST)(
        [this](crow::request const& req) -> crow::response {
            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body); }
            catch (std::exception const& e) {
                return crow::response{400, "application/json",
                    nlohmann::json{{"error", std::string{"invalid JSON: "} + e.what()}}.dump()};
            }
            auto* h = impl_->holder;
            if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
            auto errors = h->reload(body);
            if (!errors.empty()) {
                return crow::response{400, "application/json",
                    nlohmann::json{{"errors", errors}}.dump()};
            }
            return crow::response{200, "application/json", "{\"ok\":true}"};
        });

    CROW_ROUTE(impl_->app, "/api/pause").methods(crow::HTTPMethod::POST)([this] {
        auto* h = impl_->holder;
        if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
        std::scoped_lock lock(h->mu());
        auto* g = h->graph_unlocked();
        if (!g) return crow::response{503, "application/json", "{\"error\":\"no graph\"}"};
        for (auto& n : g->nodes()) n->pause();
        return crow::response{200, "application/json", "{\"ok\":true}"};
    });
    CROW_ROUTE(impl_->app, "/api/resume").methods(crow::HTTPMethod::POST)([this] {
        auto* h = impl_->holder;
        if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
        std::scoped_lock lock(h->mu());
        auto* g = h->graph_unlocked();
        if (!g) return crow::response{503, "application/json", "{\"error\":\"no graph\"}"};
        for (auto& n : g->nodes()) n->resume();
        return crow::response{200, "application/json", "{\"ok\":true}"};
    });

    // Push a boolean from a Debug.TriggerButton node's canvas button into the
    // running graph. Body: {"nodeId":"...","value":true|false}.
    CROW_ROUTE(impl_->app, "/api/button").methods(crow::HTTPMethod::POST)(
        [this](crow::request const& req) -> crow::response {
            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body); }
            catch (std::exception const& e) {
                return crow::response{400, "application/json",
                    nlohmann::json{{"error", std::string{"invalid JSON: "} + e.what()}}.dump()};
            }
            auto node_id = body.value("nodeId", std::string{});
            bool value   = body.value("value", false);
            if (node_id.empty())
                return crow::response{400, "application/json", "{\"error\":\"missing nodeId\"}"};

            auto* h = impl_->holder;
            if (!h) return crow::response{503, "application/json", "{\"error\":\"no holder\"}"};
            std::scoped_lock lock(h->mu());
            auto* g = h->graph_unlocked();
            if (!g) return crow::response{503, "application/json", "{\"error\":\"no graph\"}"};
            for (auto& n : g->nodes()) {
                if (n->id() != node_id) continue;
                if (auto* b = dynamic_cast<::flowboard::IButtonNode*>(n.get())) {
                    b->button_press(value);
                    return crow::response{200, "application/json", "{\"ok\":true}"};
                }
                return crow::response{400, "application/json",
                    "{\"error\":\"node is not a Debug.TriggerButton\"}"};
            }
            return crow::response{404, "application/json", "{\"error\":\"node not found\"}"};
        });

    CROW_WEBSOCKET_ROUTE(impl_->app, "/ws")
        .onopen([this](crow::websocket::connection& conn) {
            impl_->hub.on_open(&conn);
        })
        .onclose([this](crow::websocket::connection& conn, std::string const& /*reason*/) {
            impl_->hub.on_close(&conn);
        })
        .onmessage([this](crow::websocket::connection& conn, std::string const& msg, bool is_binary) {
            if (!is_binary) impl_->hub.on_message(&conn, msg);
        });

#if defined(FLOWBOARD_HAS_WEB)
    register_static_routes(impl_->app);
#endif

    // Let main.cpp own SIGINT/SIGTERM. Otherwise crow's asio signal handler
    // consumes Ctrl+C, stops its own thread, and main's g_stop never flips.
    impl_->app.signal_clear();
    impl_->app.bindaddr(cfg.bind_host).port(cfg.port).multithreaded();

    impl_->thread_done.store(false);
    impl_->thread = std::thread([this] {
        try {
            impl_->app.run();
        } catch (std::exception const& e) {
            spdlog::error("control plane: {}", e.what());
        }
        impl_->thread_done.store(true);
    });

    spdlog::info("control plane listening on {}:{}", cfg.bind_host, cfg.port);
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    impl_->app.stop();
    if (impl_->thread.joinable()) {
        // Bounded wait: on Windows, crow's run() occasionally doesn't return
        // promptly after stop() (a websocket teardown still pending in the
        // io_service). Poll the done flag and detach if the deadline passes
        // so program shutdown isn't held hostage by the HTTP thread.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!impl_->thread_done.load() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (impl_->thread_done.load()) {
            impl_->thread.join();
        } else {
            spdlog::warn("control plane thread did not exit in 2s — detaching");
            impl_->thread.detach();
        }
    }
    spdlog::info("control plane stopped");
}

}  // namespace flowboard::control
