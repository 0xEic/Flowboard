// SPDX-License-Identifier: MIT
import { useState, useEffect } from 'react';
import { useGraphStore } from '../store/graph_store';
import { NodeForm } from './form/NodeForm';
import { TypeSelect } from './form/TypeSelect';
import { fromSavedFile } from '../lib/group_compile';
import { nodePorts } from '../lib/node_ports';
import { listElementType, expectedListInputElement, shortElem } from '../lib/list_types';
import type { GraphNodeJson } from '../api/types';

// Connector pane: manage a group's exposed ports (its inner Group.Input /
// Group.Output terminal nodes). Shown in the Inspector while editing a group.
function ConnectorPane() {
  const graph      = useGraphStore(s => s.graph);
  const setGraph   = useGraphStore(s => s.setGraph);
  const update     = useGraphStore(s => s.updateNodeConfig);
  const removeNode = useGraphStore(s => s.removeNode);
  const addFromType = useGraphStore(s => s.addNodeFromType);
  if (!graph) return null;

  const terms = graph.nodes.filter(n => n.type === 'Group.Input' || n.type === 'Group.Output');

  // Reorder a terminal within graph.nodes (drives the GroupNode handle order).
  const move = (id: string, dir: -1 | 1) => {
    const nodes = [...graph.nodes];
    const i = nodes.findIndex(n => n.id === id);
    const j = i + dir;
    if (i < 0 || j < 0 || j >= nodes.length) return;
    [nodes[i], nodes[j]] = [nodes[j], nodes[i]];
    setGraph({ ...graph, nodes });
  };

  const row = (t: GraphNodeJson) => {
    const cfg = (t.config ?? {}) as { portName?: string; typeTag?: string };
    const isIn = t.type === 'Group.Input';
    return (
      <div key={t.id} className="border border-slate-700 rounded p-1.5 space-y-1 bg-slate-900/40">
        <div className="flex items-center gap-1">
          <span className={`text-[10px] ${isIn ? 'text-emerald-300' : 'text-sky-300'}`}>{isIn ? 'IN' : 'OUT'}</span>
          <input
            className="flex-1 bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-[11px] outline-none focus:border-sky-600"
            value={cfg.portName ?? ''}
            placeholder="port name"
            onChange={e => update(t.id, { ...cfg, portName: e.target.value })}
            spellCheck={false}
          />
          <button className="text-slate-500 hover:text-slate-200 px-1" title="Move up"   onClick={() => move(t.id, -1)}>▲</button>
          <button className="text-slate-500 hover:text-slate-200 px-1" title="Move down" onClick={() => move(t.id, 1)}>▼</button>
          <button className="text-rose-400 hover:text-rose-300 px-1"  title="Delete"     onClick={() => removeNode(t.id)}>✕</button>
        </div>
        <TypeSelect value={cfg.typeTag ?? 'flowboard::Double'} onChange={tt => update(t.id, { ...cfg, typeTag: tt })} />
      </div>
    );
  };

  return (
    <div className="mt-2 border-t border-slate-700 pt-2">
      <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Group ports</div>
      <div className="flex flex-col gap-1.5">
        {terms.length === 0 && (
          <div className="text-slate-500 text-[11px]">
            No exposed ports yet. Add one below, drop a Group.Input/Output node, or
            click ⇥/⇤ on an inner node's port.
          </div>
        )}
        {terms.map(row)}
      </div>
      <div className="flex gap-2 mt-2">
        <button
          className="px-2 py-1 text-xs rounded bg-slate-700 hover:bg-slate-600"
          onClick={() => addFromType('Group.Input', { x: 40, y: 40 + terms.length * 70 }, { portName: '', typeTag: 'flowboard::Double' })}
        >+ Input</button>
        <button
          className="px-2 py-1 text-xs rounded bg-slate-700 hover:bg-slate-600"
          onClick={() => addFromType('Group.Output', { x: 360, y: 40 + terms.length * 70 }, { portName: '', typeTag: 'flowboard::Double' })}
        >+ Output</button>
      </div>
    </div>
  );
}

function GraphInspector() {
  const graph    = useGraphStore(s => s.graph);
  const setGraph = useGraphStore(s => s.setGraph);
  const editingGroup = useGraphStore(s => s.groupStack.length > 0);
  const [showJson, setShowJson] = useState(false);
  const [text, setText] = useState('');
  const [err,  setErr]  = useState<string | null>(null);

  // Load the editor text from the graph whenever the JSON view is opened.
  useEffect(() => {
    if (showJson && graph) setText(JSON.stringify(graph, null, 2));
  }, [showJson, graph]);

  if (!graph) {
    return (
      <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm">
        No graph loaded.
      </aside>
    );
  }

  return (
    <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm flex flex-col gap-2">
      <div className="font-semibold">Inspector</div>
      <div className="text-xs text-slate-400">
        Select a node on the canvas to edit its properties.
      </div>
      <div className="text-xs text-slate-500">
        {graph.nodes.length} node{graph.nodes.length === 1 ? '' : 's'} ·{' '}
        {graph.edges.length} edge{graph.edges.length === 1 ? '' : 's'}
      </div>

      {editingGroup && <ConnectorPane />}

      <button
        className="self-start mt-2 px-3 py-1 text-xs rounded bg-slate-700 hover:bg-slate-600"
        onClick={() => setShowJson(s => !s)}
      >
        {showJson ? 'Hide JSON' : 'Show JSON'}
      </button>

      {showJson && (
        <>
          <textarea
            className="flex-1 bg-slate-900 text-slate-100 font-mono text-xs p-2 rounded border border-slate-700 outline-none focus:border-sky-600"
            value={text}
            onChange={e => { setText(e.target.value); setErr(null); }}
            spellCheck={false}
          />
          <div className="flex items-center gap-2">
            <button
              className="px-3 py-1 bg-sky-700 hover:bg-sky-600 rounded"
              onClick={() => {
                try { setGraph(fromSavedFile(JSON.parse(text))); setErr(null); }
                catch (ex) { setErr(ex instanceof Error ? ex.message : String(ex)); }
              }}
            >Apply (local)</button>
            <button
              className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded"
              onClick={() => setText(JSON.stringify(graph, null, 2))}
            >Reset</button>
            {err && <span className="text-amber-400 text-xs">{err}</span>}
          </div>
        </>
      )}
    </aside>
  );
}

function MultiSelectionPanel({ ids }: { ids: string[] }) {
  const graph          = useGraphStore(s => s.graph);
  const select         = useGraphStore(s => s.select);
  const setSelectedIds = useGraphStore(s => s.setSelectedIds);
  const nodes  = (graph?.nodes ?? []).filter(n => ids.includes(n.id));

  return (
    <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm flex flex-col gap-2 overflow-y-auto">
      <div className="font-semibold">{ids.length} nodes selected</div>
      <div className="text-xs text-slate-500">
        Click a node to edit it; Ctrl/⌘ or Shift-click removes it from the selection.
        Ctrl/⌘+C copies the selection; Ctrl/⌘+V pastes (edges between selected nodes are kept).
      </div>
      <div className="flex flex-col gap-1 mt-1">
        {nodes.map(n => (
          <button
            key={n.id}
            className="text-left px-2 py-1 rounded bg-slate-900/40 hover:bg-slate-700 border border-slate-700/60"
            onClick={e => {
              if (e.ctrlKey || e.metaKey || e.shiftKey) {
                setSelectedIds(ids.filter(i => i !== n.id));  // deselect this node
              } else {
                select(n.id);  // focus just this node
              }
            }}
            title="Click to edit · Ctrl/Shift-click to remove from selection"
          >
            <div className="text-slate-100 truncate">{n.id}</div>
            <div className="text-slate-400 text-[10px]">{n.type}</div>
          </button>
        ))}
      </div>
    </aside>
  );
}

// Endpoint = "nodeId.portName". Match GraphCanvas's split convention: the node
// id is the first segment, the handle is everything after the first dot.
function splitEndpoint(endpoint: string): { id: string; port: string } {
  const [id, ...rest] = endpoint.split('.');
  return { id, port: rest.join('.') };
}

function EdgePanel({ edge }: { edge: { from: string; to: string } }) {
  const graph   = useGraphStore(s => s.graph);
  const catalog = useGraphStore(s => s.catalog);

  const src = splitEndpoint(edge.from);
  const tgt = splitEndpoint(edge.to);
  const sNode = graph?.nodes.find(n => n.id === src.id);
  const tNode = graph?.nodes.find(n => n.id === tgt.id);
  const srcType = sNode
    ? nodePorts(sNode, catalog).outputs.find(p => p.name === src.port)?.typeTag
    : undefined;
  const tgtType = tNode
    ? nodePorts(tNode, catalog).inputs.find(p => p.name === tgt.port)?.typeTag
    : undefined;
  const mismatch = !!srcType && !!tgtType && srcType !== tgtType;

  // For list↔list links, surface the inferred element types and flag a soft
  // mismatch (only Combine requires its two list inputs to agree).
  const bothLists = srcType === 'flowboard::List' && tgtType === 'flowboard::List';
  const srcElem = bothLists && graph ? listElementType(graph, src.id, src.port) : '';
  const tgtElem = bothLists && graph ? expectedListInputElement(graph, tgt.id, tgt.port) : '';
  const listMismatch = !!srcElem && !!tgtElem && srcElem !== tgtElem;

  const endpointRow = (
    label: string, id: string, port: string, type: string | undefined, color: string,
  ) => (
    <div className="bg-slate-900 border border-slate-700 rounded p-2">
      <div className="text-slate-500 text-[10px] uppercase tracking-wide mb-1">{label}</div>
      <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 text-xs">
        <span className="text-slate-400">node</span>
        <span className="text-slate-200 truncate" title={id}>{id}</span>
        <span className="text-slate-400">port</span>
        <span className="text-slate-200 truncate" title={port}>{port || '(default)'}</span>
        <span className="text-slate-400">type</span>
        <span className={`${color} font-mono truncate`} title={type ?? ''}>
          {type ?? '(unknown)'}
        </span>
      </div>
    </div>
  );

  return (
    <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm flex flex-col gap-2 overflow-y-auto">
      <div className="font-semibold">Link</div>
      <div className="flex flex-col gap-2">
        {endpointRow('Output (source)', src.id, src.port, srcType, 'text-emerald-400')}
        <div className="text-center text-slate-500 text-xs">↓</div>
        {endpointRow('Input (target)', tgt.id, tgt.port, tgtType, 'text-sky-400')}
      </div>
      <div className={`text-xs ${mismatch ? 'text-amber-400' : 'text-slate-400'}`}>
        {mismatch
          ? `Type mismatch: ${srcType} → ${tgtType}`
          : srcType && tgtType
            ? 'Types match.'
            : 'Type could not be resolved.'}
      </div>
      {bothLists && (srcElem || tgtElem) && (
        <div className={`text-xs ${listMismatch ? 'text-amber-400' : 'text-slate-400'}`}>
          {listMismatch
            ? `List element mismatch: ${shortElem(srcElem)} ≠ ${shortElem(tgtElem)}`
            : `List element: ${shortElem(srcElem || tgtElem)}`}
        </div>
      )}
    </aside>
  );
}

export function Inspector() {
  const selectedNodeIds = useGraphStore(s => s.selectedNodeIds);
  const selectedEdge    = useGraphStore(s => s.selectedEdge);
  if (selectedNodeIds.length > 1) return <MultiSelectionPanel ids={selectedNodeIds} />;
  // key by node id so each selected node gets its own NodeForm instance — the
  // id-edit draft state can never bleed onto the next selected node.
  if (selectedNodeIds.length === 1)
    return <NodeForm key={selectedNodeIds[0]} nodeId={selectedNodeIds[0]} />;
  if (selectedEdge) return <EdgePanel edge={selectedEdge} />;
  return <GraphInspector />;
}
