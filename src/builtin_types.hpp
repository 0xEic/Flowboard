// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include "flowboard/type_tag.hpp"

OP_DECLARE_TYPE(bool,        "flowboard::Bool")
OP_DECLARE_TYPE(int64_t,     "flowboard::Int64")
OP_DECLARE_TYPE(double,      "flowboard::Double")
OP_DECLARE_TYPE(std::string, "flowboard::String")

// Additional scalar primitives so every IDL scalar parameter (axis/button
// indices, counts, float ranges, …) surfaces as a typed port instead of being
// dropped. These mirror the C++ types pipegen's cpp_type_ref emits for the
// corresponding IDL primitives (octet/short/long/float/char/…).
OP_DECLARE_TYPE(std::uint8_t,  "flowboard::UInt8")
OP_DECLARE_TYPE(std::int16_t,  "flowboard::Int16")
OP_DECLARE_TYPE(std::uint16_t, "flowboard::UInt16")
OP_DECLARE_TYPE(std::int32_t,  "flowboard::Int32")
OP_DECLARE_TYPE(std::uint32_t, "flowboard::UInt32")
OP_DECLARE_TYPE(std::uint64_t, "flowboard::UInt64")
OP_DECLARE_TYPE(float,         "flowboard::Float")
OP_DECLARE_TYPE(char,          "flowboard::Char")
