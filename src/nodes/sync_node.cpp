// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/synchronize.hpp"
#include "flowboard/synchronize_registry.hpp"

namespace flowboard::nodes {

namespace {
// One factory per primitive type tag. Generated *_nodes.cpp files contribute the
// same kind of factory for every onboardapi struct via OP_REGISTER_SYNCHRONIZE_FACTORY,
// so any registered type — primitive or struct — can be synchronised.
template <typename T>
std::unique_ptr<Node> make_sync(std::string id, ::nlohmann::json const& cfg) {
    return std::make_unique<::flowboard::SynchronizeT<T>>(std::move(id), cfg);
}
}  // namespace

OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Bool",   &make_sync<bool>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Char",   &make_sync<char>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt8",  &make_sync<std::uint8_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int16",  &make_sync<std::int16_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt16", &make_sync<std::uint16_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int32",  &make_sync<std::int32_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt32", &make_sync<std::uint32_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int64",  &make_sync<std::int64_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt64", &make_sync<std::uint64_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Float",  &make_sync<float>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Double", &make_sync<double>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::String", &make_sync<std::string>)

static auto _sync_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    auto fn = ::flowboard::lookup_synchronize_factory(t);
    if (!fn)
        throw std::runtime_error("Transform.Synchronize: unsupported inputType '" + t + "'");
    return fn(std::move(id), cfg);
};

// Like Sinks.Log/Transform.Throttle, `inputType` is an open string rather than an
// enum whitelist: the valid set is whatever the runtime registered (primitives
// here, structs from generated *_nodes.cpp). The web Inspector reads
// `registered_synchronize_types()` (via /api/synchronize_types) for the dropdown.
OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Synchronize",
    _sync_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType","inputCount"],
      "properties":{
        "inputType":{"type":"string","title":"Value type","default":"flowboard::Double"},
        "inputCount":{"type":"integer","title":"Number of inputs","minimum":1,"maximum":64,"default":2},
        "order":{"type":"array","title":"Output emission order","items":{"type":"integer"},"default":[0,1],
                 "description":"Order in which the value outputs (out0..) are emitted between beforeOutput and afterOutput."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","inputCount":2,"order":[0,1]})JSON",
    synchronize
)

}  // namespace flowboard::nodes
