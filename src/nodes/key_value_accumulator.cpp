// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/key_value_accumulator.hpp"
#include "flowboard/key_value_accumulator_registry.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

namespace {

template <typename TKey, typename TValue>
std::unique_ptr<Node> make_accumulator(std::string id, ::nlohmann::json const& cfg) {
    return std::make_unique<KeyValueAccumulator<TKey, TValue>>(std::move(id), cfg);
}

// Register the given key type against every primitive value type.
template <typename TKey>
void register_key(char const* key_tag) {
    using ::flowboard::register_key_value_accumulator_factory;
    register_key_value_accumulator_factory(key_tag, "flowboard::Bool",   &make_accumulator<TKey, bool>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Int64",  &make_accumulator<TKey, std::int64_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::UInt64", &make_accumulator<TKey, std::uint64_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Int32",  &make_accumulator<TKey, std::int32_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::UInt32", &make_accumulator<TKey, std::uint32_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Int16",  &make_accumulator<TKey, std::int16_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::UInt16", &make_accumulator<TKey, std::uint16_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::UInt8",  &make_accumulator<TKey, std::uint8_t>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Float",  &make_accumulator<TKey, float>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Double", &make_accumulator<TKey, double>);
    register_key_value_accumulator_factory(key_tag, "flowboard::String", &make_accumulator<TKey, std::string>);
    register_key_value_accumulator_factory(key_tag, "flowboard::Char",   &make_accumulator<TKey, char>);
}

}  // namespace

// Default registrations: every (primitive key, primitive value) pair. Key*
// parameters in the SDK resolve to a registered primitive — usually a string
// or an integer index (e.g. ReportAxis.KeyAxisIndex is unsigned long = UInt32).
// Struct value types are contributed by generated *_nodes.cpp via the same
// OP_REGISTER_KEY_VALUE_ACCUMULATOR_FACTORY macro (string-keyed).
namespace {
const bool _kv_register_all = [] {
    register_key<std::string>  ("flowboard::String");
    register_key<std::int64_t> ("flowboard::Int64");
    register_key<std::uint64_t>("flowboard::UInt64");
    register_key<std::int32_t> ("flowboard::Int32");
    register_key<std::uint32_t>("flowboard::UInt32");
    register_key<std::int16_t> ("flowboard::Int16");
    register_key<std::uint16_t>("flowboard::UInt16");
    register_key<std::uint8_t> ("flowboard::UInt8");
    register_key<bool>         ("flowboard::Bool");
    register_key<char>         ("flowboard::Char");
    return true;
}();
}  // namespace

static auto _kv_accumulator_factory =
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto key_t   = cfg.at("keyType"  ).get<std::string>();
    auto value_t = cfg.at("valueType").get<std::string>();
    auto fn = ::flowboard::lookup_key_value_accumulator_factory(key_t, value_t);
    if (!fn) {
        throw std::runtime_error(
            "Transform.KeyValueAccumulator: unsupported (keyType, valueType) pair "
            "(" + key_t + ", " + value_t + ")");
    }
    return fn(std::move(id), cfg);
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.KeyValueAccumulator",
    _kv_accumulator_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["keyType","valueType"],
      "properties":{
        "keyType":  {"type":"string","title":"Key type","default":"flowboard::String",
                     "description":"Type tag of the key port. Usually flowboard::String — every Key* parameter in the OnboardAPI aliases to a string."},
        "valueType":{"type":"string","title":"Value type","default":"flowboard::String",
                     "description":"Type tag of the value port. Pick any registered primitive or generated struct."},
        "elementTypeTag":{"type":"string","title":"Element type hint","default":"",
                          "description":"Optional informational tag echoed on the emitted ListValue. Defaults to '' (unset)."},
        "orderBy": {"type":"string","title":"Order","default":"insertion",
                    "enum":["insertion","key"],
                    "description":"insertion = order entries by first-seen time. key = lexicographic by JSON-stringified key."},
        "emitOnEmpty":{"type":"boolean","title":"Emit on no-op remove","default":true,
                       "description":"When true, an isRemoved=true for an absent key still re-emits the (unchanged) list."}
      },
      "additionalProperties":false
    })",
    R"({"keyType":"flowboard::String","valueType":"flowboard::String","elementTypeTag":"","orderBy":"insertion","emitOnEmpty":true})",
    key_value_accumulator)

}  // namespace flowboard::nodes
