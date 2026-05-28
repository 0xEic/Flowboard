# Architecture {#architecture}

[TOC]

Flowboard is a **pipe-and-filter workflow engine** specialized for the
Rheinmetall OnboardAPI ecosystem. At runtime it loads a JSON graph definition,
instantiates nodes from a registry, wires them together with type-checked edges,
and runs each node on its own worker thread. Messages flow as immutable
`std::shared_ptr<const T>` values along lock-free single-producer/single-consumer
rings.

This page is the conceptual map of the system. The per-type details are in the
[Classes](annotated.html) reference; the links below jump straight to them.

## High-level shape

```
 +----------------------+     +-----------------------+     +----------------------+
 |  M_Mount.Client      | --> |  Transform.Threshold  | --> |  M_Alert.Client      |
 |  (OnboardAPI client) |     |  (business logic)     |     |  (OnboardAPI client) |
 +----------------------+     +-----------------------+     +----------------------+
          |                              |                             |
          |   shared_ptr<const T> messages, one SPSC ring per edge,    |
          |   each ring drained by its own pump thread                 |
          v                              v                             v
 ============================== flowboard::Graph =========================
```

A node reads from its input ports, does work on its worker thread, and emits on
its output ports. An edge connects exactly one output port to one input port,
buffers messages, and applies a backpressure policy. The graph owns every node
and every edge and drives the lifecycle.

## Engine primitives

The engine is built from four small, composable primitives.

### Ports — typed endpoints

flowboard::InputPort and flowboard::OutputPort are the typed message
endpoints, both templated on the message type `T`. Every port reports a
**type tag** (flowboard::type_tag_v) — a stable string identifying `T`, used
to validate connections at graph-load time so two incompatible ports can never
be wired together.

- An output port fans out to zero or more sinks (flowboard::OutputPort::emit).
- An input port forwards delivered values into the owning node's worker
  (flowboard::InputPort::set_internal_sink). Unconnected primitive inputs can
  be seeded with a JSON default (flowboard::IInputPort::deliver_json).

Messages are always `std::shared_ptr<const T>`: immutable and cheap to fan out,
so a single emitted value can be shared by many consumers without copying.

### Edges — lock-free buffered transport

flowboard::Edge wraps a bounded lock-free SPSC ring (rigtorp/SPSCQueue). Each
edge carries a flowboard::BackpressurePolicy:

- **`Block`** — the producer spins/yields until space is available. No message is
  ever lost; a slow consumer throttles the producer.
- **`DropOldest`** — on a full ring the oldest queued message is dropped to make
  room for the newest. Bounded latency, lossy under overload. Drops are counted
  (flowboard::Edge::dropped).

The rationale for the default policy per edge is recorded in
@ref adr_backpressure "ADR-001: backpressure".

### Nodes — independent worker threads

flowboard::Node is the base class for every processing element. Each node owns
a `std::jthread` worker fed by a `std::counting_semaphore`: port sinks call
flowboard::Node::enqueue, the worker wakes and runs the queued task. Per-node
threading isolates failures and lets independent stages run truly in parallel.

Subclasses register their ports in their constructor
(`register_input` / `register_output`) and override
flowboard::Node hooks `on_start()` (wire input sinks here) and `on_stop()`.
Nodes also support pause/resume for the control plane.

### Graph — ownership and lifecycle

flowboard::Graph owns all nodes and edges. Its responsibilities:

- **Construction** — flowboard::Graph::add_node takes ownership of a node.
- **Type-checked wiring** — flowboard::Graph::connect (throwing) and
  flowboard::Graph::connect_checked (non-throwing) reject any connection whose
  source and destination type tags differ, or that names a missing port.
- **Type-erased edge storage** — because edges are templated on `T`, the graph
  stores them behind an internal type-erased holder and builds them through
  flowboard::Graph::wire_typed_edge. The wire-factory registry calls this with
  the concrete `T` recovered from a runtime type tag, so the graph never needs to
  know every struct type up front.
- **Per-edge pump threads** — when started, each edge gets its own `std::jthread`
  that drains the SPSC ring and delivers into the consumer's input port. On stop,
  every pump is requested to stop, then drains remaining items so producers are
  never left blocked.
- **Statistics** — flowboard::Graph::edge_stats reports per-edge pushed/dropped
  counters for the `/api/stats` endpoint.

## Message flow, end to end

```
 OutputPort<T>::emit(v)
      |  (sink installed by wire_typed_edge)
      v
 Edge<T>::push(v) ──> SPSC ring  ──> pump jthread: Edge<T>::try_pop(v)
                                          |
                                          v
                                   InputPort<T>::deliver(v)
                                          |
                                          v
                                   Node::enqueue(task)  ──> worker jthread runs node logic
```

Every hop transfers a `shared_ptr<const T>`, so ownership is shared and the
payload is never mutated in flight.

## Threading model

| Thread | Count | Owns | Purpose |
|--------|-------|------|---------|
| Node worker | one per node | flowboard::Node | Runs node logic; woken by a counting semaphore. |
| Edge pump | one per edge | flowboard::Graph internal holder | Drains the SPSC ring into the consumer port. |
| Control plane | one | server | HTTP + WebSocket I/O. |
| Main | one | process | Parses argv, builds the graph, installs signal handler. |

The SPSC invariant matters: each edge has exactly one producer (the upstream
output-port sink) and exactly one consumer (its pump thread). Counters are
atomic; node-internal task queues are mutex-protected.

## Three families of nodes

1. **OnboardAPI Service / Client nodes** — thin pass-throughs around the
   `M_<Interface>::Service` / `M_<Interface>::Client` SDK handles, generated by
   `pipegen` from `.rmodel` files (one Service + one Client class per interface).
2. **Transform nodes** — where workflow logic lives: Threshold, Compare, Convert,
   Filter, Inverter, Throttle, Synchronize, ConstantSource, KeyValueAccumulator,
   Extract, and the `Transform.List.*` operations. Hand-written under `src/nodes/`.
3. **Sinks** — `Sinks.Log` writes to stdout / a log file; the web UI's live-value
   pane is also a sink, fed over WebSocket via flowboard::LivePublishFn.

## Registry and self-registration

flowboard::NodeRegistry is a process-wide singleton mapping a string type name
(e.g. `"Transform.Threshold"`) to a factory plus an optional JSON property schema.
Nodes register themselves at static-init time via the macros in
`registry.hpp`:

- `OP_REGISTER_NODE(name, Class)` — register a node factory.
- `OP_REGISTER_NODE_WITH_SCHEMA(...)` — also register the property-form schema.
- `OP_REGISTER_NODE_LAMBDA_WITH_SCHEMA(...)` — for factory lambdas.

Message types declare their type tag with `OP_DECLARE_TYPE(Type, "Tag")`
(see type_tag.hpp), which specializes flowboard::TypeTag so
flowboard::type_tag_v works for that type.

## Codegen — pipegen

`pipegen` is a separate C++20 executable that parses `.rmodel` files (a CORBA-IDL
subset) and emits, per module:

- `<module>_types.hpp` — struct definitions, `OP_DECLARE_TYPE` registrations, and
  `to_json` serializers.
- `<module>_nodes.cpp` — generated Service / Client node classes.
- `<module>_stub.hpp` — stub SDK headers consumed by `sdk_stub/`.
- `<module>_types.ts` — TypeScript catalog metadata for the web UI.

This is what lets the engine cover the full OnboardAPI interface surface without
hand-writing a node per interface. See @ref adding_a_node for adding a
hand-written node.

## Stub vs. real SDK

`sdk_stub/` mimics enough of the real OnboardAPI SDK ABI that generated code
compiles and runs **without** the proprietary binaries — an in-process pub/sub
bus keyed by `(domainId, serviceName, opName)`, no DDS or networking. The default
build links the real SDK; pass `FLOWBOARD_USE_STUB_SDK=ON` to use the stub
instead. To link the real SDK, see @ref integrating_real_sdk.

## Control plane and hot reload

A Crow-based HTTP + WebSocket server (default `127.0.0.1:8765`) exposes the REST
API and serves the embedded web UI. The live graph lives in
flowboard::GraphHolder, which guards it with a mutex and supports atomic
hot-swap: flowboard::GraphHolder::reload validates a new JSON config, builds a
fresh flowboard::Graph, and swaps it in — rolling back on any validation
error so a bad config never takes down a running pipeline.

## Web UI

`web/` is a Vite + React + TypeScript app (React Flow + Zustand + Tailwind). It is
built at compile time and the `dist/` bundle is embedded into the binary via
`cmake/EmbedWebAssets.cmake`, so the single executable serves its own
configurator at `/`.

## Lifecycle

1. `main` parses argv and loads the JSON config.
2. The loader validates it and builds a flowboard::Graph through the
   flowboard::NodeRegistry.
3. flowboard::GraphHolder::install attaches live taps and starts the graph,
   which starts every node worker and every edge pump thread.
4. The control-plane server exposes the REST + WS API and serves the web UI.
5. Ctrl-C → signal handler sets a stop flag → server stops, holder stops, exit.

## Source layout

```
include/flowboard/   public engine API (this reference)
src/engine/             engine implementation
src/nodes/              transform + sink nodes
src/config/             JSON loader + schema
src/control/            Crow server + routes
src/onboardapi/runtime/ Service/Client node base classes
pipegen/                codegen tool
sdk_stub/               in-tree stub OnboardAPI SDK (in-process bus, no DDS)
external/onboardapi/    Rheinmetall/onboardapi submodule (.rmodel data model)
web/                    React configurator UI
docs/                   guides, ADRs, and this documentation
```
