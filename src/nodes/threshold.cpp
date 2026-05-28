// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace flowboard::nodes {

enum class CmpOp { Gt, Ge, Lt, Le, Eq, Ne };

inline CmpOp parse_op(std::string_view s) {
    if (s == ">")  return CmpOp::Gt;
    if (s == ">=") return CmpOp::Ge;
    if (s == "<")  return CmpOp::Lt;
    if (s == "<=") return CmpOp::Le;
    if (s == "==") return CmpOp::Eq;
    if (s == "!=") return CmpOp::Ne;
    throw std::runtime_error(std::string("Threshold: unknown op ") + std::string(s));
}

template <typename T>
inline bool apply(CmpOp op, T const& lhs, T const& rhs) {
    switch (op) {
        case CmpOp::Gt: return lhs >  rhs;
        case CmpOp::Ge: return lhs >= rhs;
        case CmpOp::Lt: return lhs <  rhs;
        case CmpOp::Le: return lhs <= rhs;
        case CmpOp::Eq: return lhs == rhs;
        case CmpOp::Ne: return lhs != rhs;
    }
    return false;
}

// Parse the configured threshold `value` (free-form JSON) into the port type T,
// coercing across JSON kinds so a number/string/bool config all work.
template <class T>
T parse_value(nlohmann::json const& j) {
    if (j.is_null()) return T{};
    try {
        if constexpr (std::is_same_v<T, std::string>) {
            return j.is_string() ? j.get<std::string>() : j.dump();
        } else if constexpr (std::is_same_v<T, bool>) {
            if (j.is_boolean()) return j.get<bool>();
            if (j.is_number())  return j.get<double>() != 0.0;
            if (j.is_string())  { auto s = j.get<std::string>(); return s == "true" || s == "1"; }
            return false;
        } else if constexpr (std::is_same_v<T, char>) {
            if (j.is_string()) { auto s = j.get<std::string>(); return s.empty() ? char(0) : s[0]; }
            if (j.is_number()) return static_cast<char>(j.get<int>());
            return char(0);
        } else {  // numeric
            if (j.is_number())  return static_cast<T>(j.get<double>());
            if (j.is_boolean()) return j.get<bool>() ? T(1) : T(0);
            if (j.is_string())  { try { return static_cast<T>(std::stod(j.get<std::string>())); } catch (...) { return T{}; } }
            return T{};
        }
    } catch (...) { return T{}; }
}

// Compares each incoming value against a fixed threshold with the configured
// operator and emits the boolean result.
template <typename T>
class ThresholdT : public Node {
public:
    ThresholdT(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Threshold"),
          in_("in"), out_("out"),
          op_(parse_op(cfg.value("op", ">"))),
          value_(parse_value<T>(cfg.value("value", nlohmann::json{}))) {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] {
                out_.emit(std::make_shared<const bool>(apply<T>(op_, *v, value_)));
            });
        });
    }
private:
    InputPort<T>      in_;
    OutputPort<bool>  out_;
    CmpOp             op_;
    T                 value_;
};

static auto _threshold_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    if (t == "flowboard::Bool")   return std::make_unique<ThresholdT<bool>>(std::move(id), cfg);
    if (t == "flowboard::Char")   return std::make_unique<ThresholdT<char>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<ThresholdT<std::uint8_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<ThresholdT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<ThresholdT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<ThresholdT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<ThresholdT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<ThresholdT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<ThresholdT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<ThresholdT<float>>(std::move(id), cfg);
    if (t == "flowboard::Double") return std::make_unique<ThresholdT<double>>(std::move(id), cfg);
    if (t == "flowboard::String") return std::make_unique<ThresholdT<std::string>>(std::move(id), cfg);
    throw std::runtime_error("Transform.Threshold: unsupported inputType '" + t + "'");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Threshold",
    _threshold_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType","op","value"],
      "properties":{
        "inputType":{"type":"string","enum":["flowboard::Bool","flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double","flowboard::String"],"default":"flowboard::Double"},
        "op":{"type":"string","enum":[">",">=","<","<=","==","!="],"default":">"},
        "value":{"title":"Threshold value","default":0}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","op":">","value":0})JSON",
    threshold
)

}  // namespace flowboard::nodes
