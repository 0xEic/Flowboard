// SPDX-License-Identifier: MIT
//
// Manipulate.* — single-value transform nodes.
//   Manipulate.Number : out = formula(in)   (arithmetic expression in `x`)
//   Manipulate.String : replace / set / prepend / append on a string
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "builtin_types.hpp"
#include "flowboard/manipulate_ops.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Applies an arithmetic formula in `x` to a numeric input. The formula is
// evaluated in double; the input is widened to double for x and the result is
// cast back to the configured type T (rounded for integer types).
template <typename T>
class ManipulateNumberT final : public Node {
public:
    ManipulateNumberT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Manipulate.Number"),
          in_("in"), out_("out"),
          formula_(cfg.value("formula", std::string{"x"})) {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] {
                double x = static_cast<double>(*v);
                double r = ::flowboard::eval_formula(formula_, x, x);
                out_.emit(std::make_shared<const T>(to_value(r)));
            });
        });
    }
private:
    static T to_value(double v) {
        if constexpr (std::is_integral_v<T>) return static_cast<T>(std::llround(v));
        else return static_cast<T>(v);
    }
    InputPort<T>  in_;
    OutputPort<T> out_;
    std::string   formula_;
};

static auto _manipulate_number_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("valueType", std::string{"flowboard::Double"});
    if (t == "flowboard::Double") return std::make_unique<ManipulateNumberT<double>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<ManipulateNumberT<float>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<ManipulateNumberT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<ManipulateNumberT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<ManipulateNumberT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<ManipulateNumberT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<ManipulateNumberT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<ManipulateNumberT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<ManipulateNumberT<std::uint8_t>>(std::move(id), cfg);
    throw std::runtime_error("Manipulate.Number: unsupported valueType '" + t + "' (primitive numbers only)");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Manipulate.Number", _manipulate_number_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "valueType":{"type":"string","title":"Value type","default":"flowboard::Double",
                     "enum":["flowboard::Double","flowboard::Float","flowboard::Int64","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8"],
                     "description":"Numeric type of the in/out ports."},
        "formula":{"type":"string","title":"Formula","default":"x",
                   "description":"Arithmetic expression in x, e.g. \"x * 2 + 1\", \"clamp(x,0,100)\", \"sqrt(abs(x))\"."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"valueType":"flowboard::Double","formula":"x"})JSON",
    manipulate_number)

class ManipulateString final : public Node {
public:
    ManipulateString(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Manipulate.String"),
          in_("in"), out_("out"),
          mode_(cfg.value("mode", std::string{"replace"})),
          find_(cfg.value("find", std::string{})),
          replace_(cfg.value("replace", std::string{})),
          text_(cfg.value("text", std::string{})) {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](InputPort<std::string>::Value v) {
            enqueue([this, v] {
                auto r = ::flowboard::apply_string_op(*v, mode_, find_, replace_, text_);
                out_.emit(std::make_shared<const std::string>(std::move(r)));
            });
        });
    }
private:
    InputPort<std::string>  in_;
    OutputPort<std::string> out_;
    std::string mode_, find_, replace_, text_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Manipulate.String", ManipulateString,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "mode":{"type":"string","title":"Mode","enum":["replace","set","prepend","append"],"default":"replace",
                "description":"replace: swap snippets; set: replace whole string with text; prepend/append: extend with text."},
        "find":{"type":"string","title":"Find","default":"","description":"Snippet to replace (replace mode)."},
        "replace":{"type":"string","title":"Replace with","default":"","description":"Replacement snippet (replace mode)."},
        "text":{"type":"string","title":"Text","default":"","description":"Text for set / prepend / append modes."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"mode":"replace","find":"","replace":"","text":""})JSON")

}  // namespace flowboard::nodes
