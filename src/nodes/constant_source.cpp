// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace flowboard::nodes {

// Emits a fixed primitive value. `trigger`=true re-emits it; with
// autoTriggerOnInit (default true) it also emits once when the graph starts.
// Struct/onboardapi values are produced by the per-type Factory.* nodes, not
// here — ConstantSource is primitive-only.
template <typename T>
class ConstantSourceT : public Node {
public:
    ConstantSourceT(std::string id, T value, bool auto_trigger)
        : Node(std::move(id), "Transform.ConstantSource"),
          trig_("trigger"), out_("out"),
          value_(std::make_shared<const T>(std::move(value))),
          auto_trigger_(auto_trigger) {
        register_input(&trig_); register_output(&out_);
    }
    void on_start() override {
        trig_.set_internal_sink([this](auto v) {
            enqueue([this, v] { if (v && *v) out_.emit(value_); });
        });
        if (auto_trigger_) enqueue([this] { out_.emit(value_); });
    }
private:
    InputPort<bool> trig_;
    OutputPort<T>   out_;
    std::shared_ptr<const T> value_;
    bool auto_trigger_;
};

namespace {
// Read `value` as T, falling back to `def` if it is missing or of the wrong
// JSON kind (e.g. a stale string left over from switching outputType to Bool).
template <typename T>
std::unique_ptr<Node> make_cs(std::string id, nlohmann::json const& cfg, T def) {
    T v = def;
    if (auto it = cfg.find("value"); it != cfg.end() && !it->is_null()) {
        try { v = it->get<T>(); } catch (...) { v = def; }
    }
    return std::make_unique<ConstantSourceT<T>>(
        std::move(id), std::move(v), cfg.value("autoTriggerOnInit", true));
}
}  // namespace

static auto _constsource_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("outputType", std::string{"flowboard::String"});
    bool autotrig = cfg.value("autoTriggerOnInit", true);
    if (t == "flowboard::Bool")   return make_cs<bool>        (std::move(id), cfg, false);
    if (t == "flowboard::Int64")  return make_cs<std::int64_t>(std::move(id), cfg, std::int64_t{0});
    if (t == "flowboard::Double") return make_cs<double>      (std::move(id), cfg, 0.0);
    if (t == "flowboard::String") return make_cs<std::string> (std::move(id), cfg, std::string{});
    if (t == "flowboard::UInt8")  return make_cs<std::uint8_t> (std::move(id), cfg, std::uint8_t{0});
    if (t == "flowboard::Int16")  return make_cs<std::int16_t> (std::move(id), cfg, std::int16_t{0});
    if (t == "flowboard::UInt16") return make_cs<std::uint16_t>(std::move(id), cfg, std::uint16_t{0});
    if (t == "flowboard::Int32")  return make_cs<std::int32_t> (std::move(id), cfg, std::int32_t{0});
    if (t == "flowboard::UInt32") return make_cs<std::uint32_t>(std::move(id), cfg, std::uint32_t{0});
    if (t == "flowboard::UInt64") return make_cs<std::uint64_t>(std::move(id), cfg, std::uint64_t{0});
    if (t == "flowboard::Float")  return make_cs<float>       (std::move(id), cfg, 0.0f);
    if (t == "flowboard::Char") {
        char c = 0;
        if (cfg.contains("value")) {
            auto const& jv = cfg.at("value");
            if (jv.is_string()) { auto s = jv.get<std::string>(); if (!s.empty()) c = s[0]; }
            else if (jv.is_number()) c = static_cast<char>(jv.get<int>());
        }
        return std::make_unique<ConstantSourceT<char>>(std::move(id), c, autotrig);
    }
    throw std::runtime_error("Transform.ConstantSource: unsupported outputType '" + t + "'");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.ConstantSource",
    _constsource_factory,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["outputType","value"],
      "properties":{
        "outputType":{"type":"string","title":"Output type",
          "enum":["flowboard::Bool","flowboard::Int64","flowboard::Double","flowboard::String","flowboard::Float","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8","flowboard::Char"],
          "default":"flowboard::String"},
        "value":{"title":"Value"},
        "autoTriggerOnInit":{"type":"boolean","title":"Auto-trigger on init","default":true,
          "description":"Emit the value once when the graph starts (in addition to on every trigger=true)."}
      },
      "additionalProperties":false
    })",
    R"({"outputType":"flowboard::String","value":"","autoTriggerOnInit":true})",
    constsource
)

}  // namespace flowboard::nodes
