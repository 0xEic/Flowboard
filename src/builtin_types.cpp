// SPDX-License-Identifier: MIT
#include <cstdint>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/wire_registry.hpp"
#include "flowboard/tap_registry.hpp"

// Wire-factory registrations for built-in primitive types and the ListValue
// container. Generated *_nodes.cpp files contribute one of these per struct
// (emitted by pipegen alongside the per-struct log and kv-accumulator factories),
// so any port whose type tag is registered with OP_DECLARE_TYPE can be wired
// without modifying graph.cpp.

OP_REGISTER_WIRE_FACTORY("flowboard::Bool",   bool)
OP_REGISTER_WIRE_FACTORY("flowboard::Int64",  std::int64_t)
OP_REGISTER_WIRE_FACTORY("flowboard::Double", double)
OP_REGISTER_WIRE_FACTORY("flowboard::String", std::string)
OP_REGISTER_WIRE_FACTORY("flowboard::List",   ::flowboard::ListValue)
OP_REGISTER_WIRE_FACTORY("flowboard::UInt8",  std::uint8_t)
OP_REGISTER_WIRE_FACTORY("flowboard::Int16",  std::int16_t)
OP_REGISTER_WIRE_FACTORY("flowboard::UInt16", std::uint16_t)
OP_REGISTER_WIRE_FACTORY("flowboard::Int32",  std::int32_t)
OP_REGISTER_WIRE_FACTORY("flowboard::UInt32", std::uint32_t)
OP_REGISTER_WIRE_FACTORY("flowboard::UInt64", std::uint64_t)
OP_REGISTER_WIRE_FACTORY("flowboard::Float",  float)
OP_REGISTER_WIRE_FACTORY("flowboard::Char",   char)

// Live-tap factories so the web canvas streams values for every primitive port.
OP_REGISTER_TAP_FACTORY("flowboard::Bool",   bool)
OP_REGISTER_TAP_FACTORY("flowboard::Int64",  std::int64_t)
OP_REGISTER_TAP_FACTORY("flowboard::Double", double)
OP_REGISTER_TAP_FACTORY("flowboard::String", std::string)
OP_REGISTER_TAP_FACTORY("flowboard::List",   ::flowboard::ListValue)
OP_REGISTER_TAP_FACTORY("flowboard::UInt8",  std::uint8_t)
OP_REGISTER_TAP_FACTORY("flowboard::Int16",  std::int16_t)
OP_REGISTER_TAP_FACTORY("flowboard::UInt16", std::uint16_t)
OP_REGISTER_TAP_FACTORY("flowboard::Int32",  std::int32_t)
OP_REGISTER_TAP_FACTORY("flowboard::UInt32", std::uint32_t)
OP_REGISTER_TAP_FACTORY("flowboard::UInt64", std::uint64_t)
OP_REGISTER_TAP_FACTORY("flowboard::Float",  float)
OP_REGISTER_TAP_FACTORY("flowboard::Char",   char)
