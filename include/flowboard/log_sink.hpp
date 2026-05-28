// SPDX-License-Identifier: MIT
#pragma once
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <utility>

#include "flowboard/json_helpers.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"

/// \file
/// \brief Defines flowboard::LogSinkT, the type-parametrised Sinks.Log node.

namespace flowboard {

/// \brief Sinks.Log node, parametrised on the input type.
/// \details Lives in a header so that
/// generated *_nodes.cpp files can instantiate it for every registered struct
/// type without log_node.cpp needing to know about them.
///
/// Config:
///   - inputType (string, required): used by the dispatching factory to pick
///     which template instantiation to construct; not read here.
///   - prefix    (string, default "value"): label prepended to each log line.
///   - path      (string, default ""): when non-empty, log lines are appended
///     to this file. When empty, lines go to the default spdlog logger
///     (whatever log_setup wired up — stdout by default).
///
/// Values are formatted via `to_json_or_passthrough(...).dump()` so any type
/// with an ADL `to_json` (every generated onboardapi struct) or a direct
/// nlohmann::json constructor (every primitive) prints sensibly.
/// \tparam T The input value type the sink consumes.
template <typename T>
class LogSinkT : public Node {
public:
    LogSinkT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), cfg.value("display", false) ? "Debug.ValueDisplay" : "Sinks.Log"),
          in_("in"),
          prefix_(cfg.value("prefix", std::string{"value"})),
          path_  (cfg.value("path",   std::string{})),
          display_(cfg.value("display", false)) {
        register_input(&in_);
        // Display sinks (Debug.ValueDisplay / Debug.GraphDisplay) consume the
        // value but never log — the web UI renders it from the upstream port's
        // live tap — so no file is opened and on_start installs a no-op sink.
        if (!display_ && !path_.empty()) {
            // std::ios::app = create-if-missing + always append at EOF;
            // multiple Log Sink instances sharing a path interleave their
            // writes, which is intentional — they're independent producers.
            file_.open(path_, std::ios::out | std::ios::app);
            if (!file_) {
                throw std::runtime_error(
                    "Sinks.Log: cannot open path '" + path_ + "'");
            }
        }
    }

    void on_start() override {
        if (display_) {
            // Drain the edge without side effects; the value is observed via
            // the upstream output port's live tap on the web canvas.
            in_.set_internal_sink([](typename InputPort<T>::Value) {});
            return;
        }
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] {
                std::string body = ::flowboard::to_json_or_passthrough(*v).dump();
                if (file_.is_open()) {
                    file_ << std::string(id()) << ": " << prefix_ << " = " << body << '\n';
                    file_.flush();
                } else {
                    spdlog::info("{}: {} = {}", std::string(id()), prefix_, body);
                }
            });
        });
    }

private:
    InputPort<T>  in_;
    std::string   prefix_;
    std::string   path_;
    bool          display_;
    std::ofstream file_;
};

}  // namespace flowboard
