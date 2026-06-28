// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/synchronize.hpp"
#include "flowboard/synchronize_registry.hpp"

namespace flowboard::nodes {

namespace {
// One cell factory per primitive type tag. Generated *_nodes.cpp files contribute
// the same kind of factory for every onboardapi struct via
// OP_REGISTER_SYNCHRONIZE_FACTORY, so any registered type — primitive or struct —
// can be a Synchronize pair, and pairs may mix types within one node.
template <typename T>
std::unique_ptr<::flowboard::ISyncCell> make_sync_cell(std::string in_name, std::string out_name) {
    return std::make_unique<::flowboard::SyncCellT<T>>(std::move(in_name), std::move(out_name));
}
}  // namespace

OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Bool",   &make_sync_cell<bool>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Char",   &make_sync_cell<char>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt8",  &make_sync_cell<std::uint8_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int16",  &make_sync_cell<std::int16_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt16", &make_sync_cell<std::uint16_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int32",  &make_sync_cell<std::int32_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt32", &make_sync_cell<std::uint32_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Int64",  &make_sync_cell<std::int64_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::UInt64", &make_sync_cell<std::uint64_t>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Float",  &make_sync_cell<float>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::Double", &make_sync_cell<double>)
OP_REGISTER_SYNCHRONIZE_FACTORY("flowboard::String", &make_sync_cell<std::string>)

static auto _sync_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    return std::make_unique<::flowboard::SynchronizeNode>(std::move(id), cfg);
};

// `inputType`/`inputTypes` are open strings, not an enum whitelist: the valid set
// is whatever the runtime registered (primitives here, structs from generated
// *_nodes.cpp). The web Inspector reads `registered_synchronize_types()` (via
// /api/synchronize_types) for the per-input dropdowns.
OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Synchronize",
    _sync_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputCount"],
      "properties":{
        "inputType":{"type":"string","title":"Value type (fallback)","default":"flowboard::Double",
          "description":"Type for pairs not covered by inputTypes. When inputTypes is omitted it applies to every pair (back-compat)."},
        "inputTypes":{"type":"array","title":"Per-input types","items":{"type":"string"},"default":[],
          "description":"Type tag per in/out pair (in0/out0, in1/out1, ...). Overrides inputType per index; its length sets the number of pairs."},
        "inputCount":{"type":"integer","title":"Number of inputs","minimum":1,"maximum":64,"default":2},
        "order":{"type":"array","title":"Output emission order","items":{"type":"integer"},"default":[0,1],
                 "description":"Order in which the value outputs (out0..) are emitted between beforeOutput and afterOutput."},
        "defaults":{"type":"object","title":"Per-input default values","default":{},"additionalProperties":true,
                 "description":"Default value per input port name (in0, in1, ...); used by forceOutput when that input has no current value."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","inputCount":2,"order":[0,1]})JSON",
    synchronize
)

}  // namespace flowboard::nodes
