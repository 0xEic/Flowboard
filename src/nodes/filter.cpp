// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

namespace flowboard::nodes {

// Parse the configured `defaultValue` (free-form JSON) into the port type T,
// coercing across JSON kinds so a number/string/bool default all work.
template <class T>
T parse_default(nlohmann::json const& j) {
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

// Gated pass-through. `out` reflects the latest `in` while the gate is open:
//   - an `in` value emits immediately if the gate is open;
//   - opening the gate re-emits the latest `in` (so `out` catches up);
//   - closing the gate either holds the last value (closedBehavior="hold",
//     default) or emits a configured default (closedBehavior="default").
//
// Note: `in` and `gate` arrive on independent edges/threads, so their relative
// ordering at a logical instant is not guaranteed. Re-evaluating on both ports
// makes `out` eventually consistent with (gate ? in : hold/default), but a
// boundary reorder can still briefly hold a stale value under "hold" — that is
// inherent to gating on a separately-streamed boolean.
template <typename T>
class FilterT : public Node {
public:
    FilterT(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.Filter"),
          in_("in"), gate_("gate"), out_("out"),
          emit_default_on_close_(cfg.value("closedBehavior", std::string{"hold"}) == "default"),
          default_value_(std::make_shared<const T>(
              parse_default<T>(cfg.value("defaultValue", nlohmann::json{})))) {
        register_input(&in_); register_input(&gate_); register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] {
                last_in_ = v;
                if (open_) out_.emit(v);
            });
        });
        gate_.set_internal_sink([this](InputPort<bool>::Value v) {
            enqueue([this, v] {
                const bool g   = v && *v;
                const bool was = open_;
                open_ = g;
                if (g && !was) {
                    // Gate opened: push the current value so `out` reflects it.
                    if (last_in_) out_.emit(last_in_);
                } else if (!g && was) {
                    // Gate closed: hold (emit nothing) or fall back to default.
                    if (emit_default_on_close_) out_.emit(default_value_);
                }
            });
        });
    }
private:
    InputPort<T>     in_;
    InputPort<bool>  gate_;
    OutputPort<T>    out_;
    bool             open_{false};
    bool             emit_default_on_close_;
    std::shared_ptr<const T> default_value_;
    std::shared_ptr<const T> last_in_;
};

static auto _filter_factory = [](std::string id, nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("inputType", std::string{"flowboard::Double"});
    if (t == "flowboard::Bool")   return std::make_unique<FilterT<bool>>(std::move(id), cfg);
    if (t == "flowboard::Char")   return std::make_unique<FilterT<char>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<FilterT<std::uint8_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<FilterT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<FilterT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<FilterT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<FilterT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<FilterT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<FilterT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<FilterT<float>>(std::move(id), cfg);
    if (t == "flowboard::Double") return std::make_unique<FilterT<double>>(std::move(id), cfg);
    if (t == "flowboard::String") return std::make_unique<FilterT<std::string>>(std::move(id), cfg);
    throw std::runtime_error("Transform.Filter: unsupported inputType '" + t + "'");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.Filter",
    _filter_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType"],
      "properties":{
        "inputType":{"type":"string","enum":["flowboard::Bool","flowboard::Char","flowboard::UInt8","flowboard::Int16","flowboard::UInt16","flowboard::Int32","flowboard::UInt32","flowboard::Int64","flowboard::UInt64","flowboard::Float","flowboard::Double","flowboard::String"],"default":"flowboard::Double"},
        "closedBehavior":{"type":"string","enum":["hold","default"],"default":"hold",
          "description":"What out does when the gate is closed (false). hold = keep the last passed value; default = emit the configured default value."},
        "defaultValue":{"title":"Default value (used when closedBehavior = default)"}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","closedBehavior":"hold"})JSON",
    filter
)

}  // namespace flowboard::nodes
