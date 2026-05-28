# ADR-001 — Backpressure semantics in `Edge<T>` {#adr_backpressure}

## Context

Phase 1 introduced `Edge<T>` as a `rigtorp::SPSCQueue<shared_ptr<const T>>` with two
backpressure policies (`Block`, `DropOldest`). The Phase-3 review surfaced that the
ring buffer is never read on the production path: `Graph::connect()` attaches a sink
that calls `Edge::record_push()` (counter only) and then `InputPort::deliver(v)`
synchronously. The configured `capacity` and `policy` keys in `graph.json` are parsed
and stored but have zero runtime effect.

This is a spec/promise mismatch and must be resolved before v1.0.0.

## Decision

**Phase 4 chooses Option A:** route deliveries through the SPSC ring with a
per-edge pump thread, restoring backpressure semantics. Specifically:

- `Graph::connect()` no longer calls `sin->deliver(v)` synchronously from the
  producer's emit. Instead, the attached sink lambda calls `edge->push(v)`.
- Each `EdgeHolder<T>` spawns a `std::jthread` pump on `Graph::start()`. The pump
  loops: try-pop → if value, `sin->deliver(v)`; else sleep 50µs.
- `BackpressurePolicy::Block` blocks the producer when full (existing behavior of
  `Edge::push`); `DropOldest` discards the oldest item (existing). Both now have
  observable effect.
- The pump joins on `Graph::stop()` after the per-node workers stop, so producers
  are drained first.

## Consequences

- Throughput drops modestly. Bench expected to land between 300 k/s and 1.5 M/s
  depending on busy-vs-idle pump behavior. Still ≥ 3× the 100 k/s spec floor.
- Latency adds one ~50µs sleep tick (worst case) per hop. The example workflow
  publishes at most ~50 messages/sec; this latency is invisible.
- The Phase-3 `record_push()` becomes a thin wrapper that the new code-path uses
  unchanged — counter still increments.
- CPU usage rises slightly: N threads sleeping on a 50µs interval cost roughly
  20 000 wakeups/sec/edge. Acceptable; future improvement would be a per-edge
  semaphore instead of polled sleep.

## Alternatives considered

- Option B (config has no effect, documented): rejected — would ship a v1.0.0 that
  lies in its config schema.
- Option C (remove the ring): rejected — the ring is the most useful artifact
  Phase 1 produced and we'd lose the Block / DropOldest distinction with no path
  back. A semaphore-driven pump (instead of polled 50µs) is the right Phase 5
  improvement, not Phase 4.
