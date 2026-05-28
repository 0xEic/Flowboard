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

// Transform.NumberHolder — holds a single number of the configured type and
// mutates it from three inputs, emitting the current value on every change:
//   initValue : sets (or resets) the held value to the received value.
//   increase  : adds the received value to the held value.
//   decrease  : subtracts the received value from the held value.
// The held value lives on the node's single worker thread (all inputs serialise
// through enqueue), so no locking is needed. Emits the initial value on start
// when emitOnInit is set.
template <typename T>
class NumberHolderT final : public Node {
public:
    NumberHolderT(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Transform.NumberHolder"),
          init_("initValue"), inc_("increase"), dec_("decrease"), out_("value"),
          value_(static_cast<T>(cfg.value("initValue", 0.0))),
          emit_on_init_(cfg.value("emitOnInit", true)) {
        register_input(&init_);
        register_input(&inc_);
        register_input(&dec_);
        register_output(&out_);
    }
    void on_start() override {
        init_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] { if (v) { value_ = *v; emit(); } });
        });
        inc_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] { if (v) { value_ = static_cast<T>(value_ + *v); emit(); } });
        });
        dec_.set_internal_sink([this](typename InputPort<T>::Value v) {
            enqueue([this, v] { if (v) { value_ = static_cast<T>(value_ - *v); emit(); } });
        });
        if (emit_on_init_) enqueue([this] { emit(); });
    }
private:
    void emit() { out_.emit(std::make_shared<const T>(value_)); }

    InputPort<T>  init_, inc_, dec_;
    OutputPort<T> out_;
    T             value_;
    bool          emit_on_init_;
};

static auto _number_holder_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    auto t = cfg.value("valueType", std::string{"flowboard::Double"});
    if (t == "flowboard::Double") return std::make_unique<NumberHolderT<double>>(std::move(id), cfg);
    if (t == "flowboard::Float")  return std::make_unique<NumberHolderT<float>>(std::move(id), cfg);
    if (t == "flowboard::Int64")  return std::make_unique<NumberHolderT<std::int64_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt64") return std::make_unique<NumberHolderT<std::uint64_t>>(std::move(id), cfg);
    if (t == "flowboard::Int32")  return std::make_unique<NumberHolderT<std::int32_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt32") return std::make_unique<NumberHolderT<std::uint32_t>>(std::move(id), cfg);
    if (t == "flowboard::Int16")  return std::make_unique<NumberHolderT<std::int16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt16") return std::make_unique<NumberHolderT<std::uint16_t>>(std::move(id), cfg);
    if (t == "flowboard::UInt8")  return std::make_unique<NumberHolderT<std::uint8_t>>(std::move(id), cfg);
    throw std::runtime_error("Transform.NumberHolder: unsupported valueType '" + t + "' (primitive numbers only)");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Transform.NumberHolder",
    _number_holder_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Number Holder",
      "description":"Holds a number of the chosen type. 'initValue' sets/resets it, 'increase' adds, 'decrease' subtracts; the current value is emitted on every change.",
      "required":["valueType"],
      "properties":{
        "valueType":{"type":"string","title":"Value type","default":"flowboard::Double",
                     "enum":["flowboard::Double","flowboard::Float","flowboard::Int64","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8"],
                     "description":"Numeric type of the held value and all three inputs."},
        "initValue":{"type":"number","title":"Initial value","default":0,
                     "description":"Starting held value (also the default for the initValue input port)."},
        "emitOnInit":{"type":"boolean","title":"Emit on init","default":true,
                      "description":"Emit the initial value once when the graph starts."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"valueType":"flowboard::Double","initValue":0,"emitOnInit":true})JSON",
    number_holder
)

}  // namespace flowboard::nodes
