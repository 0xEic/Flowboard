// SPDX-License-Identifier: MIT
#pragma once
#include <string_view>

namespace flowboard {

/// \file
/// \brief Describes a struct field's name, kind, and element type for extraction.

/// \brief Classifies how a field carries its value: single, optional, or list.
enum class FieldKind {
    Scalar,    ///< primitive or struct (single value)
    Optional,  ///< OnboardAPI sequence<T, 1>
    List       ///< OnboardAPI sequence<T>
};

/// \brief Metadata describing a single extractable field of a struct.
struct FieldDescriptor {
    std::string_view name;
    FieldKind        kind;
    std::string_view element_type_tag;  // matches type_tag_v<T> when present, "" if unsupported
};

}  // namespace flowboard
