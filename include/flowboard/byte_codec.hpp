// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

/// \file
/// \brief Fixed-width byte (de)serialization of primitive JSON scalars, used by
///        the Bytes.Pack / Bytes.Unpack protocol nodes.

namespace flowboard {

/// Byte width of a supported primitive type tag; 0 if unsupported (String/List/struct).
std::size_t byte_width(std::string_view tag);

/// Write JSON scalar \p v as \p tag into \p buf at \p offset (little/big endian),
/// growing \p buf as needed. Returns false if \p tag is unsupported.
bool byte_write(std::vector<std::uint8_t>& buf, std::size_t offset,
                std::string_view tag, ::nlohmann::json const& v, bool little);

/// Read a \p tag value from \p buf at \p offset (little/big endian) as a JSON
/// scalar. nullopt if unsupported or the range is out of bounds.
std::optional<::nlohmann::json> byte_read(std::vector<std::uint8_t> const& buf,
                std::size_t offset, std::string_view tag, bool little);

/// Bit width of a supported primitive type tag in bit-packing mode: Bool = 1,
/// all others = byte_width(tag)*8. 0 if unsupported.
std::size_t bit_width(std::string_view tag);

/// Write JSON scalar \p v as \p tag into \p buf starting at absolute bit
/// \p bit_pos (LSB-first / Intel), growing \p buf as needed. Returns false if
/// \p tag is unsupported.
bool bit_write(std::vector<std::uint8_t>& buf, std::size_t bit_pos,
               std::string_view tag, ::nlohmann::json const& v);

/// Read a \p tag value from \p buf starting at absolute bit \p bit_pos
/// (LSB-first / Intel) as a JSON scalar. nullopt if unsupported or the bit
/// range is out of bounds.
std::optional<::nlohmann::json> bit_read(std::vector<std::uint8_t> const& buf,
               std::size_t bit_pos, std::string_view tag);

}  // namespace flowboard
