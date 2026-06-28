// SPDX-License-Identifier: MIT
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include "flowboard/loader.hpp"
#include "flowboard/log.hpp"
#include "flowboard/graph_holder.hpp"
#include "flowboard/app_info.hpp"
#include "flowboard/plugin_host.hpp"

#if defined(FLOWBOARD_HAS_CONTROL_PLANE)
#  include "flowboard/control_plane.hpp"
#endif

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

std::atomic<bool> g_stop{false};
void handle_signal(int) { g_stop.store(true); }

#if defined(_WIN32)
// On Windows, std::signal(SIGINT) is delivered via SetConsoleCtrlHandler,
// but the C runtime spawns a separate thread to invoke the handler and
// races with other CTRL handlers in the process (e.g. ones registered by
// linked libraries). Installing our own handler directly guarantees we
// observe Ctrl+C / Ctrl+Break / console close without depending on that.
BOOL WINAPI win_console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_stop.store(true);
            return TRUE;  // handled
        default:
            return FALSE;
    }
}
#endif

struct Args {
    std::string   config_path;
    std::string   bind_host           = "127.0.0.1";
    std::uint16_t control_port        = 8765;
    bool          serve_control_plane = true;
    bool          dump_nodes          = false;
    std::string   plugins_dir;  ///< Override for the plugin folder (see --plugins).
};

bool parse_args(int argc, char** argv, Args& out) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            out.control_port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--bind" && i + 1 < argc) {
            out.bind_host = argv[++i];
        } else if (a == "--no-control-plane") {
            out.serve_control_plane = false;
        } else if (a == "--plugins" && i + 1 < argc) {
            out.plugins_dir = argv[++i];
        } else if (a == "--dump-nodes") {
            out.dump_nodes = true;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown option: " << a << "\n";
            return false;
        } else {
            if (!out.config_path.empty()) {
                std::cerr << "extra positional arg: " << a << "\n";
                return false;
            }
            out.config_path = a;
        }
    }
    // A graph file is optional: with none, the engine starts an empty graph so
    // the web UI opens on a blank canvas. Only genuine arg errors return false.
    return true;
}

// Absolute path of the running executable, or empty if it cannot be determined.
std::filesystem::path executable_path() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(std::wstring(buf, n));
#elif defined(__linux__)
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : p;
#else
    return {};
#endif
}

// Where to look for plugins: --plugins wins, then $FLOWBOARD_PLUGIN_DIR, then a
// "plugins" folder next to the executable (falling back to the CWD).
std::filesystem::path resolve_plugin_dir(Args const& args) {
    if (!args.plugins_dir.empty()) return std::filesystem::path(args.plugins_dir);
    if (char const* env = std::getenv("FLOWBOARD_PLUGIN_DIR"); env && *env)
        return std::filesystem::path(env);
    auto exe = executable_path();
    return (exe.empty() ? std::filesystem::path(".") : exe.parent_path()) / "plugins";
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        std::cerr << "usage: flowboard [--port N] [--bind HOST] [--plugins DIR] [--no-control-plane] [graph.json]\n"
                     "       (omit graph.json to start on an empty canvas)\n"
                     "       flowboard --dump-nodes\n";
        return 64;
    }

    flowboard::log::init();
    // Keep stdout pure JSON for --dump-nodes by silencing diagnostics up front.
    if (args.dump_nodes) spdlog::set_level(spdlog::level::off);

    // Load node plugins before anything reads the registry, so their types show
    // up in --dump-nodes, the web catalog, and any graph that references them.
    // Declared here so it outlives the graph + server (destroyed last).
    flowboard::PluginHost plugin_host;
    plugin_host.load_directory(resolve_plugin_dir(args));

    if (args.dump_nodes) {
#if defined(FLOWBOARD_HAS_CONTROL_PLANE)
        std::cout << flowboard::control::catalog_json_string() << "\n";
        return 0;
#else
        std::cerr << "--dump-nodes requires the control plane (FLOWBOARD_BUILD_CONTROL_PLANE=ON)\n";
        return 70;
#endif
    }

    nlohmann::json doc;
    if (args.config_path.empty()) {
        // No graph file: start on an empty canvas. The control-plane web UI can
        // then build a graph live and Apply & Reload it.
        doc = {{"version", "1.0"},
               {"metadata", {{"name", "untitled"}}},
               {"nodes", nlohmann::json::array()},
               {"edges", nlohmann::json::array()}};
        spdlog::info("no graph file given — starting on an empty canvas");
    } else {
        std::ifstream file(args.config_path);
        if (!file) { spdlog::error("cannot open config file: {}", args.config_path); return 66; }
        try { file >> doc; } catch (std::exception const& e) {
            spdlog::error("invalid JSON: {}", e.what()); return 65;
        }
    }
    auto loaded = flowboard::config::load(doc);
    if (!loaded.errors.empty()) {
        for (auto const& e : loaded.errors) spdlog::error("config: {}", e);
        return 78;
    }
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);
#if defined(_WIN32)
    SetConsoleCtrlHandler(win_console_ctrl_handler, TRUE);
#endif

    // Record runtime facts for introspection nodes (OnboardApi.DeviceReport).
    {
        auto const& meta = doc.contains("metadata") ? doc["metadata"] : nlohmann::json::object();
        auto& info = flowboard::AppInfo::instance();
        info.set_workflow(
            args.config_path,
            meta.value("name", std::string{}),
            meta.value("description", std::string{}),
            doc.contains("nodes") && doc["nodes"].is_array() ? doc["nodes"].size() : 0,
            doc.contains("edges") && doc["edges"].is_array() ? doc["edges"].size() : 0);
        info.set_control(args.bind_host, args.control_port, args.serve_control_plane);
        info.mark_start();
    }

    if (!args.config_path.empty())
        spdlog::info("starting graph from {}", args.config_path);
    flowboard::GraphHolder holder;
    holder.install(std::move(loaded.graph));

#if defined(FLOWBOARD_HAS_CONTROL_PLANE)
    flowboard::control::Server server;
    if (args.serve_control_plane) {
        server.attach_holder(&holder);
        flowboard::control::Config cfg;
        cfg.bind_host = args.bind_host;
        cfg.port      = args.control_port;
        server.start(cfg);
    }
#endif

    while (!g_stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    spdlog::info("stop signal received, draining graph...");

    // Watchdog: if graceful shutdown stalls (e.g. a node worker won't join,
    // or crow's io_service won't unwind), force-exit the process so Ctrl+C
    // always returns the user to a shell.
    std::thread watchdog([] {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        spdlog::warn("shutdown watchdog tripped — force-exiting");
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    });
    watchdog.detach();

#if defined(FLOWBOARD_HAS_CONTROL_PLANE)
    server.stop();
#endif
    holder.stop();
    spdlog::info("clean shutdown");
    return 0;
}
