// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

namespace flowboard::nodes {

// Emits the logical negation of every boolean received on `in`.
class Inverter : public Node {
public:
    Inverter(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Transform.Inverter"),
          in_("in"), out_("out") {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](InputPort<bool>::Value v) {
            enqueue([this, v] {
                if (v) out_.emit(std::make_shared<const bool>(!*v));
            });
        });
    }
private:
    InputPort<bool>  in_;
    OutputPort<bool> out_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.Inverter",
    Inverter,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{},
      "additionalProperties":false
    })",
    R"({})")

}  // namespace flowboard::nodes
