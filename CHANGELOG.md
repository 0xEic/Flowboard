# Changelog

All notable changes to Flowboard are documented in this file. The format is
based on [Keep a Changelog](https://keepachangelog.com/), and this project
adheres to [Semantic Versioning](https://semver.org/).

## [1.1.0] - 2026-06-27

### Added
- **Hierarchical `Flow.StateMachine`.** The state machine was reworked from a flat
  FSM into a hierarchical one: states may carry a nested `machine` (a composite
  state, nestable to arbitrary depth), and a level may hold several independent
  chains so **many states are active at once** — across sibling chains and nested
  levels alike. Transitions gain a `kind`: `trigger` (default, fired by a bool
  input port), `null` (fires immediately on entry) or `timed` (fires after
  `delayMs` while the source stays active). Outputs are dotted by composite path:
  `state` (comma-joined active set), `active.<path>` per state, `transition` and
  `changed`. The web editor adds a machine inspector, dedicated trigger-edge
  rendering, and a compile self-check.
- **CAN bus subsystem.** `Can.Bus`, `Can.Encode`, `Can.Decode` and `Can.Filter`
  nodes over a pluggable adapter registry, with an in-process **virtual** adapter
  (no hardware) and a Linux **SocketCAN** backend. Adapters can also be supplied
  by plugins.
- **Serial transport.** `Serial.Port` with overlapped (async) Windows I/O and a
  POSIX backend, plus `Framer.Fixed` / `Framer.Delimiter` / `Framer.LengthPrefix`
  for reframing a byte stream into records.
- **`Python.Script` node.** Runs user Python in a managed subprocess, exchanging
  typed values (including OnboardAPI structs) over its declared input/output
  ports. Requires `python` on `PATH`. See `docs/PYTHON_NODE.md`.
- **Native plugin system.** A plugin host loads `.dll`/`.so` plugins that can
  register node types and CAN adapters through a public plugin SDK and a
  port-factory registry. Ships with `hello_plugin` and `fake_can_vendor` examples
  under `examples/plugins/`. See `docs/PLUGINS.md`.
- **New nodes:** `Transform.Delay` (delay/debounce a stream) and
  `Transform.Pulse` (emit timed pulses).
- **Bit-level field placement in `Bytes.Pack` / `Bytes.Unpack`.** Each field may
  carry an optional `bitOffset` (0–7) to place it at bit position
  `offset*8 + bitOffset`, LSB-first (Intel), using the type's natural width
  (`Bool` = 1 bit). Fields without the key are byte-identical to before; the web
  form gains an optional bit column.
- **Web UI onboarding & help.** An intro tour, a per-node Help panel with icons
  backed by rewritten node help text, an in-app Python code editor, and a graph
  mini-preview.
- New example graphs:
  - `can-bus.json` — the full CAN/Bus path (`Can.Bus` ×2, `Can.Encode`,
    `Can.Decode`, `Can.Filter`) on the in-process virtual adapter; payload built
    and recovered with `Bytes.Pack` / `Bytes.Unpack`. Runs as-is, no hardware.
    Replaces `can-serial-bus.json`, which used `Python.Script` to fake the data.
  - `serial-port.json` — `Serial.Port` tx/rx with `Framer.Fixed` and
    `Bytes.Pack` / `Bytes.Unpack`; self-runs over a com0com `COM3↔COM4` pair.
  - `complex-state-machine.json` — parallel chains, a nested sub-state-machine,
    and all three transition kinds (`trigger`, `null`, `timed`).
  - `python-doubler.json` and `python-onboard-struct.json` — `Python.Script` over
    a plain `Double` and over an OnboardAPI struct, respectively.

### Fixed
- **Windows serial transport: concurrent transmit + receive no longer stalls.**
  `Serial.Port` now uses overlapped (async) I/O, so a port that both transmits and
  receives (e.g. a com0com loopback where the engine owns both ends) no longer
  serializes reads against writes and starves itself. The read timeout was also
  corrected so reads return promptly instead of blocking until the buffer fills.
- **Serial node lifecycle hardening.** Port teardown now runs inline after the
  reader thread is joined (the previous enqueue ran after the node worker had
  already stopped), and the reader holds its own port reference — removing a
  narrow error-path use-after-free.

### Changed
- Rewrote every in-app node Help entry with a clear description and a runnable
  example, and corrected several stale port/config/example references.

## [1.0.0]

- Initial release.
