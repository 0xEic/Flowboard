// SPDX-License-Identifier: MIT
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "flowboard/type_tag.hpp"

/// \file
/// \brief Defines flowboard::ListValue, the first-class list value carried on `flowboard::List` ports.

namespace flowboard {

/// \brief First-class list value flowing on `flowboard::List` ports.
/// \details `items` carries each element as a JSON value so list ops (Sort, Filter,
/// Find, GetAt) can address fields by dot path without per-element-type
/// template instantiations. `element_type_tag` is informational only — it
/// echoes the type tag of the elements produced upstream so UIs and logs can
/// hint at the shape; it is not used for dispatch.
struct ListValue {
    std::vector<::nlohmann::json> items;
    std::string                   element_type_tag;
};

/// \brief Serialize a flowboard::ListValue to JSON.
inline ::nlohmann::json to_json(ListValue const& v) {
    ::nlohmann::json j = ::nlohmann::json::object();
    j["items"]          = v.items;
    j["elementTypeTag"] = v.element_type_tag;
    return j;
}

/// \brief Deserialize a flowboard::ListValue from JSON, accepting either a bare array or an object form.
inline void from_json(::nlohmann::json const& j, ListValue& v) {
    if (j.is_array()) {
        v.items = j.get<std::vector<::nlohmann::json>>();
        v.element_type_tag.clear();
        return;
    }
    j.at("items").get_to(v.items);
    if (j.contains("elementTypeTag"))
        j.at("elementTypeTag").get_to(v.element_type_tag);
}

}  // namespace flowboard

OP_DECLARE_TYPE(::flowboard::ListValue, "flowboard::List")
