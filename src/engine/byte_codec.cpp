// SPDX-License-Identifier: MIT
#include "flowboard/byte_codec.hpp"
#include <cstring>

namespace flowboard {
namespace {

void put_uint(std::vector<std::uint8_t>& buf, std::size_t offset,
              std::uint64_t val, std::size_t width, bool little) {
    if (buf.size() < offset + width) buf.resize(offset + width, 0);
    for (std::size_t i = 0; i < width; ++i) {
        auto b = static_cast<std::uint8_t>((val >> (8 * i)) & 0xFF);
        buf[offset + (little ? i : (width - 1 - i))] = b;
    }
}

std::optional<std::uint64_t> get_uint(std::vector<std::uint8_t> const& buf,
              std::size_t offset, std::size_t width, bool little) {
    if (offset + width > buf.size()) return std::nullopt;
    std::uint64_t val = 0;
    for (std::size_t i = 0; i < width; ++i) {
        std::uint8_t b = buf[offset + (little ? i : (width - 1 - i))];
        val |= static_cast<std::uint64_t>(b) << (8 * i);
    }
    return val;
}

std::int64_t sign_extend(std::uint64_t v, std::size_t width) {
    if (width >= 8) return static_cast<std::int64_t>(v);
    std::uint64_t sign = std::uint64_t{1} << (width * 8 - 1);
    if (v & sign) v |= ~((std::uint64_t{1} << (width * 8)) - 1);
    return static_cast<std::int64_t>(v);
}

std::int64_t as_int(::nlohmann::json const& v) {
    if (v.is_number_integer())  return v.get<std::int64_t>();
    if (v.is_number_unsigned()) return static_cast<std::int64_t>(v.get<std::uint64_t>());
    if (v.is_number_float())    return static_cast<std::int64_t>(v.get<double>());
    if (v.is_boolean())         return v.get<bool>() ? 1 : 0;
    return 0;
}

bool is_unsigned_tag(std::string_view t) {
    return t == "flowboard::UInt8" || t == "flowboard::UInt16" ||
           t == "flowboard::UInt32" || t == "flowboard::UInt64" ||
           t == "flowboard::Char";   // Char treated as an unsigned byte
}

std::int64_t sign_extend_bits(std::uint64_t v, std::size_t bits) {
    if (bits == 0) return 0;
    if (bits >= 64) return static_cast<std::int64_t>(v);
    std::uint64_t sign = std::uint64_t{1} << (bits - 1);
    if (v & sign) v |= ~((std::uint64_t{1} << bits) - 1);
    return static_cast<std::int64_t>(v);
}

// Raw little-endian bit pattern of a JSON scalar for a tag (low bit_width bits used).
std::uint64_t to_raw_bits(std::string_view t, ::nlohmann::json const& v) {
    if (t == "flowboard::Float")  { float f = v.is_number() ? v.get<float>() : 0.0f;
        std::uint32_t b; std::memcpy(&b, &f, 4); return b; }
    if (t == "flowboard::Double") { double d = v.is_number() ? v.get<double>() : 0.0;
        std::uint64_t b; std::memcpy(&b, &d, 8); return b; }
    if (t == "flowboard::Bool")   return as_int(v) != 0 ? 1u : 0u;
    return static_cast<std::uint64_t>(as_int(v));
}

}  // namespace

std::size_t byte_width(std::string_view t) {
    if (t == "flowboard::Bool" || t == "flowboard::Char" || t == "flowboard::UInt8") return 1;
    if (t == "flowboard::Int16" || t == "flowboard::UInt16") return 2;
    if (t == "flowboard::Int32" || t == "flowboard::UInt32" || t == "flowboard::Float") return 4;
    if (t == "flowboard::Int64" || t == "flowboard::UInt64" || t == "flowboard::Double") return 8;
    return 0;
}

bool byte_write(std::vector<std::uint8_t>& buf, std::size_t offset,
                std::string_view t, ::nlohmann::json const& v, bool little) {
    std::size_t w = byte_width(t);
    if (w == 0) return false;
    if (t == "flowboard::Float") {
        float f = v.is_number() ? v.get<float>() : 0.0f;
        std::uint32_t bits; std::memcpy(&bits, &f, 4);
        put_uint(buf, offset, bits, 4, little); return true;
    }
    if (t == "flowboard::Double") {
        double d = v.is_number() ? v.get<double>() : 0.0;
        std::uint64_t bits; std::memcpy(&bits, &d, 8);
        put_uint(buf, offset, bits, 8, little); return true;
    }
    std::uint64_t u;
    if (t == "flowboard::Bool") u = as_int(v) != 0 ? 1 : 0;
    else                        u = static_cast<std::uint64_t>(as_int(v));
    put_uint(buf, offset, u, w, little);
    return true;
}

std::optional<::nlohmann::json> byte_read(std::vector<std::uint8_t> const& buf,
                std::size_t offset, std::string_view t, bool little) {
    std::size_t w = byte_width(t);
    if (w == 0) return std::nullopt;
    auto raw = get_uint(buf, offset, w, little);
    if (!raw) return std::nullopt;
    if (t == "flowboard::Float")  { std::uint32_t b = static_cast<std::uint32_t>(*raw); float f; std::memcpy(&f, &b, 4); return ::nlohmann::json(f); }
    if (t == "flowboard::Double") { std::uint64_t b = *raw; double d; std::memcpy(&d, &b, 8); return ::nlohmann::json(d); }
    if (t == "flowboard::Bool")   return ::nlohmann::json(*raw != 0);
    if (is_unsigned_tag(t))       return ::nlohmann::json(*raw);
    return ::nlohmann::json(sign_extend(*raw, w));  // signed integers
}

std::size_t bit_width(std::string_view t) {
    if (t == "flowboard::Bool") return 1;
    return byte_width(t) * 8;   // 0 stays 0 for unsupported tags
}

bool bit_write(std::vector<std::uint8_t>& buf, std::size_t bit_pos,
               std::string_view t, ::nlohmann::json const& v) {
    std::size_t W = bit_width(t);
    if (W == 0) return false;
    std::uint64_t u = to_raw_bits(t, v);
    std::size_t need = (bit_pos + W + 7) / 8;
    if (buf.size() < need) buf.resize(need, 0);
    for (std::size_t i = 0; i < W; ++i) {
        std::size_t P = bit_pos + i;
        std::uint8_t mask = static_cast<std::uint8_t>(1u << (P % 8));
        if ((u >> i) & 1u) buf[P / 8] |= mask;
        else               buf[P / 8] &= static_cast<std::uint8_t>(~mask);
    }
    return true;
}

std::optional<::nlohmann::json> bit_read(std::vector<std::uint8_t> const& buf,
               std::size_t bit_pos, std::string_view t) {
    std::size_t W = bit_width(t);
    if (W == 0) return std::nullopt;
    if (bit_pos + W > buf.size() * 8) return std::nullopt;
    std::uint64_t u = 0;
    for (std::size_t i = 0; i < W; ++i) {
        std::size_t P = bit_pos + i;
        if (buf[P / 8] & (1u << (P % 8))) u |= (std::uint64_t{1} << i);
    }
    if (t == "flowboard::Float")  { std::uint32_t b = static_cast<std::uint32_t>(u);
        float f;  std::memcpy(&f, &b, 4); return ::nlohmann::json(f); }
    if (t == "flowboard::Double") { std::uint64_t b = u;
        double d; std::memcpy(&d, &b, 8); return ::nlohmann::json(d); }
    if (t == "flowboard::Bool")   return ::nlohmann::json(u != 0);
    if (is_unsigned_tag(t))       return ::nlohmann::json(u);
    return ::nlohmann::json(sign_extend_bits(u, W));  // signed integers
}

}  // namespace flowboard
