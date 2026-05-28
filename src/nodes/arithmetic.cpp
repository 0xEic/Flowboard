// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "flowboard/log.hpp"
#include "builtin_types.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace flowboard::nodes {

// Transform.Arithmetic — combines two numeric inputs (a, b) into one output of
// the same type. Latches the last value on each input and emits op(a,b) on
// every update once both have been seen (the multi-input latch pattern used by
// Compare/Synchronize). Numeric primitives only.

enum class ArithOp { Add, Sub, Mul, Div, Min, Max };

inline ArithOp parse_arith_op(std::string_view s) {
    if (s == "add"      || s == "+") return ArithOp::Add;
    if (s == "subtract" || s == "-") return ArithOp::Sub;
    if (s == "multiply" || s == "*") return ArithOp::Mul;
    if (s == "divide"   || s == "/") return ArithOp::Div;
    if (s == "min") return ArithOp::Min;
    if (s == "max") return ArithOp::Max;
    throw std::runtime_error("Transform.Arithmetic: unknown op '" + std::string(s) + "'");
}

template <typename T>
inline T apply_arith(ArithOp op, T a, T b) {
    switch (op) {
        case ArithOp::Add: return static_cast<T>(a + b);
        case ArithOp::Sub: return static_cast<T>(a - b);
        case ArithOp::Mul: return static_cast<T>(a * b);
        case ArithOp::Div: return static_cast<T>(a / b);
        case ArithOp::Min: return std::min(a, b);
        case ArithOp::Max: return std::max(a, b);
    }
    return a;
}

template <typename T>
class ArithmeticT : public Node {
public:
    ArithmeticT(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Arithmetic"),
          a_("a"), b_("b"), out_("out"),
          op_(parse_arith_op(cfg.value("op", "add"))) {
        register_input(&a_);
        register_input(&b_);
        register_output(&out_);
    }
    void on_start() override {
        a_.set_internal_sink([this](auto v) { enqueue([this, v] { last_a_ = v; maybe_emit(); }); });
        b_.set_internal_sink([this](auto v) { enqueue([this, v] { last_b_ = v; maybe_emit(); }); });
    }
private:
    void maybe_emit() {
        if (!last_a_ || !last_b_) return;
        T a = *last_a_, b = *last_b_;
        // Integer divide-by-zero is undefined behaviour — skip the emission and
        // warn rather than crash. (Floating-point div by zero yields inf/nan,
        // which is well-defined, so it passes through.)
        if (op_ == ArithOp::Div && std::is_integral_v<T> && b == T{0}) {
            spdlog::warn("[{}] Transform.Arithmetic: integer divide by zero — skipped", std::string(id()));
            return;
        }
        out_.emit(std::make_shared<const T>(apply_arith<T>(op_, a, b)));
    }
    InputPort<T> a_, b_;
    OutputPort<T> out_;
    ArithOp op_;
    std::shared_ptr<const T> last_a_, last_b_;
};

static auto _arithmetic_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    if (t == "flowboard::Char")   return std::make_unique<ArithmeticT<char>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<ArithmeticT<std::uint8_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<ArithmeticT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<ArithmeticT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<ArithmeticT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<ArithmeticT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<ArithmeticT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<ArithmeticT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<ArithmeticT<float>>(std::move(id), cfg);
    if (t == "flowboard::Double") return std::make_unique<ArithmeticT<double>>(std::move(id), cfg);
    throw std::runtime_error("Transform.Arithmetic: unsupported inputType '" + t +
                             "' (numeric primitives only)");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Arithmetic",
    _arithmetic_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Arithmetic",
      "description":"Combines two numeric inputs a and b into one output of the same type. Emits op(a,b) whenever either input updates (after both have a value).",
      "required":["inputType","op"],
      "properties":{
        "inputType":{"type":"string","title":"Input type",
          "enum":["flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double"],
          "default":"flowboard::Double"},
        "op":{"type":"string","title":"Operation",
          "enum":["add","subtract","multiply","divide","min","max"],
          "default":"add"}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","op":"add"})JSON",
    arithmetic
)

}  // namespace flowboard::nodes
