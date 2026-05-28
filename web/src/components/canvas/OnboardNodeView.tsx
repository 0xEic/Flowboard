// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { Handle, Position, type NodeProps } from 'reactflow';
import { useGraphStore } from '../../store/graph_store';
import { colorForTypeTag } from '../../lib/typeColor';
import { nodePorts } from '../../lib/node_ports';
import { resolveTapKey } from '../../lib/group_compile';
import { TriggerButtonView, ValueDisplayView, GraphDisplayView } from './DebugNodeViews';
import { GroupInputView, GroupOutputView } from './GroupTerminalViews';
import { NoteNodeView } from './NoteNodeView';

type Data = {
  id: string;
  type: string;
};

type Port = { name: string; typeTag: string };

const dot = (tag: string) => ({
  width: 10, height: 10, borderRadius: 999,
  background: colorForTypeTag(tag),
  border: '1px solid #0f172a',
});

// Primitive type tags that get an inline default-value editor on the canvas.
// Struct-typed inputs still need a connection — they have no editor.
const NUMBER_TAGS = new Set([
  'flowboard::Int64', 'flowboard::UInt64', 'flowboard::Int32', 'flowboard::UInt32',
  'flowboard::Int16', 'flowboard::UInt16', 'flowboard::UInt8',
  'flowboard::Double', 'flowboard::Float',
]);
const INT_TAGS = new Set([
  'flowboard::Int64', 'flowboard::UInt64', 'flowboard::Int32', 'flowboard::UInt32',
  'flowboard::Int16', 'flowboard::UInt16', 'flowboard::UInt8',
]);
const PRIM_TAGS = new Set([
  'flowboard::Bool', 'flowboard::String', 'flowboard::Char', ...NUMBER_TAGS,
]);
const isPrimitive = (tag: string) => PRIM_TAGS.has(tag);
const isFactoryType = (typeName: string) => typeName.startsWith('Factory.');

// "trigger" inputs (Factory.* + multi-param Cmd composers) get a visual cue
// so they read as the "fire" signal rather than a data input.
const isTrigger = (p: Port) => p.name === 'trigger' || p.name.endsWith('.trigger');

// Group ports by the segment before the first '.' in their name. Ports with
// no '.' collect into a synthetic "" group rendered without a header.
//
// The C++ side stores ports in std::unordered_map, so the catalog API
// returns them in hash-bucket order — not declaration order. We can't merge
// adjacent same-op ports because the source order interleaves ops. Instead
// we collect into a Map keyed by op-name, then sort: ungrouped first, then
// op groups alphabetically. Within each group, trigger ports sink to the
// bottom and everything else sorts alphabetically so the visual order is
// stable across reloads.
function groupPorts(ports: Port[]): { op: string; ports: Port[] }[] {
  const map = new Map<string, Port[]>();
  for (const p of ports) {
    const dot = p.name.indexOf('.');
    const op = dot < 0 ? '' : p.name.slice(0, dot);
    const arr = map.get(op);
    if (arr) arr.push(p);
    else map.set(op, [p]);
  }
  for (const arr of map.values()) {
    arr.sort((a, b) => {
      const at = isTrigger(a) ? 1 : 0;
      const bt = isTrigger(b) ? 1 : 0;
      if (at !== bt) return at - bt;
      return a.name.localeCompare(b.name);
    });
  }
  return [...map.entries()]
    .sort(([a], [b]) => {
      if (a === b) return 0;
      if (a === '') return -1;
      if (b === '') return 1;
      return a.localeCompare(b);
    })
    .map(([op, ports]) => ({ op, ports }));
}

// Strip the "<Op>." prefix from a port label when it's already shown in the
// group header. Falls back to the original name for ungrouped ports.
function displayName(name: string, op: string): string {
  if (!op) return name;
  const prefix = op + '.';
  return name.startsWith(prefix) ? name.slice(prefix.length) : name;
}

// Stale threshold for live values. Older than this and the cell goes dim
// so the user can tell a port has gone quiet without it disappearing.
const LIVE_STALE_MS = 3000;
// Truncate displayed values so a fat struct doesn't blow up the node width.
function formatLiveValue(v: unknown): string {
  const s = typeof v === 'string' ? v : JSON.stringify(v);
  if (s === undefined) return '';
  return s.length > 24 ? s.slice(0, 23) + '…' : s;
}

export function OnboardNodeView({ data, selected }: NodeProps<Data>) {
  const catalog = useGraphStore(s => s.catalog);
  const graph   = useGraphStore(s => s.graph);
  const live    = useGraphStore(s => s.live);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const openMachine = useGraphStore(s => s.openMachine);
  const openGroup   = useGraphStore(s => s.openGroup);
  const promotePort = useGraphStore(s => s.promotePort);
  const groupStack  = useGraphStore(s => s.groupStack);
  const editingGroup = groupStack.length > 0;
  const isMachine = data.type === 'Flow.StateMachine';
  const isGroup   = data.type === 'Flow.Group';
  // Live-value keys are namespaced by the active group path (taps run on the
  // flattened, namespaced graph); a group's output port maps to its inner
  // producer's tap (resolveTapKey).
  const livePrefix = groupStack.length ? groupStack.map(g => g.id).join('/') + '/' : '';
  const liveScope = { nodes: graph?.nodes ?? [], edges: graph?.edges ?? [] };

  // Per-node expand/collapse state for grouped ports, persisted on the node
  // (`expandedGroups`, saved in the graph file) so it survives Apply & Reload.
  // Default: every group collapsed. Keys are direction-qualified ("in:Op" /
  // "out:Op") since input and output groups can share an op name (e.g.
  // multi-param Cmd ops have both input params and a trigger output on Service
  // nodes). Ungrouped ports (op === "") are always shown — nothing to collapse.
  const setNodeExpandedGroups = useGraphStore(s => s.setNodeExpandedGroups);
  const expandedGroups = useMemo(
    () => new Set(graph?.nodes.find(n => n.id === data.id)?.expandedGroups ?? []),
    [graph, data.id],
  );
  const toggleGroup = (key: string) => {
    const next = new Set(expandedGroups);
    if (next.has(key)) next.delete(key);
    else next.add(key);
    setNodeExpandedGroups(data.id, [...next]);
  };

  // Debug.* nodes have bespoke canvas bodies (button / value / chart).
  if (data.type === 'Debug.TriggerButton')
    return <TriggerButtonView id={data.id} type={data.type} selected={!!selected} />;
  if (data.type === 'Debug.ValueDisplay')
    return <ValueDisplayView id={data.id} type={data.type} selected={!!selected} />;
  if (data.type === 'Debug.GraphDisplay')
    return <GraphDisplayView id={data.id} type={data.type} selected={!!selected} />;
  // Group boundary terminals (only ever live inside a group's inner canvas).
  if (data.type === 'Group.Input')
    return <GroupInputView id={data.id} type={data.type} selected={!!selected} />;
  if (data.type === 'Group.Output')
    return <GroupOutputView id={data.id} type={data.type} selected={!!selected} />;
  // Canvas annotation (text only, no ports).
  if (data.type === 'Note')
    return <NoteNodeView id={data.id} type={data.type} selected={!!selected} />;

  const thisNode = graph?.nodes.find(n => n.id === data.id);
  const { inputs, outputs } = nodePorts(
    { type: data.type, config: thisNode?.config },
    catalog,
  );

  const inputGroups  = groupPorts(inputs);
  const outputGroups = groupPorts(outputs);

  // Inline default-value editors for primitive input ports. Where the value is
  // stored depends on the port:
  //  - Factory.* nodes store it in config[<portName>] (seeds the composer cache).
  //  - Ports whose name is ALSO a top-level config property (e.g. the Iterator's
  //    startValue / endValue / stepSize, which are both a schema field and an
  //    input port) store it in config[<portName>] — the same place the property
  //    panel writes and the engine reads — so both editors stay in sync.
  //  - Every other port stores it in config.defaults[<portName>]; the engine
  //    seeds it into the port at graph start when nothing is wired to it.
  // An incoming edge overrides the value at runtime (editor shown muted).
  const isFactory  = isFactoryType(data.type);
  const nodeCfg    = (thisNode?.config ?? {}) as Record<string, unknown>;
  const defaultsCfg = (nodeCfg.defaults ?? {}) as Record<string, unknown>;
  const schemaProps = new Set(
    Object.keys(
      ((catalog.find(c => c.typeName === data.type)?.configSchema as
        { properties?: Record<string, unknown> } | undefined)?.properties) ?? {},
    ),
  );
  // True when the inline value lives at the top level of config (factory inputs
  // and config-backed ports) rather than in config.defaults.
  const inlineAtTopLevel = (portName: string) => isFactory || schemaProps.has(portName);
  const connectedTargets = new Set(
    (graph?.edges ?? [])
      .filter(e => e.to.startsWith(data.id + '.'))
      .map(e => e.to.slice(data.id.length + 1)),
  );
  const inlineEditable = (p: Port) =>
    !!thisNode && p.name !== 'trigger' && !p.name.endsWith('.trigger') && isPrimitive(p.typeTag);
  const inlineValue = (portName: string) =>
    inlineAtTopLevel(portName) ? nodeCfg[portName] : defaultsCfg[portName];

  function commitInlineValue(portName: string, raw: string, tag: string) {
    if (!thisNode) return;
    let parsed: unknown = raw;
    if (tag === 'flowboard::Bool')      parsed = raw === 'true' || raw === '1';
    else if (INT_TAGS.has(tag))            parsed = raw === '' ? 0 : Math.trunc(Number(raw));
    else if (NUMBER_TAGS.has(tag))         parsed = raw === '' ? 0 : Number(raw);
    // string / char fall through as-is
    if (inlineAtTopLevel(portName)) update(data.id, { ...nodeCfg, [portName]: parsed });
    else update(data.id, { ...nodeCfg, defaults: { ...defaultsCfg, [portName]: parsed } });
  }

  return (
    <div
      className={[
        'rounded border min-w-[200px] text-xs bg-slate-800 text-slate-100',
        selected ? 'border-sky-500 ring-1 ring-sky-500' : 'border-slate-600',
      ].join(' ')}
    >
      <div className="px-2 py-1 border-b border-slate-700 bg-slate-900 rounded-t flex items-start justify-between gap-2">
        <div>
          <div className="font-semibold">{data.id}</div>
          <div className="text-slate-400 text-[10px]">{data.type}</div>
        </div>
        {isMachine && (
          <button
            type="button"
            className="nodrag shrink-0 mt-0.5 px-1.5 py-0.5 rounded border border-slate-600 text-[10px] text-sky-300 hover:bg-slate-700"
            onClick={() => openMachine(data.id)}
            onMouseDown={e => e.stopPropagation()}
            title="Open the state machine's inner canvas"
          >
            ⤢ Open
          </button>
        )}
        {isGroup && (
          <button
            type="button"
            className="nodrag shrink-0 mt-0.5 px-1.5 py-0.5 rounded border border-slate-600 text-[10px] text-sky-300 hover:bg-slate-700"
            onClick={() => openGroup(data.id)}
            onMouseDown={e => e.stopPropagation()}
            title="Open the group's inner canvas"
          >
            ⤢ Open
          </button>
        )}
      </div>

      <div className="py-1">
        {inputGroups.map(g => {
          const key  = 'in:' + g.op;
          const open = !g.op || expandedGroups.has(key);
          return (
            <div
              key={'ing-' + (g.op || '_')}
              className={g.op ? 'border-l-2 border-slate-700 ml-0.5 my-1' : ''}
            >
              {g.op && (
                <button
                  type="button"
                  className="nodrag flex items-center w-full px-2 pt-0.5 pb-0.5 text-[10px] uppercase tracking-wide text-slate-500 italic hover:text-slate-200 select-none"
                  onClick={() => toggleGroup(key)}
                  onMouseDown={e => e.stopPropagation()}
                  title={open ? 'Collapse' : 'Expand'}
                >
                  <span className="w-3 text-slate-400">{open ? '▾' : '▸'}</span>
                  <span className="flex-1 text-left">{g.op}</span>
                  {!open && (
                    <span className="text-slate-600 normal-case">{g.ports.length}</span>
                  )}
                </button>
              )}
              {g.ports.map(p => {
                const editable = inlineEditable(p);
                const connected = connectedTargets.has(p.name);
                if (!open) {
                  // Collapsed: keep the Handle in the DOM so existing edges
                  // still find a valid endpoint, but hide the row body and
                  // dot. The handles stack just under the group header — an
                  // edge into a collapsed group visually terminates there.
                  return (
                    <div key={'in-' + p.name} className="relative h-0 overflow-hidden">
                      <Handle
                        type="target"
                        position={Position.Left}
                        id={p.name}
                        style={{ ...dot(p.typeTag), left: -5, top: 0, visibility: 'hidden' }}
                      />
                    </div>
                  );
                }
                return (
                  <div key={'in-' + p.name} className="relative pl-4 pr-2 py-0.5">
                    <Handle
                      type="target"
                      position={Position.Left}
                      id={p.name}
                      style={{ ...dot(p.typeTag), left: -5, top: editable ? 12 : '50%', transform: editable ? 'none' : 'translateY(-50%)' }}
                    />
                    <span
                      className={isTrigger(p) ? 'text-amber-300' : 'text-slate-300'}
                      title={p.name}
                    >
                      {isTrigger(p) ? '▶ ' : ''}{displayName(p.name, g.op)}
                    </span>
                    {editable && (
                      <FactoryFieldInput
                        typeTag={p.typeTag}
                        value={inlineValue(p.name)}
                        muted={connected}
                        onCommit={raw => commitInlineValue(p.name, raw, p.typeTag)}
                      />
                    )}
                    {editingGroup && !connected && !isTrigger(p) && (
                      <button
                        type="button"
                        className="nodrag absolute right-1 top-0.5 text-[10px] text-sky-400 hover:text-sky-200"
                        title="Expose as group input"
                        onMouseDown={e => e.stopPropagation()}
                        onClick={() => promotePort(data.id, p.name, 'input', p.typeTag)}
                      >⇥</button>
                    )}
                  </div>
                );
              })}
            </div>
          );
        })}
        {outputGroups.map(g => {
          const key  = 'out:' + g.op;
          const open = !g.op || expandedGroups.has(key);
          return (
            <div
              key={'outg-' + (g.op || '_')}
              className={g.op ? 'border-r-2 border-slate-700 mr-0.5 my-1' : ''}
            >
              {g.op && (
                <button
                  type="button"
                  className="nodrag flex items-center w-full px-2 pt-0.5 pb-0.5 text-[10px] uppercase tracking-wide text-slate-500 italic hover:text-slate-200 select-none"
                  onClick={() => toggleGroup(key)}
                  onMouseDown={e => e.stopPropagation()}
                  title={open ? 'Collapse' : 'Expand'}
                >
                  {!open && (
                    <span className="text-slate-600 normal-case mr-2">{g.ports.length}</span>
                  )}
                  <span className="flex-1 text-right">{g.op}</span>
                  <span className="w-3 text-right text-slate-400">{open ? '▾' : '▸'}</span>
                </button>
              )}
              {g.ports.map(p => {
                if (!open) {
                  return (
                    <div key={'out-' + p.name} className="relative h-0 overflow-hidden">
                      <Handle
                        type="source"
                        position={Position.Right}
                        id={p.name}
                        style={{ ...dot(p.typeTag), right: -5, top: 0, visibility: 'hidden' }}
                      />
                    </div>
                  );
                }
                const liveEntry = live[resolveTapKey(`${data.id}.${p.name}`, livePrefix, liveScope)];
                const stale = liveEntry !== undefined && Date.now() - liveEntry.ts > LIVE_STALE_MS;
                return (
                  <div key={'out-' + p.name} className="relative pl-2 pr-4 py-0.5 text-right">
                    {editingGroup && (
                      <button
                        type="button"
                        className="nodrag absolute left-1 top-0.5 text-[10px] text-sky-400 hover:text-sky-200"
                        title="Expose as group output"
                        onMouseDown={e => e.stopPropagation()}
                        onClick={() => promotePort(data.id, p.name, 'output', p.typeTag)}
                      >⇤</button>
                    )}
                    <span className="text-slate-300" title={p.name}>{displayName(p.name, g.op)}</span>
                    {liveEntry !== undefined && (
                      <span
                        className={
                          'ml-2 font-mono text-[10px] ' +
                          (stale ? 'text-slate-600' : 'text-emerald-300')
                        }
                        title={typeof liveEntry.value === 'string'
                          ? liveEntry.value
                          : JSON.stringify(liveEntry.value)}
                      >
                        {formatLiveValue(liveEntry.value)}
                      </span>
                    )}
                    <Handle
                      type="source"
                      position={Position.Right}
                      id={p.name}
                      style={{ ...dot(p.typeTag), right: -5, top: '50%', transform: 'translateY(-50%)' }}
                    />
                  </div>
                );
              })}
            </div>
          );
        })}
        {inputs.length === 0 && outputs.length === 0 && (
          <div className="px-2 py-1 text-slate-500 italic">(no ports)</div>
        )}
      </div>
    </div>
  );
}

function FactoryFieldInput(props: {
  typeTag: string;
  value: unknown;
  muted: boolean;       // true when an incoming edge is connected — the field still
                        //   acts as a default, but live values come from the edge
  onCommit: (raw: string) => void;
}) {
  const { typeTag, value, muted, onCommit } = props;
  const stringValue =
    value === undefined || value === null ? '' :
    typeof value === 'boolean' ? (value ? 'true' : 'false') :
    String(value);
  const baseCls =
    'mt-0.5 w-full bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 ' +
    'text-[11px] text-slate-100 outline-none focus:border-sky-600';
  const cls = muted ? `${baseCls} opacity-60 italic` : baseCls;
  const title = muted ? 'Default (overridden by connected input)' : 'Default value';

  // nodrag/nowheel keeps reactflow from dragging the node / scrolling the canvas
  // when the user interacts with the input.
  if (typeTag === 'flowboard::Bool') {
    return (
      <select
        className={`nodrag nowheel ${cls}`}
        value={stringValue || 'false'}
        title={title}
        onChange={e => onCommit(e.target.value)}
        onMouseDown={e => e.stopPropagation()}
      >
        <option value="false">false</option>
        <option value="true">true</option>
      </select>
    );
  }

  const inputType = NUMBER_TAGS.has(typeTag) ? 'number' : 'text';
  const step = (typeTag === 'flowboard::Double' || typeTag === 'flowboard::Float') ? 'any' : undefined;

  return (
    <input
      className={`nodrag nowheel ${cls}`}
      type={inputType}
      step={step}
      value={stringValue}
      title={title}
      placeholder={muted ? 'overridden by edge' : 'default'}
      onChange={e => onCommit(e.target.value)}
      onMouseDown={e => e.stopPropagation()}
    />
  );
}
