// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>
#include "flowboard/edge.hpp"  // BackpressurePolicy

namespace flowboard {

/// \file
/// \brief Maps OnboardAPI operation names to delivery reliability and default edge backpressure policies.

/// \brief Delivery reliability class for an OnboardAPI operation.
enum class OpReliability { Coalesce, Reliable };

/// \brief Classify an OnboardAPI operation by its name prefix.
///   Report*, Config*           -> Coalesce (latest value wins)
///   Notify*, Event*, Cmd*, etc -> Reliable (safe default — never silently drop)
inline OpReliability classify_op(std::string_view op) {
    auto starts = [&](std::string_view p) {
        return op.size() >= p.size() && op.substr(0, p.size()) == p;
    };
    if (starts("Report") || starts("Config")) return OpReliability::Coalesce;
    return OpReliability::Reliable;
}

/// \brief "ReportAxis.Value" -> "ReportAxis"; "out" -> "out".
inline std::string_view op_of_port(std::string_view port_name) {
    auto dot = port_name.find('.');
    return dot == std::string_view::npos ? port_name : port_name.substr(0, dot);
}

/// \brief Default edge policy for an edge whose source output port has the given name.
///
/// Block only for the recognized reliable OnboardAPI prefixes (Notify/Event/Cmd);
/// coalesce ops and non-OnboardAPI ports (e.g. "out", "tick") fall back to
/// DropOldest (the existing default). Note this is intentionally narrower than
/// classify_op, whose unknown-op default is Reliable: an unrecognized port is not
/// an OnboardAPI command, so its edge keeps the latest-wins DropOldest default.
inline BackpressurePolicy default_edge_policy(std::string_view src_port_name) {
    auto op = op_of_port(src_port_name);
    auto starts = [&](std::string_view p) {
        return op.size() >= p.size() && op.substr(0, p.size()) == p;
    };
    if (starts("Notify") || starts("Event") || starts("Cmd"))
        return BackpressurePolicy::Block;
    return BackpressurePolicy::DropOldest;
}

}  // namespace flowboard
