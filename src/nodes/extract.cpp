// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/extract_registry.hpp"
#include <stdexcept>

namespace flowboard::nodes {

static auto _extract_factory =
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    std::string input_type = cfg.at("inputType").get<std::string>();
    std::string field      = cfg.at("field"    ).get<std::string>();
    auto fn = ::flowboard::lookup_extract_factory(input_type);
    if (!fn)
        throw std::runtime_error(
            "Transform.Extract: unknown inputType '" + input_type + "'");
    auto node = fn(std::move(id), field);
    if (!node)
        throw std::runtime_error(
            "Transform.Extract: unknown field '" + field +
            "' for inputType " + input_type);
    return node;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Extract",
    _extract_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType","field"],
      "properties":{
        "inputType":{"type":"string","title":"Input type"},
        "field":    {"type":"string","title":"Field"}
      },
      "additionalProperties":false
    })",
    R"({"inputType":"M_Mount::MountPositionType","field":"Elevation"})",
    extract
)

}  // namespace flowboard::nodes
