// SPDX-License-Identifier: MIT
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Flow.StateMachine — a finite state machine compiled from the web UI's inner
// canvas. States and transitions are authoring-only nodes there; they flatten
// into this node's config. The single instance owns the current state and
// enforces single-active by serialising every event on its worker queue.
//
// Config:
//   initial:     state name to start in (must be one of states).
//   states:      array of {name, ...} (extra keys like `position` ignored) or
//                plain name strings.
//   transitions: array of {from, to, trigger} (extra keys ignored).
//
// Derived ports:
//   inputs:  one bool port per distinct trigger name.
//   outputs: state (string), active.<State> (bool, one per state),
//            transition (string), changed (bool).
class FlowMachine final : public Node {
public:
    FlowMachine(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Flow.StateMachine"),
          state_out_("state"),
          transition_out_("transition"),
          changed_out_("changed") {
        initial_ = cfg.value("initial", std::string{});

        if (!cfg.contains("states") || !cfg["states"].is_array() ||
            cfg["states"].empty()) {
            throw std::runtime_error("Flow.StateMachine: 'states' must be a non-empty array");
        }
        for (auto const& s : cfg["states"]) {
            std::string name = s.is_string() ? s.get<std::string>()
                                             : s.value("name", std::string{});
            if (name.empty())
                throw std::runtime_error("Flow.StateMachine: a state has an empty name");
            if (state_set_.count(name))
                throw std::runtime_error("Flow.StateMachine: duplicate state '" + name + "'");
            states_.push_back(name);
            state_set_.insert(name);
        }

        if (initial_.empty()) initial_ = states_.front();
        if (!state_set_.count(initial_))
            throw std::runtime_error("Flow.StateMachine: initial state '" + initial_ +
                                     "' is not a declared state");

        if (cfg.contains("transitions")) {
            if (!cfg["transitions"].is_array())
                throw std::runtime_error("Flow.StateMachine: 'transitions' must be an array");
            for (auto const& t : cfg["transitions"]) {
                Transition tr;
                tr.from    = t.value("from",    std::string{});
                tr.to      = t.value("to",      std::string{});
                tr.trigger = t.value("trigger", std::string{});
                if (!state_set_.count(tr.from))
                    throw std::runtime_error("Flow.StateMachine: transition from unknown state '" + tr.from + "'");
                if (!state_set_.count(tr.to))
                    throw std::runtime_error("Flow.StateMachine: transition to unknown state '" + tr.to + "'");
                if (tr.trigger.empty())
                    throw std::runtime_error("Flow.StateMachine: transition " + tr.from + "->" + tr.to +
                                             " has an empty trigger");
                transitions_.push_back(tr);
                trigger_names_.insert(tr.trigger);
            }
        }

        // Outputs: per-state active bools + the three fixed outputs.
        for (auto const& name : states_) {
            auto port = std::make_unique<OutputPort<bool>>("active." + name);
            register_output(port.get());
            active_outs_.emplace(name, std::move(port));
        }
        register_output(&state_out_);
        register_output(&transition_out_);
        register_output(&changed_out_);

        // Inputs: one bool trigger port per distinct trigger name.
        for (auto const& trig : trigger_names_) {
            auto port = std::make_unique<InputPort<bool>>(trig);
            register_input(port.get());
            trigger_ins_.emplace(trig, std::move(port));
        }
    }

    void on_start() override {
        current_ = initial_;
        for (auto& [trig, port] : trigger_ins_) {
            std::string name = trig;
            port->set_internal_sink([this, name](InputPort<bool>::Value v) {
                bool fired = v && *v;
                if (!fired) return;
                enqueue([this, name] { handle_trigger(name); });
            });
        }
        // Emit the initial state once edges are attached. start_pump runs after
        // on_start, so these pushes buffer in the SPSC ring and drain on start.
        enqueue([this] {
            for (auto const& name : states_)
                active_outs_[name]->emit(std::make_shared<const bool>(name == current_));
            state_out_.emit(std::make_shared<const std::string>(current_));
        });
    }

private:
    struct Transition { std::string from, to, trigger; };

    void handle_trigger(std::string const& trigger) {
        for (auto const& t : transitions_) {
            if (t.from == current_ && t.trigger == trigger) {
                switch_to(t);
                return;
            }
        }
        // No transition from the current state for this trigger: a no-op.
    }

    void switch_to(Transition const& t) {
        std::string prev = current_;
        current_ = t.to;
        active_outs_[prev]->emit(std::make_shared<const bool>(false));
        active_outs_[current_]->emit(std::make_shared<const bool>(true));
        state_out_.emit(std::make_shared<const std::string>(current_));
        transition_out_.emit(std::make_shared<const std::string>(prev + "->" + current_));
        changed_out_.emit(std::make_shared<const bool>(true));
    }

    std::string                                              initial_;
    std::string                                              current_;
    std::vector<std::string>                                 states_;
    std::set<std::string>                                    state_set_;
    std::vector<Transition>                                  transitions_;
    std::set<std::string>                                    trigger_names_;

    OutputPort<std::string>                                  state_out_;
    OutputPort<std::string>                                  transition_out_;
    OutputPort<bool>                                         changed_out_;
    std::map<std::string, std::unique_ptr<OutputPort<bool>>> active_outs_;
    std::map<std::string, std::unique_ptr<InputPort<bool>>>  trigger_ins_;
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Flow.StateMachine",
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<FlowMachine>(std::move(id), cfg);
    },
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["initial","states"],
      "properties":{
        "initial":{"type":"string","title":"Initial state",
                   "description":"Name of the state the machine starts in. Must be one of states[]."},
        "states":{"type":"array","title":"States","minItems":1,
                  "items":{"type":"object","required":["name"],
                           "properties":{
                             "name":{"type":"string","title":"Name"},
                             "position":{"type":"object","description":"Editor-only layout; ignored by the engine."}
                           }}},
        "transitions":{"type":"array","title":"Transitions",
                       "items":{"type":"object","required":["from","to","trigger"],
                                "properties":{
                                  "from":{"type":"string","title":"From state"},
                                  "to":{"type":"string","title":"To state"},
                                  "trigger":{"type":"string","title":"Trigger",
                                             "description":"Boolean input port that fires this transition. When two transitions share from+trigger, the first declared wins."},
                                  "position":{"type":"object","description":"Editor-only layout; ignored by the engine."}
                                }}}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"initial":"","states":[],"transitions":[]})JSON",
    flow_machine
)

}  // namespace flowboard::nodes
