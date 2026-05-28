// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/list_value.hpp"
#include "flowboard/log_registry.hpp"
#include "flowboard/log_sink.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

namespace {

// One factory per primitive type tag. Generated *_nodes.cpp files contribute
// the same kind of factory for every struct via OP_REGISTER_LOG_FACTORY,
// so any registered type — primitive or onboardapi struct — is loggable.
template <typename T>
std::unique_ptr<Node> make_logsink(std::string id, ::nlohmann::json const& cfg) {
    return std::make_unique<LogSinkT<T>>(std::move(id), cfg);
}

}  // namespace

OP_REGISTER_LOG_FACTORY("flowboard::Bool",   &make_logsink<bool>)
OP_REGISTER_LOG_FACTORY("flowboard::Int64",  &make_logsink<std::int64_t>)
OP_REGISTER_LOG_FACTORY("flowboard::Double", &make_logsink<double>)
OP_REGISTER_LOG_FACTORY("flowboard::String", &make_logsink<std::string>)
OP_REGISTER_LOG_FACTORY("flowboard::List",   &make_logsink<ListValue>)
OP_REGISTER_LOG_FACTORY("flowboard::UInt8",  &make_logsink<std::uint8_t>)
OP_REGISTER_LOG_FACTORY("flowboard::Int16",  &make_logsink<std::int16_t>)
OP_REGISTER_LOG_FACTORY("flowboard::UInt16", &make_logsink<std::uint16_t>)
OP_REGISTER_LOG_FACTORY("flowboard::Int32",  &make_logsink<std::int32_t>)
OP_REGISTER_LOG_FACTORY("flowboard::UInt32", &make_logsink<std::uint32_t>)
OP_REGISTER_LOG_FACTORY("flowboard::UInt64", &make_logsink<std::uint64_t>)
OP_REGISTER_LOG_FACTORY("flowboard::Float",  &make_logsink<float>)
OP_REGISTER_LOG_FACTORY("flowboard::Char",   &make_logsink<char>)

static auto _logsink_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    auto fn = ::flowboard::lookup_log_factory(t);
    if (!fn)
        throw std::runtime_error("Sinks.Log: unsupported inputType '" + t + "'");
    return fn(std::move(id), cfg);
};

// Schema deliberately keeps `inputType` as an open string instead of an
// `enum` whitelist — the set of valid types is whatever the runtime has
// registered via OP_REGISTER_LOG_FACTORY (primitives here, structs from
// generated *_nodes.cpp). The web Inspector reads `registered_log_types()`
// to populate the dropdown, so the truth lives in the registry, not the
// schema. `path` is optional; empty means "log to the default spdlog sink".
OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Sinks.Log",
    _logsink_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType"],
      "properties":{
        "inputType":{"type":"string","title":"Input type","default":"flowboard::Double"},
        "prefix":   {"type":"string","title":"Prefix","default":"value"},
        "path":     {"type":"string","title":"File path","default":"",
                     "description":"Append log lines to this file. Empty = use the default spdlog logger (stdout)."}
      },
      "additionalProperties":false
    })",
    R"({"inputType":"flowboard::Double","prefix":"value","path":""})",
    logsink
)

}  // namespace flowboard::nodes
