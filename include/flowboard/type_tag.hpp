// SPDX-License-Identifier: MIT
#pragma once
#include <string_view>

/// \file
/// \brief Compile-time type tags used to validate port connections at runtime.

namespace flowboard {

/// \brief Trait mapping a message type `T` to its stable string type tag.
///
/// Intentionally left undefined: a type has no tag until it is explicitly
/// registered with #OP_DECLARE_TYPE. Referencing the tag of an unregistered
/// type is therefore a compile error, which prevents silently wiring up a
/// type the engine cannot describe.
template <typename T>
struct TypeTag;  // intentionally undefined: forces explicit registration

/// \brief Convenience accessor for the type tag of `T`.
/// \tparam T A message type previously registered with #OP_DECLARE_TYPE.
template <typename T>
inline constexpr std::string_view type_tag_v = TypeTag<T>::value;

}  // namespace flowboard

/// \brief Register the type tag for a message type.
///
/// Specializes flowboard::TypeTag so that flowboard::type_tag_v works for
/// \p Type. Two ports may only be connected if their tags compare equal.
/// \param Type      The C++ type being tagged.
/// \param TagString A stable string literal identifying the type.
#define OP_DECLARE_TYPE(Type, TagString)                                \
    namespace flowboard {                                            \
        template <> struct TypeTag<Type> {                              \
            static constexpr std::string_view value = TagString;        \
        };                                                              \
    }
