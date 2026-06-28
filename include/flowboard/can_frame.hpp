// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>
#include "flowboard/type_tag.hpp"

/// \file
/// \brief First-class CAN frame value flowing on `flowboard::CanFrame` ports.

namespace flowboard {

/// \brief Forward-compatible CAN frame (classical fields populated; FD fields
///        wired through but only set by an FD-aware adapter).
struct CanFrame {
    std::uint32_t id           = 0;     ///< 11-bit (0..0x7FF) or 29-bit (0..0x1FFFFFFF).
    bool          is_extended  = false; ///< 29-bit identifier flag.
    bool          is_remote    = false; ///< RTR (classical only).
    bool          is_fd        = false; ///< CAN FD frame.
    bool          is_brs       = false; ///< Bit-rate switch (FD only).
    bool          is_esi       = false; ///< Error state indicator (FD only).
    std::uint8_t  dlc          = 0;     ///< 0..8 classical, 0..15 FD.
    std::vector<std::uint8_t> data{};   ///< 0..8 (classical) / 0..64 (FD) bytes.
    std::uint64_t timestamp_ns = 0;     ///< Adapter RX timestamp; 0 for TX-side frames.

    friend bool operator==(CanFrame const&, CanFrame const&) = default;
};

inline ::nlohmann::json to_json(CanFrame const& f) {
    return {
        {"id",          f.id},
        {"isExtended",  f.is_extended},
        {"isRemote",    f.is_remote},
        {"isFd",        f.is_fd},
        {"isBrs",       f.is_brs},
        {"isEsi",       f.is_esi},
        {"dlc",         f.dlc},
        {"data",        f.data},
        {"timestampNs", f.timestamp_ns},
    };
}

inline void from_json(::nlohmann::json const& j, CanFrame& f) {
    auto val = [&](char const* k, auto def) {
        auto it = j.find(k);
        using T = decltype(def);
        if (it == j.end() || it->is_null()) return def;
        try { return it->template get<T>(); } catch (...) { return def; }
    };
    f.id           = val("id",          std::uint32_t{0});
    f.is_extended  = val("isExtended",  false);
    f.is_remote    = val("isRemote",    false);
    f.is_fd        = val("isFd",        false);
    f.is_brs       = val("isBrs",       false);
    f.is_esi       = val("isEsi",       false);
    f.dlc          = val("dlc",         std::uint8_t{0});
    f.timestamp_ns = val("timestampNs", std::uint64_t{0});
    f.data.clear();
    if (auto it = j.find("data"); it != j.end() && it->is_array())
        f.data = it->get<std::vector<std::uint8_t>>();
}

}  // namespace flowboard

OP_DECLARE_TYPE(::flowboard::CanFrame, "flowboard::CanFrame")
