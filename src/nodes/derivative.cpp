// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "builtin_types.hpp"
#include "flowboard/derivative.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

namespace {
template <typename T>
std::unique_ptr<Node> make_derivative(std::string id, ::nlohmann::json const& cfg) {
    return std::make_unique<::flowboard::DerivativeT<T>>(std::move(id), cfg);
}

using DerivFactory = std::unique_ptr<Node> (*)(std::string, ::nlohmann::json const&);

// Numeric primitives only — a rate of change is undefined for bool/string and
// non-scalar types. The output is always Double regardless of input type.
DerivFactory derivative_factory_for(std::string const& tag) {
    static const std::unordered_map<std::string, DerivFactory> kMap = {
        {"flowboard::Char",   &make_derivative<char>},
        {"flowboard::UInt8",  &make_derivative<std::uint8_t>},
        {"flowboard::Int16",  &make_derivative<std::int16_t>},
        {"flowboard::UInt16", &make_derivative<std::uint16_t>},
        {"flowboard::Int32",  &make_derivative<std::int32_t>},
        {"flowboard::UInt32", &make_derivative<std::uint32_t>},
        {"flowboard::Int64",  &make_derivative<std::int64_t>},
        {"flowboard::UInt64", &make_derivative<std::uint64_t>},
        {"flowboard::Float",  &make_derivative<float>},
        {"flowboard::Double", &make_derivative<double>},
    };
    auto it = kMap.find(tag);
    return it == kMap.end() ? nullptr : it->second;
}
}  // namespace

static auto _derivative_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    auto fn = derivative_factory_for(t);
    if (!fn)
        throw std::runtime_error(
            "Transform.Derivative: unsupported inputType '" + t + "' (numeric primitives only)");
    return fn(std::move(id), cfg);
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Derivative",
    _derivative_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType"],
      "properties":{
        "inputType":{"type":"string","title":"Input type","default":"flowboard::Double",
          "enum":["flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double"],
          "description":"Numeric input type. The output is always a Double rate (input units per second)."}
      },
      "additionalProperties":false
    })",
    R"({"inputType":"flowboard::Double"})",
    derivative
)

}  // namespace flowboard::nodes
