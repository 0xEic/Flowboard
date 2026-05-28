# Adding a Node {#adding_a_node}

A walkthrough using a hypothetical `Transform.Square` node (input: double, output: double, computes x²) as the model.

## 1. Scaffold the source file

Create `src/nodes/square.cpp`:

```cpp
// SPDX-License-Identifier: MIT
#include "flowboard/node.hpp"
#include "flowboard/port.hpp"
#include "flowboard/registry.hpp"
#include "builtin_types.hpp"

namespace flowboard::nodes {

class Square : public Node {
public:
    Square(std::string id, nlohmann::json const&)
        : Node(std::move(id), "Transform.Square"),
          in_("in"), out_("out") {
        register_input(&in_);
        register_output(&out_);
    }

    void on_start() override {
        in_.set_internal_sink([this](auto v) {
            enqueue([this, v] {
                out_.emit(std::make_shared<const double>((*v) * (*v)));
            });
        });
    }

private:
    InputPort<double>  in_;
    OutputPort<double> out_;
};

OP_REGISTER_NODE("Transform.Square", Square)

}  // namespace flowboard::nodes
```

Key points:
- `register_input(&in_)` and `register_output(&out_)` tell the base class about your ports.
- `on_start()` is where you wire input sinks. The base class's worker thread calls `enqueue`-d closures one at a time.
- `OP_REGISTER_NODE` makes your node discoverable by name in the JSON config.

## 2. Register in CMake

Open `src/CMakeLists.txt` and add `nodes/square.cpp` to the `flowboard_engine` source list.

## 3. Write a unit test

Create `tests/unit/test_square.cpp`:

```cpp
// SPDX-License-Identifier: MIT
#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "flowboard/registry.hpp"
#include "flowboard/port.hpp"
#include "builtin_types.hpp"

using namespace flowboard;

TEST_CASE("Square node squares its input") {
    auto& reg = NodeRegistry::instance();
    auto node = reg.create("Transform.Square", "sq1", nlohmann::json::object());
    REQUIRE(node);

    std::atomic<double> last{0.0};
    auto* out = dynamic_cast<OutputPort<double>*>(node->output("out"));
    REQUIRE(out);
    out->attach_sink([&](auto v) { last.store(*v); });

    node->start();
    auto* in = dynamic_cast<InputPort<double>*>(node->input("in"));
    REQUIRE(in);
    in->deliver(std::make_shared<const double>(5.0));

    for (int i = 0; i < 100 && last.load() == 0.0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(last.load() == 25.0);
    node->stop();
}
```

Add it to `tests/CMakeLists.txt`'s test source list.

## 4. Build + test

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 5. Use it in a graph

In your JSON config:

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "source",  "type": "Transform.ConstantSource",
      "config": {"outputType": "flowboard::Double", "value": 3.14} },
    { "id": "squarer", "type": "Transform.Square" },
    { "id": "logger",  "type": "Sinks.Log",
      "config": {"inputType": "flowboard::Double", "prefix": "x²"} }
  ],
  "edges": [
    { "from": "source.out",  "to": "squarer.in",  "policy": "block", "capacity": 16 },
    { "from": "squarer.out", "to": "logger.in",   "policy": "block", "capacity": 16 }
  ]
}
```

## When a new value type is introduced

If your node introduces a value type that the engine hasn't seen, you also need:

1. **Declare a type tag** in a header you include from the node:
   ```cpp
   OP_DECLARE_TYPE(MyType, "MyType")
   ```
2. **Add a dispatch arm** in `src/engine/graph.cpp`'s `Graph::connect()` switch chain:
   ```cpp
   else if (tag == type_tag_v<MyType>) wire_with(MyType{});
   ```

For OnboardAPI struct types, `pipegen` does both of the above automatically.

## Tips

- The `Node` base class catches exceptions thrown from your `process()` closures and logs them; the worker thread survives. Repeated failures (default: 10 in 5 s) trip the per-node error budget and the engine marks the node `failed`. Configure via `config.errorBudget`.
- Use `spdlog::info(...)`-style logging from inside your node — it goes through the same sink the framework uses.
- For multi-input nodes, register each port separately and set per-port sinks. The base class serializes their closures on the single worker thread.

## See also

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — engine primitives at a glance.
- Existing transform nodes follow this same pattern; read any of `src/nodes/threshold.cpp`, `compare.cpp`, `filter.cpp` for variations.

## Codegen-driven node families

Some nodes ship as one type name (`Transform.Extract`) whose behavior is
backed by codegen-emitted registries. For `Transform.Extract`, every
pipegen-emitted struct contributes a per-struct factory + a field-descriptor
table at build time. The supported input types come from the OnboardAPI data
model in the `external/onboardapi` submodule; updating that submodule (or
pointing `-DONBOARDAPI_DATAMODEL_DIR=<dir>` at another set of `.rmodel` files)
and rebuilding registers the new structs' factories at static-init time.

To extend the Extract node itself (e.g. add a new output port kind or
support nested-path fields), edit:
- `include/flowboard/extract_nodes.hpp` (the three Extract* templates)
- `pipegen/emit_cpp.cpp` (per-struct factory emission — search for
  `make_extract_`)
- `pipegen/emit_ts.cpp` (TS field-descriptor metadata for the web UI)
- `web/src/lib/extract_ports.ts` (UI-side port-shape mapping)

## Nodes with config-derived ports (`Flow.Machine`)

`Flow.Machine` (`src/nodes/flow_machine.cpp`) is a single engine node whose
ports are **derived from its config** rather than fixed: one bool input per
distinct transition `trigger`, plus `state` (string), `active.<State>` (bool
per state), `transition` (string), and `changed` (bool) outputs. It owns the
current state and enforces single-active by serialising every event on its
worker queue.

In the web UI it is a container: double-click / **"⤢ Open"** drills into an
inner canvas (`web/src/components/MachineCanvas.tsx`) where `Flow.State` and
`Flow.Transition` authoring nodes are placed. These authoring nodes have **no
engine classes** — on "Done" the inner canvas is *compiled* into the Machine's
config by the pure module `web/src/lib/machine_compile.ts` (and decompiled back
on open). The same module's `machineOuterPorts` keeps `web/src/lib/node_ports.ts`
in sync with the engine's port derivation so the main-canvas node renders the
right pins. To add a port kind or FSM feature (timeouts, entry actions), edit
`flow_machine.cpp` (runtime), `machine_compile.ts` (compile + outer ports), and
the inner node views.
