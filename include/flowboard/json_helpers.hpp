// SPDX-License-Identifier: MIT
#pragma once
#include <iterator>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <utility>

namespace flowboard {

/// \file
/// \brief Best-effort conversion of arbitrary C++ values to nlohmann::json, with trait detection helpers.

namespace detail {
    // Detects whether a free function to_json(T const&) is accessible via ADL.
    template <typename T, typename = void>
    struct has_to_json : std::false_type {};
    template <typename T>
    struct has_to_json<T, std::void_t<decltype(to_json(std::declval<T const&>()))>>
        : std::true_type {};

    // Detects whether nlohmann::json can be constructed directly from T.
    template <typename T, typename = void>
    struct is_json_constructible : std::false_type {};
    template <typename T>
    struct is_json_constructible<T,
        std::void_t<decltype(::nlohmann::json(std::declval<T const&>()))>>
        : std::true_type {};

    // Detects a non-string iterable (e.g. std::vector). Used to serialise
    // containers of element types that nlohmann can't handle directly.
    template <typename T, typename = void>
    struct is_iterable : std::false_type {};
    template <typename T>
    struct is_iterable<T, std::void_t<
        decltype(std::begin(std::declval<T const&>())),
        decltype(std::end(std::declval<T const&>()))>>
        : std::true_type {};
}

/// \brief Convert a value to JSON, preferring an ADL to_json, then enum/primitive/container handling, else null.
template <typename T>
inline ::nlohmann::json to_json_or_passthrough(T const& v) {
    if constexpr (detail::has_to_json<T>::value)
        return to_json(v);
    else if constexpr (std::is_enum_v<T>)
        return static_cast<std::underlying_type_t<T>>(v);
    else if constexpr (detail::is_json_constructible<T>::value)
        // Primitives, strings, and vectors of primitives — nlohmann handles these.
        return ::nlohmann::json(v);
    else if constexpr (detail::is_iterable<T>::value)
        // Container of a type nlohmann can't serialise directly — most importantly
        // std::vector<Struct>, where the struct only exposes the engine's 1-arg
        // to_json(Struct) (not nlohmann's 2-arg adl form). Recurse per element so
        // list-typed struct fields (Axes, Buttons, …) serialise instead of
        // collapsing to null.
        {
            auto arr = ::nlohmann::json::array();
            for (auto const& el : v) arr.push_back(to_json_or_passthrough(el));
            return arr;
        }
    else
        return ::nlohmann::json(nullptr);  // fallback: unknown type — emit null
}

}  // namespace flowboard
