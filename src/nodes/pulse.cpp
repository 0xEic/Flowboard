// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

#include <memory>

namespace flowboard::nodes {

// Emits a `true` pulse on `out` whenever the boolean on `in` changes (the first
// value always counts as a change). Turns a level signal — e.g. a pushbutton
// that emits true on press and false on release — into an edge-triggered pulse,
// so both the press and the release fire a downstream trigger that only acts on
// `true` (service request groups, ConstantSource, Synchronize.forceOutput, …).
// Repeated identical values are suppressed.
class Pulse : public Node {
public:
    Pulse(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Transform.Pulse"),
          in_("in"), out_("out") {
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](InputPort<bool>::Value v) {
            enqueue([this, v] {
                if (!v) return;
                if (last_ && *last_ == *v) return;  // unchanged — suppress
                last_ = v;
                out_.emit(std::make_shared<const bool>(true));
            });
        });
    }
private:
    InputPort<bool>  in_;
    OutputPort<bool> out_;
    std::shared_ptr<const bool> last_;  // last value seen (empty until first)
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Transform.Pulse",
    Pulse,
    R"({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{},
      "additionalProperties":false
    })",
    R"({})")

}  // namespace flowboard::nodes
