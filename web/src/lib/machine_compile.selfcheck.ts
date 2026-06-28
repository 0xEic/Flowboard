// SPDX-License-Identifier: MIT
// Pure-function self-checks for machine_compile. The web/ tree has no test
// runner -- invoke this from the dev console:
//   await import('/src/lib/machine_compile.selfcheck.ts').then(m => m.runSelfChecks())
// On success: returns the number of checks that ran. On failure: throws with
// a precise message.
import {
  compile, decompile, machineOuterPorts,
  type InnerCanvas, type InnerEdge, type InnerStateNode, type MachineConfig,
} from './machine_compile';

function eq<T>(actual: T, expected: T, msg: string): void {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a !== e) throw new Error(`${msg}\n  actual:   ${a}\n  expected: ${e}`);
}

function checkRoundTrip(): number {
  const config: MachineConfig = {
    initial: 'Green',
    states: [
      { name: 'Green',  position: { x: 0,   y: 0 } },
      { name: 'Yellow', position: { x: 200, y: 0 } },
      { name: 'Red',    position: { x: 400, y: 0 } },
    ],
    transitions: [
      { from: 'Green',  to: 'Yellow', trigger: 'go',
        labelOffset: { x: 10, y: -8 }, controlPoint: { x: 0, y: -30 } },
      { from: 'Yellow', to: 'Red',    trigger: 'crit' },
      { from: 'Red',    to: 'Red',    trigger: 'stay' },  // self-loop
      { from: 'Yellow', to: 'Green',  trigger: 'clear' },
    ],
  };

  const canvas = decompile(config);

  // 3 state nodes, 4 edges.
  eq(canvas.nodes.length, 3, 'round-trip: state count');
  eq(canvas.edges.length, 4, 'round-trip: edge count');

  // Self-loop got the default control point.
  const self = canvas.edges.find(e => e.source === 'state:Red' && e.target === 'state:Red');
  if (!self) throw new Error('round-trip: missing self-loop edge');
  if (!self.controlPoint || self.controlPoint.y >= 0)
    throw new Error('round-trip: self-loop default controlPoint missing or wrong sign');

  const back = compile(canvas);
  if (!back.config) throw new Error(`round-trip: compile errored: ${back.errors.join('; ')}`);

  // Order of transitions preserved.
  eq(back.config.transitions.map(t => `${t.from}->${t.to}:${t.trigger}`),
     ['Green->Yellow:go', 'Yellow->Red:crit', 'Red->Red:stay', 'Yellow->Green:clear'],
     'round-trip: transition order');

  // Explicit labelOffset / controlPoint preserved on the first edge.
  const first = back.config.transitions[0];
  eq(first.labelOffset,  { x: 10, y: -8 }, 'round-trip: labelOffset preserved');
  eq(first.controlPoint, { x: 0,  y: -30 }, 'round-trip: controlPoint preserved');

  // Default labelOffset omitted from output (compile skips zero offsets).
  if (back.config.transitions[1].labelOffset !== undefined)
    throw new Error('round-trip: zero labelOffset should be omitted from output');

  return 6;
}

function checkLegacyMigration(): number {
  // Legacy file: transition with `position` but no labelOffset/controlPoint.
  const config: MachineConfig = {
    initial: 'A',
    states: [
      { name: 'A', position: { x: 0,   y: 0 } },
      { name: 'B', position: { x: 200, y: 0 } },
    ],
    transitions: [
      { from: 'A', to: 'B', trigger: 't', position: { x: 100, y: -30 } },
    ],
  };
  const canvas = decompile(config);
  const e = canvas.edges[0];
  // Centre midpoint is (100, 0); position is (100, -30); so labelOffset is (0, -30).
  eq(e.labelOffset, { x: 0, y: -30 }, 'legacy: labelOffset from position');
  if (e.controlPoint !== null)
    throw new Error('legacy: controlPoint should be null for non-self-loop legacy edge');
  return 2;
}

function checkMultiEdgeSamePair(): number {
  const canvas: InnerCanvas = {
    nodes: [
      { id: 'state:A', kind: 'state', name: 'A', initial: true,  position: { x: 0, y: 0 }, machine: null },
      { id: 'state:B', kind: 'state', name: 'B', initial: false, position: { x: 200, y: 0 }, machine: null },
    ],
    edges: [
      mkEdge('state:A', 'state:B', 't1'),
      mkEdge('state:A', 'state:B', 't2'),
    ],
  };
  const r = compile(canvas);
  if (!r.config) throw new Error(`multi-edge-same-pair: ${r.errors.join('; ')}`);
  eq(r.config.transitions.length, 2, 'multi-edge-same-pair: 2 transitions emitted');
  return 1;
}

function checkErrors(): number {
  const A = (initial: boolean): InnerCanvas['nodes'][number] =>
    ({ id: 'state:A', kind: 'state', name: 'A', initial, position: { x: 0, y: 0 }, machine: null });
  const B = (initial: boolean): InnerCanvas['nodes'][number] =>
    ({ id: 'state:B', kind: 'state', name: 'B', initial, position: { x: 100, y: 0 }, machine: null });

  // Empty trigger.
  const r1 = compile({
    nodes: [A(true), B(false)],
    edges: [mkEdge('state:A', 'state:B', '')],
  });
  if (r1.config) throw new Error('errors: empty trigger should fail');
  if (!r1.errors.some(e => e.includes('empty trigger')))
    throw new Error(`errors: expected 'empty trigger', got ${r1.errors.join('; ')}`);

  // Dangling endpoint.
  const r2 = compile({
    nodes: [A(true)],
    edges: [mkEdge('state:A', 'state:GONE', 't')],
  });
  if (r2.config) throw new Error('errors: dangling endpoint should fail');
  if (!r2.errors.some(e => e.includes('endpoint')))
    throw new Error(`errors: expected 'endpoint', got ${r2.errors.join('; ')}`);

  // No initial (each lone state is its own chain, so both chains lack one).
  const r3 = compile({ nodes: [A(false), B(false)], edges: [] });
  if (r3.config) throw new Error('errors: no initial should fail');

  // Multiple initials in ONE chain (A and B connected) should fail.
  const r4 = compile({ nodes: [A(true), B(true)], edges: [mkEdge('state:A', 'state:B', 'go')] });
  if (r4.config) throw new Error('errors: multiple initials in one chain should fail');

  // Duplicate name.
  const r5 = compile({
    nodes: [A(true), { ...B(false), name: 'A' } as InnerStateNode],
    edges: [],
  });
  if (r5.config) throw new Error('errors: duplicate name should fail');

  return 5;
}

function checkMultiChain(): number {
  // Two disjoint chains, each with its own initial -> valid; both entry states
  // are emitted with per-state initial flags and can be active concurrently.
  const canvas: InnerCanvas = {
    nodes: [
      { id: 'state:Idle',  kind: 'state', name: 'Idle',  initial: true,  position: { x: 0,   y: 0 },   machine: null },
      { id: 'state:Run',   kind: 'state', name: 'Run',   initial: false, position: { x: 200, y: 0 },   machine: null },
      { id: 'state:Armed', kind: 'state', name: 'Armed', initial: true,  position: { x: 0,   y: 200 }, machine: null },
      { id: 'state:Fire',  kind: 'state', name: 'Fire',  initial: false, position: { x: 200, y: 200 }, machine: null },
    ],
    edges: [
      mkEdge('state:Idle',  'state:Run',  'go'),
      mkEdge('state:Armed', 'state:Fire', 'trip'),
    ],
  };
  const r = compile(canvas);
  if (!r.config) throw new Error(`multi-chain: should compile, got ${r.errors.join('; ')}`);
  const initialNames = r.config.states.filter(s => s.initial).map(s => s.name).sort();
  eq(initialNames, ['Armed', 'Idle'], 'multi-chain: two initial flags emitted');
  if (!initialNames.includes(r.config.initial))
    throw new Error('multi-chain: legacy top-level initial should be one of the initials');

  // A chain missing its own initial fails even if another chain has one.
  const bad = compile({
    nodes: [
      { id: 'state:Idle', kind: 'state', name: 'Idle', initial: true,  position: { x: 0,   y: 0 },   machine: null },
      { id: 'state:Run',  kind: 'state', name: 'Run',  initial: false, position: { x: 200, y: 0 },   machine: null },
      { id: 'state:Lone', kind: 'state', name: 'Lone', initial: false, position: { x: 0,   y: 200 }, machine: null },
    ],
    edges: [mkEdge('state:Idle', 'state:Run', 'go')],
  });
  if (bad.config) throw new Error('multi-chain: a chain with no initial should fail');
  return 3;
}

function checkNested(): number {
  // A composite state (Heating) with its own substate machine, round-tripped.
  const config: MachineConfig = {
    initial: 'Idle',
    states: [
      { name: 'Idle', position: { x: 0, y: 0 }, initial: true },
      { name: 'Heating', position: { x: 200, y: 0 }, machine: {
        initial: 'Warming',
        states: [
          { name: 'Warming', position: { x: 0, y: 0 }, initial: true },
          { name: 'Hot', position: { x: 200, y: 0 } },
        ],
        transitions: [{ from: 'Warming', to: 'Hot', trigger: 'warn' }],
      } },
    ],
    transitions: [
      { from: 'Idle', to: 'Heating', trigger: 'go' },
      { from: 'Heating', to: 'Idle', trigger: 'stop' },
    ],
  };

  const canvas = decompile(config);
  const heating = canvas.nodes.find(n => n.id === 'state:Heating');
  if (!heating || heating.kind !== 'state' || !heating.machine)
    throw new Error('nested: Heating.machine not decompiled');
  eq(heating.machine.states.map(s => s.name), ['Warming', 'Hot'], 'nested: substate names');

  const back = compile(canvas);
  if (!back.config) throw new Error(`nested: compile errored: ${back.errors.join('; ')}`);
  const outHeating = back.config.states.find(s => s.name === 'Heating');
  if (!outHeating?.machine) throw new Error('nested: machine not re-emitted on compile');
  eq(outHeating.machine.transitions.map(t => `${t.from}->${t.to}:${t.trigger}`),
     ['Warming->Hot:warn'], 'nested: substate transition preserved');

  // Ports: dotted triggers + active.<path> at every level.
  const ports = machineOuterPorts(back.config);
  eq(ports.inputs.map(p => p.name).sort(),
     ['Heating.warn', 'go', 'stop'].sort(), 'nested: dotted trigger ports');
  eq(ports.outputs.filter(p => p.name.startsWith('active.')).map(p => p.name).sort(),
     ['active.Heating', 'active.Heating.Hot', 'active.Heating.Warming', 'active.Idle'].sort(),
     'nested: active ports per level');

  // Recursive validation: a substate chain with no initial fails, path-qualified.
  const badCanvas = decompile({
    initial: 'Idle',
    states: [
      { name: 'Idle', position: { x: 0, y: 0 }, initial: true },
      { name: 'Heating', position: { x: 200, y: 0 }, machine: {
        initial: '', states: [{ name: 'Warming', position: { x: 0, y: 0 } }], transitions: [],
      } },
    ],
    transitions: [{ from: 'Idle', to: 'Heating', trigger: 'go' }],
  });
  const badR = compile(badCanvas);
  if (badR.config) throw new Error('nested: substate with no initial should fail');
  if (!badR.errors.some(e => e.includes('Heating:')))
    throw new Error(`nested: error should be path-qualified, got ${badR.errors.join('; ')}`);

  return 5;
}

function mkEdge(source: string, target: string, trigger: string): InnerEdge {
  return { source, target, kind: 'trigger', trigger, delayMs: 1000, waypoints: [], labelOffset: { x: 0, y: 0 }, controlPoint: null, sourceOffset: null, targetOffset: null };
}

export function runSelfChecks(): number {
  let n = 0;
  n += checkRoundTrip();
  n += checkLegacyMigration();
  n += checkMultiEdgeSamePair();
  n += checkMultiChain();
  n += checkNested();
  n += checkErrors();
  return n;
}
