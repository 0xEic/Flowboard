// SPDX-License-Identifier: MIT
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Input.ButtonHandler — fans a HidJoystick EventButton stream out to one bool
// port per button of interest. EventButton decomposes (on the M_HidJoystick
// client node) into three values per press/release: ButtonIndex (UInt32),
// ButtonName (String) and IsSelected (Bool). Wire those three to this node's
// like-named inputs; each configured output then emits IsSelected ("isPressed")
// whenever an event for its button arrives.
//
// Config:
//   outputs: array of { output?, match, index?, buttonName? }
//     match:      "byIndex" (default) or "byName" — how to identify the button.
//     index:      button index to match when match=="byIndex".
//     buttonName: button name to match when match=="byName".
//     output:     output port name; defaults to "button<index>" (byIndex) or the
//                 button name (byName).
//
// Derived ports:
//   inputs:  ButtonIndex (UInt32), ButtonName (String), IsSelected (Bool).
//   outputs: one Bool port per configured output.
//
// IsSelected is the trigger: a press/release arrives as ButtonIndex then
// ButtonName then IsSelected, so the index/name latched when IsSelected lands
// belong to that event. byIndex outputs need only ButtonIndex wired; byName
// outputs need only ButtonName.
class ButtonHandler final : public Node {
public:
    ButtonHandler(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Input.ButtonHandler"),
          button_index_in_("ButtonIndex"),
          button_name_in_("ButtonName"),
          is_selected_in_("IsSelected") {
        register_input(&button_index_in_);
        register_input(&button_name_in_);
        register_input(&is_selected_in_);

        if (cfg.contains("outputs")) {
            if (!cfg["outputs"].is_array())
                throw std::runtime_error("Input.ButtonHandler: 'outputs' must be an array");
            for (auto const& o : cfg["outputs"])
                add_output(o);
        }
    }

    void on_start() override {
        button_index_in_.set_internal_sink([this](InputPort<std::uint32_t>::Value v) {
            if (!v) return;
            auto idx = *v;
            enqueue([this, idx] { last_index_ = idx; });
        });
        button_name_in_.set_internal_sink([this](InputPort<std::string>::Value v) {
            if (!v) return;
            auto name = *v;
            enqueue([this, name = std::move(name)] { last_name_ = name; });
        });
        is_selected_in_.set_internal_sink([this](InputPort<bool>::Value v) {
            if (!v) return;
            bool pressed = *v;
            enqueue([this, pressed] { dispatch(pressed); });
        });
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

    void dispatch(bool pressed) {
        for (auto const& s : specs_) {
            bool match = s.by_index ? (last_index_ && *last_index_ == s.index)
                                    : (last_name_  && *last_name_  == s.name);
            if (match)
                s.port->emit(std::make_shared<const bool>(pressed));
        }
    }

    InputPort<std::uint32_t> button_index_in_;
    InputPort<std::string>   button_name_in_;
    InputPort<bool>          is_selected_in_;

    std::vector<std::unique_ptr<OutputPort<bool>>> ports_;
    std::vector<OutputSpec>                        specs_;

    std::optional<std::uint32_t> last_index_;
    std::optional<std::string>   last_name_;
};

OP_REGISTER_NODE_WITH_SCHEMA(
    "Input.ButtonHandler",
    ButtonHandler,
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "title":"Button Handler",
      "description":"Routes HidJoystick EventButton presses to one bool output per button. Wire EventButton.ButtonIndex/ButtonName/IsSelected to the inputs of the same name.",
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
        }
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"outputs":[]})JSON")

}  // namespace flowboard::nodes
