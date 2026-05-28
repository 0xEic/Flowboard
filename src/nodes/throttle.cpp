// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/throttle.hpp"
#include "flowboard/throttle_registry.hpp"

namespace flowboard::nodes {

namespace {
// One factory per primitive type tag. Generated *_nodes.cpp files contribute
// the same kind of factory for every onboardapi struct via
// OP_REGISTER_THROTTLE_FACTORY, so any registered type — primitive, list, or
// struct — can be throttled.
template <typename T>
std::unique_ptr<Node> make_throttle(std::string id, ::nlohmann::json const& cfg) {
    return std::make_unique<::flowboard::ThrottleT<T>>(std::move(id), cfg);
}
}  // namespace

OP_REGISTER_THROTTLE_FACTORY("flowboard::Bool",   &make_throttle<bool>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Char",   &make_throttle<char>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::UInt8",  &make_throttle<std::uint8_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Int16",  &make_throttle<std::int16_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::UInt16", &make_throttle<std::uint16_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Int32",  &make_throttle<std::int32_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::UInt32", &make_throttle<std::uint32_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Int64",  &make_throttle<std::int64_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::UInt64", &make_throttle<std::uint64_t>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Float",  &make_throttle<float>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::Double", &make_throttle<double>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::String", &make_throttle<std::string>)
OP_REGISTER_THROTTLE_FACTORY("flowboard::List",   &make_throttle<ListValue>)

static auto _throttle_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    auto fn = ::flowboard::lookup_throttle_factory(t);
    if (!fn)
        throw std::runtime_error("Transform.Throttle: unsupported inputType '" + t + "'");
    return fn(std::move(id), cfg);
};

// Like Sinks.Log, `inputType` is an open string rather than an enum whitelist:
// the valid set is whatever the runtime registered via OP_REGISTER_THROTTLE_FACTORY
// (primitives here, structs from generated *_nodes.cpp). The web Inspector reads
// `registered_throttle_types()` (via /api/throttle_types) to populate the dropdown.
OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Throttle",
    _throttle_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType","minPeriodMs"],
      "properties":{
        "inputType":{"type":"string","title":"Input type","default":"flowboard::Double"},
        "minPeriodMs":{"type":"integer","minimum":0,"default":100}
      },
      "additionalProperties":false
    })",
    R"({"inputType":"flowboard::Double","minPeriodMs":100})",
    throttle
)

}  // namespace flowboard::nodes
