// SPDX-License-Identifier: MIT
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "builtin_types.hpp"
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"

namespace flowboard::nodes {

// Flow.StateMachine — a HIERARCHICAL finite state machine compiled from the web
// UI's inner canvas. States/transitions flatten into this node's config. A state
// may itself carry a nested `machine` (a composite state): while that state is
// active its submachine runs, starting from the submachine's own initial(s) and
// resetting to them on every (re-)entry. Nesting is recursive (arbitrary depth).
// A level may also hold several independent chains, so MANY states can be active
// at once -- across both sibling chains and nested levels. Every event is
// serialised on the worker queue, keeping the active set consistent.
//
// Config (recursive):
//   initial:     legacy single entry state (fallback when no `initial` flag).
//   states:      array of {name, initial?, machine?, ...}. initial:true marks an
//                entry point; `machine` is a nested config (composite state).
//   transitions: array of {from, to, trigger, kind?, delayMs?}. kind is
//                "trigger" (default), "null" (fires immediately on entry), or
//                "timed" (fires after delayMs while the source stays active).
//
// Derived ports (dotted by the path of owning composite states):
//   inputs:  one bool port per distinct TRIGGER-kind trigger ("warn",
//            "Heating.warn"); null/timed transitions are autonomous (no port).
//   outputs: state (string, comma-joined active set across all levels, dotted),
//            active.<path> (bool, one per state at every level), transition
//            (string, dotted from->to), changed (bool).
class FlowMachine final : public Node {
public:
    FlowMachine(std::string id, ::nlohmann::json const& cfg)
        : Node(std::move(id), "Flow.StateMachine"),
          state_out_("state"),
          transition_out_("transition"),
          changed_out_("changed") {
        root_ = build(cfg, "", "");
        register_output(&state_out_);
        register_output(&transition_out_);
        register_output(&changed_out_);
    }

    // Stop the worker and join all timer threads before our members (the machine
    // tree the timers reference) are destroyed.
    ~FlowMachine() override { stop(); timers_.clear(); }

    void on_start() override {
        for (auto& [name, port] : trigger_ins_) {
            std::string full = name;
            port->set_internal_sink([this, full](InputPort<bool>::Value v) {
                bool fired = v && *v;
                if (!fired) return;
                enqueue([this, full] { handle_trigger(full); });
            });
        }
        // Activate the whole tree from its initials and emit once edges attach.
        // start_pump runs after on_start, so these pushes buffer and drain on start.
        enqueue([this] {
            activate(root_, "");
            emit_state();
        });
    }

    void on_stop() override { timers_.clear(); }  // cancel + join all timers

private:
    enum class Kind { Trigger, Null, Timed };
    struct Transition {
        std::string from, to, trigger;
        Kind kind = Kind::Trigger;
        int  delay_ms = 0;            // for Kind::Timed
    };
    struct Machine {
        std::vector<std::string>                       states;       // local names, in order
        std::set<std::string>                          state_set;
        std::set<std::string>                          initials;     // local initial names
        std::vector<Transition>                        transitions;  // local transitions
        std::map<std::string, std::unique_ptr<Machine>> children;    // state name -> submachine
        std::set<std::string>                          active;       // active local states
    };

    // Recursively build a machine (registering its ports) from config JSON.
    // `prefix` is the dotted path of owning composite states ("" at the root,
    // "Heating." one level down); `where` is a human path for error messages.
    Machine build(::nlohmann::json const& cfg, std::string const& prefix, std::string const& where) {
        Machine m;
        if (!cfg.contains("states") || !cfg["states"].is_array() || cfg["states"].empty())
            throw std::runtime_error("Flow.StateMachine: " + where + "'states' must be a non-empty array");

        for (auto const& s : cfg["states"]) {
            std::string name = s.is_string() ? s.get<std::string>()
                                             : s.value("name", std::string{});
            if (name.empty())
                throw std::runtime_error("Flow.StateMachine: " + where + "a state has an empty name");
            if (name.find('.') != std::string::npos)
                throw std::runtime_error("Flow.StateMachine: " + where + "state name '" + name + "' must not contain '.'");
            if (m.state_set.count(name))
                throw std::runtime_error("Flow.StateMachine: " + where + "duplicate state '" + name + "'");
            m.states.push_back(name);
            m.state_set.insert(name);
            if (s.is_object() && s.value("initial", false))
                m.initials.insert(name);

            std::string full = prefix + name;
            auto port = std::make_unique<OutputPort<bool>>("active." + full);
            register_output(port.get());
            active_outs_.emplace(full, std::move(port));

            if (s.is_object() && s.contains("machine") && s["machine"].is_object())
                m.children.emplace(name, std::make_unique<Machine>(
                    build(s["machine"], full + ".", where + name + " > ")));
        }

        // Legacy fallback: no per-state `initial` flag -> single top-level initial.
        if (m.initials.empty()) {
            std::string init = cfg.value("initial", std::string{});
            if (init.empty()) init = m.states.front();
            if (!m.state_set.count(init))
                throw std::runtime_error("Flow.StateMachine: " + where + "initial state '" + init + "' is not a declared state");
            m.initials.insert(init);
        }

        if (cfg.contains("transitions")) {
            if (!cfg["transitions"].is_array())
                throw std::runtime_error("Flow.StateMachine: " + where + "'transitions' must be an array");
            for (auto const& t : cfg["transitions"]) {
                Transition tr;
                tr.from    = t.value("from",    std::string{});
                tr.to      = t.value("to",      std::string{});
                tr.trigger = t.value("trigger", std::string{});
                std::string kind = t.value("kind", std::string{"trigger"});
                tr.kind = kind == "null"  ? Kind::Null
                        : kind == "timed" ? Kind::Timed
                                          : Kind::Trigger;
                tr.delay_ms = t.value("delayMs", 0);
                if (!m.state_set.count(tr.from))
                    throw std::runtime_error("Flow.StateMachine: " + where + "transition from unknown state '" + tr.from + "'");
                if (!m.state_set.count(tr.to))
                    throw std::runtime_error("Flow.StateMachine: " + where + "transition to unknown state '" + tr.to + "'");
                if (tr.kind == Kind::Trigger && tr.trigger.empty())
                    throw std::runtime_error("Flow.StateMachine: " + where + "transition " + tr.from + "->" + tr.to + " has an empty trigger");
                if (tr.kind == Kind::Timed && tr.delay_ms <= 0)
                    throw std::runtime_error("Flow.StateMachine: " + where + "timed transition " + tr.from + "->" + tr.to + " needs a positive delayMs");
                m.transitions.push_back(tr);

                // Only trigger-kind transitions derive a boolean input port.
                if (tr.kind == Kind::Trigger) {
                    std::string full = prefix + tr.trigger;
                    if (!trigger_ins_.count(full)) {
                        auto port = std::make_unique<InputPort<bool>>(full);
                        register_input(port.get());
                        trigger_ins_.emplace(full, std::move(port));
                    }
                }
                // Timed transitions expose an Int64 "remaining ms" output so the
                // web canvas can animate a countdown / progress bar on the link.
                if (tr.kind == Kind::Timed) {
                    std::string tname = timer_port_name(prefix, tr.from, tr.to);
                    auto port = std::make_unique<OutputPort<std::int64_t>>(tname);
                    register_output(port.get());
                    timer_outs_.emplace(tname, std::move(port));
                }
            }
        }
        return m;
    }

    void emit_active(std::string const& full, bool v) {
        auto it = active_outs_.find(full);
        if (it != active_outs_.end())
            it->second->emit(std::make_shared<const bool>(v));
    }

    // (Re)enter a machine: active := initials; emit every local state's bool,
    // recurse into composite states, and arm autos (timers / null transitions)
    // for each newly-active state.
    void activate(Machine& m, std::string const& prefix) {
        m.active = m.initials;
        for (auto const& name : m.states) {
            bool on = m.active.count(name) > 0;
            emit_active(prefix + name, on);
            auto it = m.children.find(name);
            if (it != m.children.end()) {
                if (on) activate(*it->second, prefix + name + ".");
                else    deactivate(*it->second, prefix + name + ".");
            }
            if (on) arm(m, prefix, name);
        }
    }

    // Leave a machine entirely: cancel its timers, clear active, emit false for
    // every descendant.
    void deactivate(Machine& m, std::string const& prefix) {
        for (auto const& t : m.transitions)
            if (t.kind == Kind::Timed) cancel_timer(&t);
        m.active.clear();
        for (auto const& name : m.states) {
            emit_active(prefix + name, false);
            auto it = m.children.find(name);
            if (it != m.children.end()) deactivate(*it->second, prefix + name + ".");
        }
    }

    // Arm autos for a state that has just become active: start timers for its
    // timed transitions and (immediately, via the queue) fire a null transition.
    void arm(Machine& m, std::string const& prefix, std::string const& name) {
        bool has_null = false;
        for (auto const& t : m.transitions) {
            if (t.from != name) continue;
            if (t.kind == Kind::Timed) start_timer(m, prefix, &t);
            else if (t.kind == Kind::Null) has_null = true;
        }
        if (has_null) {
            Machine* mp = &m;
            std::string p = prefix, nm = name;
            enqueue([this, mp, p, nm] { fire_null(*mp, p, nm); });
        }
    }

    // Cancel any timers on transitions leaving `name` (the state is exiting).
    void disarm(Machine& m, std::string const& name) {
        for (auto const& t : m.transitions)
            if (t.from == name && t.kind == Kind::Timed) cancel_timer(&t);
    }

    void handle_trigger(std::string const& full) {
        // Split "Heating.Fan.on" -> path ["Heating","Fan"] + local trigger "on".
        std::vector<std::string> segs;
        std::string cur;
        for (char c : full) { if (c == '.') { segs.push_back(cur); cur.clear(); } else cur += c; }
        segs.push_back(cur);
        std::string trig = segs.back();
        segs.pop_back();

        // Descend to the owning machine, but only through currently-active states.
        Machine* m = &root_;
        std::string prefix;
        for (auto const& seg : segs) {
            if (!m->active.count(seg)) return;          // path not active -> no-op
            auto it = m->children.find(seg);
            if (it == m->children.end()) return;        // not composite (shouldn't happen)
            prefix += seg + ".";
            m = it->second.get();
        }
        apply(*m, prefix, trig);
    }

    // A boolean trigger fired: every active state with a matching trigger
    // transition advances (first declared wins per state).
    void apply(Machine& m, std::string const& prefix, std::string const& trig) {
        std::vector<Transition const*> to_fire;
        for (auto const& from : m.active)
            for (auto const& t : m.transitions)
                if (t.kind == Kind::Trigger && t.from == from && t.trigger == trig) { to_fire.push_back(&t); break; }
        for (auto const* t : to_fire)
            if (m.active.count(t->from)) fire_one(m, prefix, t);
    }

    // A null transition's source is active: fire it immediately (the first null
    // declared from that state). Chains naturally via arm() -> queue.
    void fire_null(Machine& m, std::string const& prefix, std::string const& from) {
        if (!m.active.count(from)) return;  // exited before the queued fire ran
        for (auto const& t : m.transitions)
            if (t.kind == Kind::Null && t.from == from) { fire_one(m, prefix, &t); return; }
    }

    // Apply a single transition: leave `from` (tearing down its substate + timers),
    // enter `to` (spinning its substate up + arming its autos), and emit.
    void fire_one(Machine& m, std::string const& prefix, Transition const* t) {
        m.active.erase(t->from);
        emit_active(prefix + t->from, false);
        disarm(m, t->from);
        {
            auto it = m.children.find(t->from);
            if (it != m.children.end()) deactivate(*it->second, prefix + t->from + ".");
        }
        m.active.insert(t->to);
        emit_active(prefix + t->to, true);
        {
            auto it = m.children.find(t->to);
            if (it != m.children.end()) activate(*it->second, prefix + t->to + ".");
        }
        arm(m, prefix, t->to);
        transition_out_.emit(std::make_shared<const std::string>(prefix + t->from + "->" + prefix + t->to));
        emit_state();
        changed_out_.emit(std::make_shared<const bool>(true));
    }

    // ---- timers ----------------------------------------------------------
    // Each timed transition gets a cancellable jthread keyed by its (stable)
    // address. A generation counter invalidates a fire that was queued just
    // before the timer was cancelled or restarted (re-entry).
    static std::string timer_key(Transition const* t) {
        return std::to_string(reinterpret_cast<std::uintptr_t>(t));
    }

    // Output-port name for a timed transition's remaining-ms stream. Dotted by the
    // owning composite path (matching active.<path>) so the web can reconstruct it.
    static std::string timer_port_name(std::string const& prefix,
                                       std::string const& from, std::string const& to) {
        return "timer." + prefix + from + "->" + to;
    }

    void start_timer(Machine& m, std::string const& prefix, Transition const* t) {
        std::string key = timer_key(t);
        std::uint64_t g = ++timer_gen_[key];
        timers_.erase(key);  // cancel + join any stale timer for this transition
        int delay = t->delay_ms;
        Machine* mp = &m;
        std::string p = prefix;
        OutputPort<std::int64_t>* port = nullptr;
        if (auto it = timer_outs_.find(timer_port_name(prefix, t->from, t->to)); it != timer_outs_.end())
            port = it->second.get();
        timers_.emplace(key, std::jthread([this, key, g, delay, mp, p, t, port](std::stop_token st) {
            using clock = std::chrono::steady_clock;
            auto const deadline = clock::now() + std::chrono::milliseconds(delay);
            auto emit_remaining = [port](std::int64_t v) {
                if (port) port->emit(std::make_shared<const std::int64_t>(v));
            };
            emit_remaining(delay);  // start full so the bar resets on (re-)entry
            std::mutex mm;
            std::condition_variable_any cv;
            std::unique_lock<std::mutex> lk(mm);
            for (;;) {
                auto left = deadline - clock::now();
                if (left <= std::chrono::milliseconds(0)) break;
                emit_remaining(std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
                // ~60 ms ticks for a smooth countdown, never overshooting the deadline.
                auto step = left < std::chrono::milliseconds(60)
                    ? std::chrono::duration_cast<std::chrono::milliseconds>(left)
                    : std::chrono::milliseconds(60);
                cv.wait_for(lk, st, step, [] { return false; });
                if (st.stop_requested()) return;  // cancelled while waiting
            }
            if (st.stop_requested()) return;
            emit_remaining(0);
            enqueue([this, key, g, mp, p, t] {
                if (timer_gen_[key] != g) return;        // superseded (cancel / re-entry)
                if (!mp->active.count(t->from)) return;  // source no longer active
                fire_one(*mp, p, t);
            });
        }));
    }

    void cancel_timer(Transition const* t) {
        std::string key = timer_key(t);
        ++timer_gen_[key];   // invalidate any already-queued fire
        timers_.erase(key);  // stop + join the sleeping thread
    }

    // The `state` output: comma-joined active set across the whole tree (dotted,
    // sorted via std::set so the string is deterministic).
    void collect_active(Machine const& m, std::string const& prefix, std::set<std::string>& out) const {
        for (auto const& name : m.active) {
            out.insert(prefix + name);
            auto it = m.children.find(name);
            if (it != m.children.end()) collect_active(*it->second, prefix + name + ".", out);
        }
    }
    void emit_state() {
        std::set<std::string> act;
        collect_active(root_, "", act);
        std::string joined;
        for (auto const& n : act) { if (!joined.empty()) joined += ','; joined += n; }
        state_out_.emit(std::make_shared<const std::string>(joined));
    }

    Machine                                                 root_;
    OutputPort<std::string>                                 state_out_;
    OutputPort<std::string>                                 transition_out_;
    OutputPort<bool>                                        changed_out_;
    std::map<std::string, std::unique_ptr<OutputPort<bool>>> active_outs_;
    std::map<std::string, std::unique_ptr<InputPort<bool>>>  trigger_ins_;
    // One Int64 output per timed transition, streaming the remaining ms while the
    // source state is active (delayMs down to 0). Declared before timers_ so the
    // timer threads (which emit on these) are torn down first. Keyed by port name.
    std::map<std::string, std::unique_ptr<OutputPort<std::int64_t>>> timer_outs_;
    std::map<std::string, std::jthread>                      timers_;     // key -> sleeping timer
    std::map<std::string, std::uint64_t>                     timer_gen_;  // key -> live generation
};

OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(
    "Flow.StateMachine",
    [](std::string id, ::nlohmann::json const& cfg) -> std::unique_ptr<Node> {
        return std::make_unique<FlowMachine>(std::move(id), cfg);
    },
    R"JSON({
      "$schema":"http://json-schema.org/draft-07/schema#",
      "type":"object",
      "required":["states"],
      "properties":{
        "initial":{"type":"string","title":"Initial state",
                   "description":"Legacy single entry state. Used only as a fallback when no state carries an `initial` flag. Must be one of states[]."},
        "states":{"type":"array","title":"States","minItems":1,
                  "items":{"type":"object","required":["name"],
                           "properties":{
                             "name":{"type":"string","title":"Name"},
                             "initial":{"type":"boolean","title":"Initial",
                                        "description":"Entry point: active when the machine starts. Multiple states may be initial — one per chain (connected component) — so several states can be active at once."},
                             "machine":{"$ref":"#","title":"Substate machine",
                                        "description":"Nested state machine for a composite state (recursive). Runs while this state is active; resets to its initial(s) on entry."},
                             "position":{"type":"object","description":"Editor-only layout; ignored by the engine."},
                             "width":{"type":"number","description":"Editor-only canvas size; ignored by the engine."},
                             "height":{"type":"number","description":"Editor-only canvas size; ignored by the engine."}
                           }}},
        "transitions":{"type":"array","title":"Transitions",
                       "items":{"type":"object","required":["from","to","trigger"],
                                "properties":{
                                  "from":{"type":"string","title":"From state"},
                                  "to":{"type":"string","title":"To state"},
                                  "kind":{"type":"string","title":"Kind","enum":["trigger","null","timed"],
                                          "description":"trigger (default): fires on the boolean trigger port. null: fires immediately on entry. timed: fires after delayMs while the source stays active."},
                                  "trigger":{"type":"string","title":"Trigger",
                                             "description":"Boolean input port that fires a trigger-kind transition. When two transitions share from+trigger, the first declared wins. Ignored for null/timed."},
                                  "delayMs":{"type":"number","title":"Delay (ms)",
                                             "description":"Timer for a timed-kind transition, in milliseconds. Starts on entry; restarts on re-entry."},
                                  "position":{"type":"object","description":"Editor-only layout; ignored by the engine (legacy)."},
                                  "labelOffset":{"type":"object","description":"Editor-only layout; ignored by the engine."},
                                  "controlPoint":{"type":"object","description":"Editor-only layout; ignored by the engine."}
                                }}},
        "notes":{"type":"array","title":"Notes",
                 "description":"Editor-only canvas annotations on this machine's inner canvas; ignored by the engine.",
                 "items":{"type":"object",
                          "properties":{
                            "text":{"type":"string"},
                            "position":{"type":"object"},
                            "width":{"type":"number"},
                            "height":{"type":"number"}
                          }}}
      },
      "additionalProperties":false
    })JSON",
    R"JSON({"initial":"","states":[],"transitions":[]})JSON",
    flow_machine
)

}  // namespace flowboard::nodes
