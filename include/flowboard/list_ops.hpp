// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "flowboard/list_value.hpp"

/// \file
/// \brief Helpers for list transform ops: dot-path field access, value comparison, and predicate evaluation.

namespace flowboard::list_ops {

/// \brief Resolve a dot-separated field path (e.g. "value.SourcePosition.X") against
/// a single list item.
/// \details Missing segments yield JSON null so downstream predicates
/// behave consistently for absent fields.
inline ::nlohmann::json resolve_path(::nlohmann::json const& item,
                                     std::string_view path) {
    if (path.empty()) return item;
    ::nlohmann::json const* cur = &item;
    std::size_t start = 0;
    while (start <= path.size()) {
        auto dot = path.find('.', start);
        auto end = (dot == std::string_view::npos) ? path.size() : dot;
        std::string_view seg = path.substr(start, end - start);
        if (!cur->is_object() || !cur->contains(seg)) return nullptr;
        cur = &cur->at(std::string(seg));
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return *cur;
}

/// \brief Write `value` at a dot-separated field path, creating intermediate objects
/// as needed. Empty path replaces the whole item. Used by Transform.List.MapField.
inline void set_path(::nlohmann::json& item, std::string_view path,
                     ::nlohmann::json value) {
    if (path.empty()) { item = std::move(value); return; }
    ::nlohmann::json* cur = &item;
    std::size_t start = 0;
    for (;;) {
        auto dot = path.find('.', start);
        auto end = (dot == std::string_view::npos) ? path.size() : dot;
        std::string seg(path.substr(start, end - start));
        if (dot == std::string_view::npos) { (*cur)[seg] = std::move(value); return; }
        if (!cur->is_object()) *cur = ::nlohmann::json::object();
        cur = &(*cur)[seg];
        start = dot + 1;
    }
}

/// \brief Comparison/predicate operators supported by list filter and find ops.
enum class PredicateOp { Eq, Neq, Lt, Lte, Gt, Gte, Contains };

/// \brief Parse a predicate op tag (e.g. "eq", "contains") into a flowboard::list_ops::PredicateOp.
inline PredicateOp parse_predicate_op(std::string const& s) {
    if (s == "eq")       return PredicateOp::Eq;
    if (s == "neq")      return PredicateOp::Neq;
    if (s == "lt")       return PredicateOp::Lt;
    if (s == "lte")      return PredicateOp::Lte;
    if (s == "gt")       return PredicateOp::Gt;
    if (s == "gte")      return PredicateOp::Gte;
    if (s == "contains") return PredicateOp::Contains;
    throw std::runtime_error("Transform.List.*: unknown op '" + s + "'");
}

/// \brief Compare two JSON values. Numbers compare numerically; strings lexicographically;
/// nulls sort before everything. Heterogeneous comparison falls back to JSON-dump
/// lexicographic order so sort is total even on mixed types.
inline int compare_json(::nlohmann::json const& a, ::nlohmann::json const& b) {
    if (a.is_null() && b.is_null()) return 0;
    if (a.is_null()) return -1;
    if (b.is_null()) return 1;
    if (a.is_number() && b.is_number()) {
        double da = a.get<double>(), db = b.get<double>();
        return (da < db) ? -1 : (da > db ? 1 : 0);
    }
    if (a.is_string() && b.is_string()) {
        auto sa = a.get<std::string>(), sb = b.get<std::string>();
        return sa.compare(sb);
    }
    if (a.is_boolean() && b.is_boolean()) {
        bool ba = a.get<bool>(), bb = b.get<bool>();
        return (ba == bb) ? 0 : (ba ? 1 : -1);
    }
    auto sa = a.dump(), sb = b.dump();
    return sa.compare(sb);
}

/// \brief Evaluate a flowboard::list_ops::PredicateOp comparing a field value against a target.
inline bool eval_predicate(::nlohmann::json const& field_value,
                           PredicateOp op,
                           ::nlohmann::json const& target) {
    if (op == PredicateOp::Contains) {
        if (!field_value.is_string() || !target.is_string()) return false;
        return field_value.get<std::string>().find(target.get<std::string>())
               != std::string::npos;
    }
    int c = compare_json(field_value, target);
    switch (op) {
        case PredicateOp::Eq:  return c == 0;
        case PredicateOp::Neq: return c != 0;
        case PredicateOp::Lt:  return c <  0;
        case PredicateOp::Lte: return c <= 0;
        case PredicateOp::Gt:  return c >  0;
        case PredicateOp::Gte: return c >= 0;
        case PredicateOp::Contains: return false;  // unreachable
    }
    return false;
}

}  // namespace flowboard::list_ops
