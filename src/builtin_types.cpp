// SPDX-License-Identifier: MIT
#include <cstdint>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/can_frame.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/wire_registry.hpp"
#include "flowboard/tap_registry.hpp"
#include "flowboard/port_factory_registry.hpp"

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

// Port factories for the built-in primitives + List container. These let any
// type-generic node that builds its ports through lookup_port_factory()
// (e.g. Transform.Delay) round-trip built-in values through JSON, the same way
// it already works for OnboardAPI structs and CanFrame. The JSON round-trip is
// symmetric (same type in and out), so the plain to_json/from_json glue is
// sufficient for each of these.
OP_REGISTER_PORT_FACTORY("flowboard::Bool",   bool)
OP_REGISTER_PORT_FACTORY("flowboard::Int64",  std::int64_t)
OP_REGISTER_PORT_FACTORY("flowboard::Double", double)
OP_REGISTER_PORT_FACTORY("flowboard::String", std::string)
OP_REGISTER_PORT_FACTORY("flowboard::List",   ::flowboard::ListValue)
OP_REGISTER_PORT_FACTORY("flowboard::UInt8",  std::uint8_t)
OP_REGISTER_PORT_FACTORY("flowboard::Int16",  std::int16_t)
OP_REGISTER_PORT_FACTORY("flowboard::UInt16", std::uint16_t)
OP_REGISTER_PORT_FACTORY("flowboard::Int32",  std::int32_t)
OP_REGISTER_PORT_FACTORY("flowboard::UInt32", std::uint32_t)
OP_REGISTER_PORT_FACTORY("flowboard::UInt64", std::uint64_t)
OP_REGISTER_PORT_FACTORY("flowboard::Float",  float)
OP_REGISTER_PORT_FACTORY("flowboard::Char",   char)

// CAN frame value: wireable on flowboard::CanFrame ports, tap-streamable to the
// web canvas, and round-trips through Python.Script via the port factory.
OP_REGISTER_WIRE_FACTORY ("flowboard::CanFrame", ::flowboard::CanFrame)
OP_REGISTER_TAP_FACTORY  ("flowboard::CanFrame", ::flowboard::CanFrame)
OP_REGISTER_PORT_FACTORY ("flowboard::CanFrame", ::flowboard::CanFrame)
