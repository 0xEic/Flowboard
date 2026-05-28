# Flowboard {#mainpage}

**Flowboard** is a configurable pipe-and-filter workflow engine for the
Rheinmetall OnboardAPI data ecosystem. A workflow is described as a JSON graph
of **nodes** wired together by typed **ports**; the engine instantiates the
nodes, type-checks every connection, and runs each node on its own worker
thread, streaming `std::shared_ptr<const T>` messages along lock-free edges.

This site is the generated API reference. It complements the prose guides in the
repository (`README.md`, `docs/`).

## Where to start

- @subpage architecture — the system design: primitives, threading model,
  data flow, control plane, codegen, and lifecycle. **Read this first.**
- The [Classes](annotated.html) list — every public type, grouped by namespace.
- The [Files](files.html) list — per-header summaries.

## The four engine primitives

| Primitive | Type | Role |
|-----------|------|------|
| Port | flowboard::InputPort / flowboard::OutputPort | Strongly-typed message endpoints; mismatched connections are rejected at load time via flowboard::type_tag_v. |
| Edge | flowboard::Edge | Bounded lock-free SPSC ring with a flowboard::BackpressurePolicy (`Block` or `DropOldest`). |
| Node | flowboard::Node | Base class owning a `std::jthread` worker fed by a counting semaphore. |
| Graph | flowboard::Graph | Owns nodes and edges; performs type-checked `connect()` and manages the run lifecycle. |

Nodes are created by name from the flowboard::NodeRegistry, populated at
static-init time through the `OP_REGISTER_NODE` family of macros. The live graph
is owned by flowboard::GraphHolder, which the control plane uses to hot-swap
configurations atomically.

## Building this documentation

```bash
cmake -S . -B build -DFLOWBOARD_BUILD_DOCS=ON
cmake --build build --target docs
# open build/docs/html/index.html
```

Graphviz (`dot`) is used for class and collaboration diagrams when present;
Doxygen falls back to its built-in diagrams otherwise.

## License

MIT. The OnboardAPI data model is pulled from the `Rheinmetall/onboardapi`
submodule (`external/onboardapi`) and is licensed EPL-2.0 by Rheinmetall.
