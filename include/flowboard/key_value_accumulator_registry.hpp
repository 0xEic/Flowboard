// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

/// \file
/// \brief Registry mapping (keyType, valueType) tag pairs to key-value accumulator node factories.

namespace flowboard {

class Node;

/// \brief Factory signature: takes a node id and config; returns a Node closed over a
/// specific (keyType, valueType) pair. Registration is keyed by the pair
/// "<keyTypeTag>|<valueTypeTag>".
using KeyValueAccumulatorFactoryFn =
    std::unique_ptr<Node>(*)(std::string id, ::nlohmann::json const& cfg);

/// \brief Register a factory for the given key-type/value-type tag pair.
void register_key_value_accumulator_factory(
    std::string key_type_tag, std::string value_type_tag,
    KeyValueAccumulatorFactoryFn fn);

/// \brief Look up the factory registered for the given key-type/value-type tag pair.
KeyValueAccumulatorFactoryFn lookup_key_value_accumulator_factory(
    std::string_view key_type_tag, std::string_view value_type_tag);

/// \brief A registered (keyType, valueType) tag pair.
/// \details Used by the web Inspector to populate the (keyType, valueType) dropdowns.
struct KeyValuePair { std::string key_type; std::string value_type; };
/// \brief Return a snapshot of all currently registered (keyType, valueType) pairs.
std::vector<KeyValuePair> registered_key_value_pairs();

}  // namespace flowboard

#define OP_KV_CAT_(a, b) a##b
#define OP_KV_CAT(a, b)  OP_KV_CAT_(a, b)

#define OP_REGISTER_KEY_VALUE_ACCUMULATOR_FACTORY(KeyTagStr, ValueTagStr, FactoryFn)  \
    namespace {                                                                       \
    const bool OP_KV_CAT(_op_kv_reg_, __COUNTER__) = [] {                            \
        ::flowboard::register_key_value_accumulator_factory(                       \
            (KeyTagStr), (ValueTagStr), (FactoryFn));                                 \
        return true;                                                                  \
    }();                                                                              \
    }
