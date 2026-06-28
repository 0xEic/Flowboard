// SPDX-License-Identifier: MIT
//
// Right-hand properties panel for the StateMachine inner canvas -- the machine
// analogue of <Inspector />. Shows the selected state or transition and lets you
// edit its configuration. Selection is owned by MachineCanvas (React Flow local
// state) and passed in; the live state/edge is read from the store by id/index
// so edits round-trip through the same actions the canvas node views use.

import { useGraphStore } from '../store/graph_store';
import type { InnerEdge } from '../lib/machine_compile';

function Shell({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <aside className="w-80 shrink-0 bg-slate-800 border-l border-slate-700 p-3 text-sm flex flex-col gap-2 overflow-y-auto">
      <div className="font-semibold">{title}</div>
      {children}
    </aside>
  );
}

function StatePanel({ stateId }: { stateId: string }) {
  const nodes = useGraphStore(s => s.innerCanvas?.nodes ?? []);
  const edges = useGraphStore(s => s.innerCanvas?.edges ?? []);
  const update = useGraphStore(s => s.updateInnerState);
  const toggleInitial = useGraphStore(s => s.toggleInitialState);
  const openSubmachine = useGraphStore(s => s.openSubmachine);
  const removeSubmachine = useGraphStore(s => s.removeSubmachine);
  const node = nodes.find(n => n.id === stateId);
  const nameOf = (id: string) => { const n = nodes.find(x => x.id === id); return n && n.kind === 'state' ? n.name : id; };
  if (!node || node.kind !== 'state') return null;

  const outgoing = edges.filter(e => e.source === stateId);
  const incoming = edges.filter(e => e.target === stateId);

  return (
    <Shell title="State">
      <label className="flex flex-col gap-1">
        <span className="text-xs text-slate-400">Name</span>
        <input
          className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600 font-semibold"
          value={node.name}
          onChange={e => update(stateId, { name: e.target.value })}
          spellCheck={false}
        />
      </label>

      <label className="flex items-center gap-2 text-xs text-slate-300 cursor-pointer">
        <input
          type="checkbox"
          checked={node.initial}
          onChange={() => toggleInitial(stateId)}
        />
        Initial state
      </label>
      {node.initial ? (
        <div className="text-[11px] text-slate-500">Entry point for its chain &mdash; active when the machine starts.</div>
      ) : (
        <div className="text-[11px] text-slate-500">Tick to make this an entry point. Each chain needs exactly one; chains run concurrently.</div>
      )}

      <div className="grid grid-cols-2 gap-x-2 gap-y-0.5 text-[11px] text-slate-400 mt-1">
        <span>x</span><span className="text-slate-300">{Math.round(node.position.x)}</span>
        <span>y</span><span className="text-slate-300">{Math.round(node.position.y)}</span>
      </div>

      <div className="mt-2 border-t border-slate-700 pt-2">
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Substate machine</div>
        {node.machine ? (
          <div className="flex flex-col gap-1.5">
            <div className="text-[11px] text-slate-400">
              {node.machine.states?.length ?? 0} state{(node.machine.states?.length ?? 0) === 1 ? '' : 's'} ·{' '}
              {node.machine.transitions?.length ?? 0} transition{(node.machine.transitions?.length ?? 0) === 1 ? '' : 's'}
            </div>
            <div className="flex gap-2">
              <button
                className="flex-1 px-2 py-1 rounded bg-sky-700 hover:bg-sky-600 text-xs text-white"
                onClick={() => openSubmachine(stateId)}
              >⤢ Open</button>
              <button
                className="px-2 py-1 rounded border border-rose-700/60 text-xs text-rose-300 hover:bg-rose-900/30"
                onClick={() => { if (confirm('Remove this substate machine? Its nested states are discarded.')) removeSubmachine(stateId); }}
              >Remove</button>
            </div>
            <div className="text-[11px] text-slate-500">
              Runs while this state is active; resets to its initial state(s) each time the state is (re-)entered.
            </div>
          </div>
        ) : (
          <div className="flex flex-col gap-1.5">
            <button
              className="px-2 py-1 rounded bg-slate-700 hover:bg-slate-600 text-xs text-slate-100 self-start"
              onClick={() => openSubmachine(stateId)}
            >+ Add substate machine</button>
            <div className="text-[11px] text-slate-500">
              Make this a composite state with its own nested state machine. Its triggers appear on the node, grouped under “{node.name}”.
            </div>
          </div>
        )}
      </div>

      <div className="mt-2 border-t border-slate-700 pt-2">
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">
          Transitions ({outgoing.length} out · {incoming.length} in)
        </div>
        <div className="flex flex-col gap-1">
          {outgoing.map((e, i) => (
            <div key={'o' + i} className="text-[11px] text-slate-300 bg-slate-900/40 border border-slate-700/60 rounded px-1.5 py-0.5 truncate">
              <span className="text-amber-300">{e.trigger || '?'}</span>
              <span className="text-slate-500"> → </span>{nameOf(e.target)}
            </div>
          ))}
          {incoming.map((e, i) => (
            <div key={'i' + i} className="text-[11px] text-slate-400 bg-slate-900/40 border border-slate-700/60 rounded px-1.5 py-0.5 truncate">
              {nameOf(e.source)}<span className="text-slate-500"> → </span>
              <span className="text-amber-300">{e.trigger || '?'}</span>
            </div>
          ))}
          {outgoing.length === 0 && incoming.length === 0 && (
            <div className="text-[11px] text-slate-500">No transitions touch this state.</div>
          )}
        </div>
      </div>
    </Shell>
  );
}

// One geometry parameter row: shows whether it's been customised and offers a
// reset back to the auto/default layout.
function GeomRow({ label, customised, onReset }: { label: string; customised: boolean; onReset: () => void }) {
  return (
    <div className="flex items-center justify-between gap-2 text-[11px]">
      <span className="text-slate-400">{label}</span>
      <div className="flex items-center gap-2">
        <span className={customised ? 'text-sky-300' : 'text-slate-500'}>
          {customised ? 'custom' : 'auto'}
        </span>
        <button
          className="px-1.5 py-0.5 rounded border border-slate-600 text-slate-300 hover:bg-slate-700 disabled:opacity-40 disabled:hover:bg-transparent"
          disabled={!customised}
          onClick={onReset}
        >reset</button>
      </div>
    </div>
  );
}

function LinkPanel({ index }: { index: number }) {
  const nodes = useGraphStore(s => s.innerCanvas?.nodes ?? []);
  const edge = useGraphStore(s => s.innerCanvas?.edges[index] as InnerEdge | undefined);
  const update = useGraphStore(s => s.updateInnerEdge);
  const nameOf = (id: string) => { const n = nodes.find(x => x.id === id); return n && n.kind === 'state' ? n.name : id; };
  if (!edge) return null;

  const isSelfLoop = edge.source === edge.target;
  const curveCustom = isSelfLoop
    ? !!edge.controlPoint && (edge.controlPoint.x !== 0 || edge.controlPoint.y !== -60)
    : !!edge.controlPoint;
  const labelCustom = edge.labelOffset.x !== 0 || edge.labelOffset.y !== 0;
  const sourceCustom = !!edge.sourceOffset && (edge.sourceOffset.x !== 0 || edge.sourceOffset.y !== 0);
  const targetCustom = !!edge.targetOffset && (edge.targetOffset.x !== 0 || edge.targetOffset.y !== 0);

  return (
    <Shell title="Transition">
      <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 text-xs bg-slate-900 border border-slate-700 rounded p-2">
        <span className="text-slate-400">from</span>
        <span className="text-slate-200 truncate" title={nameOf(edge.source)}>{nameOf(edge.source)}</span>
        <span className="text-slate-400">to</span>
        <span className="text-slate-200 truncate" title={nameOf(edge.target)}>{nameOf(edge.target)}</span>
      </div>

      <label className="flex flex-col gap-1">
        <span className="text-xs text-slate-400">Type</span>
        <select
          className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600 text-slate-100"
          value={edge.kind}
          onChange={e => update(index, { kind: e.target.value as InnerEdge['kind'] })}
        >
          <option value="trigger">Trigger-driven</option>
          <option value="null">Null (immediate)</option>
          <option value="timed">Timed</option>
        </select>
      </label>

      {edge.kind === 'trigger' && (
        <label className="flex flex-col gap-1">
          <span className="text-xs text-slate-400">Trigger</span>
          <input
            className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600 text-amber-200"
            value={edge.trigger}
            onChange={e => update(index, { trigger: e.target.value })}
            placeholder="boolean trigger port"
            spellCheck={false}
          />
          <span className="text-[11px] text-slate-500">Fires when this boolean input port becomes true.</span>
        </label>
      )}
      {edge.kind === 'timed' && (
        <label className="flex flex-col gap-1">
          <span className="text-xs text-slate-400">Delay (ms)</span>
          <input
            type="number" min={1}
            className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600 text-cyan-200"
            value={edge.delayMs}
            onChange={e => update(index, { delayMs: Math.max(1, Math.trunc(Number(e.target.value) || 0)) })}
          />
          <span className="text-[11px] text-slate-500">Fires after this delay once the source state is active; restarts on re-entry.</span>
        </label>
      )}
      {edge.kind === 'null' && (
        <div className="text-[11px] text-slate-500">
          Fires immediately when the source state becomes active — emits its outputs and jumps straight to the target. No trigger needed.
        </div>
      )}

      <div className="mt-2 border-t border-slate-700 pt-2 flex flex-col gap-1.5">
        <div className="text-xs uppercase tracking-wide text-slate-400">Layout</div>
        <GeomRow label={`Joints (${edge.waypoints.length})`} customised={edge.waypoints.length > 0}
                 onReset={() => update(index, { waypoints: [] })} />
        <GeomRow label="Curve" customised={curveCustom}
                 onReset={() => update(index, { controlPoint: isSelfLoop ? { x: 0, y: -60 } : null })} />
        <GeomRow label="Label position" customised={labelCustom}
                 onReset={() => update(index, { labelOffset: { x: 0, y: 0 } })} />
        <GeomRow label="Source endpoint" customised={sourceCustom}
                 onReset={() => update(index, { sourceOffset: null })} />
        <GeomRow label="Target endpoint" customised={targetCustom}
                 onReset={() => update(index, { targetOffset: null })} />
      </div>
    </Shell>
  );
}

export function MachineInspector({
  selectedStateId, selectedEdgeIndex, selectionCount,
}: {
  selectedStateId: string | null;
  selectedEdgeIndex: number | null;
  selectionCount: number;
}) {
  const counts = useGraphStore(s => ({
    states: s.innerCanvas?.nodes.length ?? 0,
    edges: s.innerCanvas?.edges.length ?? 0,
  }));

  if (selectedStateId) return <StatePanel key={selectedStateId} stateId={selectedStateId} />;
  if (selectedEdgeIndex !== null) return <LinkPanel key={selectedEdgeIndex} index={selectedEdgeIndex} />;

  return (
    <Shell title="Machine">
      {selectionCount > 1 ? (
        <div className="text-xs text-slate-400">
          {selectionCount} items selected. Select a single state or transition to edit it.
        </div>
      ) : (
        <div className="text-xs text-slate-400">
          Select a state or transition on the canvas to edit its properties.
        </div>
      )}
      <div className="text-xs text-slate-500">
        {counts.states} state{counts.states === 1 ? '' : 's'} ·{' '}
        {counts.edges} transition{counts.edges === 1 ? '' : 's'}
      </div>
    </Shell>
  );
}
