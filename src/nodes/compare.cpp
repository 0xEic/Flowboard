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

namespace flowboard::nodes {

// --- relational comparison (numeric / bool / char) --------------------------

enum class CmpOp { Gt, Ge, Lt, Le, Eq, Ne };

inline CmpOp parse_op_c(std::string_view s) {
    if (s == ">")  return CmpOp::Gt;
    if (s == ">=") return CmpOp::Ge;
    if (s == "<")  return CmpOp::Lt;
    if (s == "<=") return CmpOp::Le;
    if (s == "==") return CmpOp::Eq;
    if (s == "!=") return CmpOp::Ne;
    throw std::runtime_error("Compare: unknown op '" + std::string(s) + "'");
}

template <typename T>
inline bool apply_c(CmpOp op, T const& a, T const& b) {
    switch (op) {
        case CmpOp::Gt: return a >  b;
        case CmpOp::Ge: return a >= b;
        case CmpOp::Lt: return a <  b;
        case CmpOp::Le: return a <= b;
        case CmpOp::Eq: return a == b;
        case CmpOp::Ne: return a != b;
    }
    return false;
}

template <typename T>
class CompareT : public Node {
public:
    CompareT(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Compare"),
          a_("a"), b_("b"), out_("out"),
          op_(parse_op_c(cfg.value("op", ">"))) {
        register_input(&a_);
        register_input(&b_);
        register_output(&out_);
    }
    void on_start() override {
        a_.set_internal_sink([this](auto v) {
            enqueue([this, v] { last_a_ = v; maybe_emit(); });
        });
        b_.set_internal_sink([this](auto v) {
            enqueue([this, v] { last_b_ = v; maybe_emit(); });
        });
    }
private:
    void maybe_emit() {
        if (!last_a_ || !last_b_) return;
        out_.emit(std::make_shared<const bool>(apply_c<T>(op_, *last_a_, *last_b_)));
    }
    InputPort<T> a_, b_;
    OutputPort<bool> out_;
    CmpOp op_;
    std::shared_ptr<const T> last_a_, last_b_;
};

// --- string comparison ------------------------------------------------------

enum class StrOp { Eq, Ne, Contains, StartsWith, EndsWith };

inline StrOp parse_str_op(std::string_view s) {
    if (s == "equals"     || s == "==") return StrOp::Eq;
    if (s == "notEquals"  || s == "!=") return StrOp::Ne;
    if (s == "contains")   return StrOp::Contains;
    if (s == "startsWith") return StrOp::StartsWith;
    if (s == "endsWith")   return StrOp::EndsWith;
    throw std::runtime_error("Compare: unknown string op '" + std::string(s) + "'");
}

inline bool apply_str(StrOp op, std::string const& a, std::string const& b) {
    switch (op) {
        case StrOp::Eq: return a == b;
        case StrOp::Ne: return a != b;
        case StrOp::Contains:   return a.find(b) != std::string::npos;
        case StrOp::StartsWith: return a.size() >= b.size() && a.compare(0, b.size(), b) == 0;
        case StrOp::EndsWith:   return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
    }
    return false;
}

class CompareString : public Node {
public:
    CompareString(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Compare"),
          a_("a"), b_("b"), out_("out"),
          op_(parse_str_op(cfg.value("op", "equals"))) {
        register_input(&a_);
        register_input(&b_);
        register_output(&out_);
    }
    void on_start() override {
        a_.set_internal_sink([this](auto v) {
            enqueue([this, v] { last_a_ = v; maybe_emit(); });
        });
        b_.set_internal_sink([this](auto v) {
            enqueue([this, v] { last_b_ = v; maybe_emit(); });
        });
    }
private:
    void maybe_emit() {
        if (!last_a_ || !last_b_) return;
        out_.emit(std::make_shared<const bool>(apply_str(op_, *last_a_, *last_b_)));
    }
    InputPort<std::string> a_, b_;
    OutputPort<bool> out_;
    StrOp op_;
    std::shared_ptr<const std::string> last_a_, last_b_;
};

static auto _compare_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    if (t == "flowboard::Bool")   return std::make_unique<CompareT<bool>>(std::move(id), cfg);
    if (t == "flowboard::Char")   return std::make_unique<CompareT<char>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<CompareT<std::uint8_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<CompareT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<CompareT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<CompareT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<CompareT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<CompareT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<CompareT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<CompareT<float>>(std::move(id), cfg);
    if (t == "flowboard::Double") return std::make_unique<CompareT<double>>(std::move(id), cfg);
    if (t == "flowboard::String") return std::make_unique<CompareString>(std::move(id), cfg);
    throw std::runtime_error("Transform.Compare: unsupported inputType '" + t + "'");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Compare",
    _compare_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType","op"],
      "properties":{
        "inputType":{"type":"string",
          "enum":["flowboard::Bool","flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double","flowboard::String"],
          "default":"flowboard::Double"},
        "op":{"type":"string",
          "enum":[">",">=","<","<=","==","!=","equals","notEquals","contains","startsWith","endsWith"],
          "default":">"}
      },
      "additionalProperties":false
    })",
    R"({"inputType":"flowboard::Double","op":">"})",
    compare
)

}  // namespace flowboard::nodes
