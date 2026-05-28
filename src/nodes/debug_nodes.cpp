// SPDX-License-Identifier: MIT
//
// Debug.* node group — canvas-side debugging aids.
//
//   Debug.TriggerButton : a Source emitting bool `out`; the value is pushed in
//                         from the web UI via POST /api/button (IButtonNode).
//   Debug.ValueDisplay  : a passive sink accepting any registered type; the web
//                         canvas renders the live value (with optional field
//                         selection). Reuses the Sinks.Log per-type factory
//                         registry in "display" mode (no logging).
//   Debug.GraphDisplay  : a passive sink accepting a numeric value; the web
//                         canvas plots it over time. Same display-mode reuse,
//                         restricted to Int64/Double.
#include <memory>
#include <stdexcept>
#include <string>

#include "builtin_types.hpp"
#include "flowboard/button_node.hpp"
#include "flowboard/log_registry.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// ---------------------------------------------------------------------------
// Debug.TriggerButton
// ---------------------------------------------------------------------------
class TriggerButton final : public Node, public IButtonNode {
public:
    TriggerButton(std::string id, ::nlohmann::json const&)
        : Node(std::move(id), "Debug.TriggerButton"), out_("out") {
        register_output(&out_);
    }

    // Called from the control-plane HTTP thread; hop onto the node worker so
    // emission is serialized with the rest of the engine.
    void button_press(bool value) override {
        enqueue([this, value] { out_.emit(std::make_shared<const bool>(value)); });
    }

private:
    OutputPort<bool> out_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Debug.TriggerButton", TriggerButton,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "properties":{
        "label":{"type":"string","title":"Label","default":"Trigger",
                 "description":"Caption shown on the canvas button."},
        "mode":{"type":"string","title":"Mode","enum":["pushbutton","switch"],"default":"pushbutton",
                "description":"pushbutton: emits true while held, false on release; switch: click toggles true/false."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"label":"Trigger","mode":"pushbutton"})JSON")

// ---------------------------------------------------------------------------
// Debug.ValueDisplay / Debug.GraphDisplay
//
// Both are passive sinks: they reuse the registered Sinks.Log per-type factory
// (which knows how to build a typed `in` port for every primitive + onboardapi
// struct) but flip it into display mode so it consumes without logging.
// ---------------------------------------------------------------------------
namespace {
std::unique_ptr<Node> make_display_sink(std::string id, ::nlohmann::json const& cfg,
                                        std::string const& default_type) {
    auto type = cfg.value("inputType", default_type);
    auto fn = ::flowboard::lookup_log_factory(type);
    if (!fn)
        throw std::runtime_error("Debug display: unsupported inputType '" + type + "'");
    ::nlohmann::json sink_cfg = cfg;
    sink_cfg["display"] = true;  // suppress logging; the web canvas renders it
    return fn(std::move(id), sink_cfg);
}
}  // namespace

static auto _value_display_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    return make_display_sink(std::move(id), cfg, "flowboard::Double");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Debug.ValueDisplay",
    _value_display_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType"],
      "properties":{
        "inputType":{"type":"string","title":"Input type","default":"flowboard::Double",
                     "description":"Type tag accepted on the in port (any registered primitive or struct)."},
        "fields":{"type":"array","title":"Fields","items":{"type":"string"},"default":[],
                  "description":"Optional dot-paths to show from a struct value (e.g. Azimuth, Position.X). Empty = whole value."}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","fields":[]})JSON",
    valuedisplay)

static auto _graph_display_factory = [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
    return make_display_sink(std::move(id), cfg, "flowboard::Double");
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Debug.GraphDisplay",
    _graph_display_factory,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["inputType"],
      "properties":{
        "inputType":{"type":"string","title":"Input type","enum":["flowboard::Double","flowboard::Float","flowboard::Int64","flowboard::UInt64","flowboard::Int32","flowboard::UInt32","flowboard::Int16","flowboard::UInt16","flowboard::UInt8","flowboard::Bool"],
                     "default":"flowboard::Double","description":"Numeric value plotted over time (bool plots as 0/1)."},
        "timeWindowSec":{"type":"number","title":"Time window (s)","minimum":1,"default":30,
                         "description":"How many seconds of history to show."},
        "autoScale":{"type":"boolean","title":"Auto-scale Y","default":true},
        "yMin":{"type":"number","title":"Y min","default":0},
        "yMax":{"type":"number","title":"Y max","default":1}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"inputType":"flowboard::Double","timeWindowSec":30,"autoScale":true,"yMin":0,"yMax":1})JSON",
    graphdisplay)

}  // namespace flowboard::nodes
