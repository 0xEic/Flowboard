// SPDX-License-Identifier: MIT
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/port_factory_registry.hpp"
#include "flowboard/registry.hpp"
#include <nlohmann/json.hpp>

namespace flowboard::nodes {

// Input.ButtonHandler — fans a HidJoystick EventButton stream out to one bool
// port per button of interest. EventButton arrives atomically as a single
// M_HidJoystick::EventButton_Args struct on the "args" input (fields
// ButtonIndex, ButtonName, IsSelected). Wire the M_HidJoystick client's
// EventButton.args output to this node's "args" input; each configured output
// then emits IsSelected ("isPressed") whenever an event for its button arrives.
//
// Receiving the whole event in one message (rather than three separate scalar
// inputs) eliminates cross-edge ordering races: the index/name/pressed that
// belong to one press always travel together.
//
// Config:
//   outputs: array of { output?, match, index?, buttonName? }
//     match:      "byIndex" (default) or "byName" — how to identify the button.
//     index:      button index to match when match=="byIndex".
//     buttonName: button name to match when match=="byName".
//     output:     output port name; defaults to "button<index>" (byIndex) or the
//                 button name (byName).
//   argsType:   the struct type tag delivered on "args" (default
//               "M_HidJoystick::EventButton_Args"); overridable for compatible
//               EventButton-like structs.
//
// Derived ports:
//   inputs:  args (EventButton_Args struct).
//   outputs: one Bool port per configured output.
class ButtonHandler final : public Node {
public:
    ButtonHandler(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Input.ButtonHandler") {
        if (cfg.contains("outputs")) {
            if (!cfg["outputs"].is_array())
                throw std::runtime_error("Input.ButtonHandler: 'outputs' must be an array");
            for (auto const& o : cfg["outputs"])
                add_output(o);
        }

        // Single atomic struct input: the whole EventButton in one message.
        // Defaults to the HidJoystick EventButton_Args struct; overridable via
        // argsType for other EventButton-shaped structs.
        std::string argsType =
            cfg.value("argsType", std::string("M_HidJoystick::EventButton_Args"));
        auto const* entry = ::flowboard::lookup_port_factory(argsType);
        if (!entry)
            throw std::runtime_error(
                "Input.ButtonHandler: argsType '" + argsType +
                "' is not a registered port factory type");

        auto args_in = entry->make_input("args", [this](::nlohmann::json j) {
            // Parse the three fields from the struct JSON and dispatch atomically
            // on the node worker thread.
            try {
                auto idx     = j.at("ButtonIndex").get<std::uint32_t>();
                auto name    = j.at("ButtonName").get<std::string>();
                auto pressed = j.at("IsSelected").get<bool>();
                enqueue([this, idx, name = std::move(name), pressed] {
                    dispatch_atomic(idx, name, pressed);
                });
            } catch (...) {
                // Malformed message — silently drop.
            }
        });
        register_input(args_in.get());
        args_in_storage_ = std::move(args_in);
    }

private:
    struct OutputSpec {
        bool          by_index;
        std::uint32_t index{};
        std::string   name;          // button name to match (when !by_index)
        OutputPort<bool>* port;      // owned by ports_
    };

    void add_output(::nlohmann::json const& o) {
        std::string match = o.value("match", std::string("byIndex"));
        bool by_index;
        if (match == "byIndex")      by_index = true;
        else if (match == "byName")  by_index = false;
        else throw std::runtime_error(
            "Input.ButtonHandler: output 'match' must be \"byIndex\" or \"byName\", got \"" + match + "\"");

        OutputSpec spec;
        spec.by_index = by_index;
        std::string port_name = o.value("output", std::string{});

        if (by_index) {
            if (!o.contains("index") || !o["index"].is_number_integer())
                throw std::runtime_error("Input.ButtonHandler: byIndex output needs an integer 'index'");
            auto idx = o["index"].get<std::int64_t>();
            if (idx < 0)
                throw std::runtime_error("Input.ButtonHandler: 'index' must be >= 0");
            spec.index = static_cast<std::uint32_t>(idx);
            if (port_name.empty()) port_name = "button" + std::to_string(spec.index);
        } else {
            spec.name = o.value("buttonName", std::string{});
            if (spec.name.empty())
                throw std::runtime_error("Input.ButtonHandler: byName output needs a non-empty 'buttonName'");
            if (port_name.empty()) port_name = spec.name;
        }

        if (output(port_name) != nullptr)
            throw std::runtime_error("Input.ButtonHandler: duplicate output port '" + port_name + "'");

        auto port = std::make_unique<OutputPort<bool>>(port_name);
        spec.port = port.get();
        register_output(port.get());
        ports_.push_back(std::move(port));
        specs_.push_back(std::move(spec));
    }

    // All three fields arrive together — no cached state needed.
    void dispatch_atomic(std::uint32_t idx, std::string const& name, bool pressed) {
        for (auto const& s : specs_) {
            bool match = s.by_index ? (idx == s.index)
                                    : (name == s.name);
            if (match)
                s.port->emit(std::make_shared<const bool>(pressed));
        }
    }

    // The single atomic struct input (the whole EventButton in one message).
    std::unique_ptr<IInputPort> args_in_storage_;

    std::vector<std::unique_ptr<OutputPort<bool>>> ports_;
    std::vector<OutputSpec>                        specs_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Input.ButtonHandler",
    ButtonHandler,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Button Handler",
      "description":"Routes a HidJoystick EventButton (received atomically on the 'args' struct input) to one bool output per button. Wire the M_HidJoystick client's EventButton.args output to the 'args' input. Each output emits IsSelected when its button fires.",
      "properties":{
        "outputs":{
          "type":"array","title":"Outputs",
          "description":"One bool output per button of interest. Each emits IsSelected when its button fires.",
          "items":{
            "type":"object",
            "properties":{
              "match":{"type":"string","title":"Match by","enum":["byIndex","byName"],"default":"byIndex",
                       "description":"Identify the button by its index or its name."},
              "index":{"type":"integer","title":"Button index","minimum":0,
                       "description":"Matched against ButtonIndex when match=byIndex."},
              "buttonName":{"type":"string","title":"Button name",
                            "description":"Matched against ButtonName when match=byName."},
              "output":{"type":"string","title":"Output port",
                        "description":"Output port name. Defaults to button<index> or the button name."}
            }
          }
        },
        "argsType":{
          "type":"string",
          "title":"Args struct type",
          "default":"M_HidJoystick::EventButton_Args",
          "description":"The EventButton-like struct type tag delivered atomically on the 'args' input. Defaults to M_HidJoystick::EventButton_Args."
        }
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"outputs":[],"argsType":"M_HidJoystick::EventButton_Args"})JSON")

}  // namespace flowboard::nodes
