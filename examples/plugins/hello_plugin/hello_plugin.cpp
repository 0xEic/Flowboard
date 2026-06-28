// SPDX-License-Identifier: MIT
//
// Example Flowboard node plugin.
//
// Compiles into a shared library that the host loads from its plugin folder at
// startup (see docs/PLUGINS.md). It contributes two nodes:
//
//   * Plugin.Scale — multiplies a Double input by a configurable factor
//                    (one property, one input, one output).
//   * Plugin.Sum   — adds two Double inputs and emits the sum
//                    (two inputs, one output).
//
// Both use only built-in Flowboard types (Double), so the host wires them and
// streams their live values without any extra registration.

#include "flowboard/plugin.hpp"

#include <memory>
#include <optional>
#include <string>

namespace {

using namespace flowboard;

// Plugin.Scale: out = in * factor.
class ScaleNode : public Node {
public:
    ScaleNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Plugin.Scale"),
          in_("in"), out_("out"),
          factor_(cfg.value("factor", 2.0)) {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](InputPort<double>::Value v) {
            enqueue([this, v] {
                out_.emit(std::make_shared<const double>(*v * factor_));
            });
        });
    }

private:
    InputPort<double>  in_;
    OutputPort<double> out_;
    double             factor_;
};

// Plugin.Sum: out = a + b. Emits once both inputs have produced a value, then on
// every subsequent value on either input.
class SumNode : public Node {
public:
    SumNode(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Plugin.Sum"),
          a_("a"), b_("b"), out_("out") {
        register_input(&a_);
        register_input(&b_);
        register_output(&out_);
    }

    void on_start() override {
        a_.set_internal_sink([this](InputPort<double>::Value v) {
            enqueue([this, v] { last_a_ = *v; emit_if_ready(); });
        });
        b_.set_internal_sink([this](InputPort<double>::Value v) {
            enqueue([this, v] { last_b_ = *v; emit_if_ready(); });
        });
    }

private:
    // All sink closures run on the node's single worker thread, so these members
    // are touched serially and need no extra synchronisation.
    void emit_if_ready() {
        if (last_a_ && last_b_)
            out_.emit(std::make_shared<const double>(*last_a_ + *last_b_));
    }

    InputPort<double>     a_;
    InputPort<double>     b_;
    OutputPort<double>    out_;
    std::optional<double> last_a_;
    std::optional<double> last_b_;
};

}  // namespace

// --- Plugin entry points ---------------------------------------------------

FLOWBOARD_DECLARE_PLUGIN("Hello Example Plugin")

extern "C" FLOWBOARD_PLUGIN_EXPORT
void flowboard_plugin_register(flowboard::NodeRegistry& reg) {
    OP_REGISTER_NODE_INTO_WITH_SCHEMA(
        reg, "Plugin.Scale", ScaleNode,
        R"JSON({
          "$schema":"http://json-schema.org/draft-07/schema#",
          "type":"object",
          "properties":{
            "factor":{"type":"number","title":"Factor","default":2.0,
              "description":"Each input value is multiplied by this factor."}
          },
          "additionalProperties":false
        })JSON",
        R"JSON({"factor":2.0})JSON");

    OP_REGISTER_NODE_INTO(reg, "Plugin.Sum", SumNode);
}
