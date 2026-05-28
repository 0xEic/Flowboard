// SPDX-License-Identifier: MIT
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ReactFlow, {
  Background, Controls, ReactFlowProvider,
  Handle, Position, ConnectionMode, MarkerType, applyNodeChanges,
  type Node as RfNode, type Edge as RfEdge, type EdgeTypes,
  type Connection, type IsValidConnection, type NodeProps,
  type OnSelectionChangeParams, type OnNodesChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
import { useGraphStore } from '../store/graph_store';
import { FloatingEdge, FloatingConnectionLine } from './canvas/floating_edge';
import type { InnerNode, InnerStateNode, InnerTransitionNode } from '../lib/machine_compile';

// Connection points on all four sides. With ConnectionMode.Loose each acts as
// both source and target, so links can start/end on any side; the floating edge
// then draws a straight line between the true border points.
const HANDLE_SIDES: ReadonlyArray<{ id: string; position: Position }> = [
  { id: 't', position: Position.Top },
  { id: 'r', position: Position.Right },
  { id: 'b', position: Position.Bottom },
  { id: 'l', position: Position.Left },
];

function BorderHandles({ color }: { color: string }) {
  return (
    <>
      {HANDLE_SIDES.map(s => (
        <Handle
          key={s.id} id={s.id} type="source" position={s.position}
          style={{ width: 8, height: 8, background: color, border: '1px solid #0f172a' }}
        />
      ))}
    </>
  );
}

// ---- inner node views -------------------------------------------------------

// Reads its node live from the store by id (not from `data`) so editing the
// name doesn't churn the controlled node array — which would remount the input
// and drop focus after the first keystroke.
function StateNodeView({ id, data, selected }: NodeProps<{ machineId: string }>) {
  const machineId = data.machineId;
  const node = useGraphStore(s => s.innerCanvas?.nodes.find(n => n.id === id));
  const update = useGraphStore(s => s.updateInnerState);
  const setInitial = useGraphStore(s => s.setInitialState);
  const live = useGraphStore(s => s.live);
  if (!node || node.kind !== 'state') return null;
  const st = node as InnerStateNode;
  const active = live[`${machineId}.active.${st.name}`]?.value === true;

  return (
    <div
      className={[
        'rounded-lg border px-3 py-2 min-w-[140px] text-xs',
        active ? 'bg-emerald-700 border-emerald-400 text-white'
               : 'bg-slate-800 text-slate-100',
        selected ? 'ring-1 ring-sky-500 border-sky-500'
                 : active ? '' : 'border-slate-600',
      ].join(' ')}
    >
      <BorderHandles color="#38bdf8" />
      <div className="flex items-center gap-1">
        <input
          className="nodrag nowheel flex-1 bg-transparent border-b border-slate-600 focus:border-sky-500 outline-none font-semibold"
          value={st.name}
          onChange={e => update(id, { name: e.target.value })}
          onMouseDown={e => e.stopPropagation()}
          title="State name"
        />
      </div>
      <label
        className="nodrag mt-1 flex items-center gap-1 text-[10px] text-slate-300 cursor-pointer"
        onMouseDown={e => e.stopPropagation()}
      >
        <input
          type="radio"
          checked={st.initial}
          onChange={() => setInitial(id)}
          title="Mark as the initial state"
        />
        initial
      </label>
    </div>
  );
}

function TransitionNodeView({ id, selected }: NodeProps) {
  const node = useGraphStore(s => s.innerCanvas?.nodes.find(n => n.id === id));
  const update = useGraphStore(s => s.updateInnerTransition);
  if (!node || node.kind !== 'transition') return null;
  const tr = node as InnerTransitionNode;
  return (
    <div
      className={[
        'rounded border px-2 py-1 min-w-[110px] text-[11px] bg-slate-900 text-amber-200',
        selected ? 'ring-1 ring-sky-500 border-sky-500' : 'border-amber-700/60',
      ].join(' ')}
    >
      <BorderHandles color="#f59e0b" />
      <div className="text-[9px] uppercase tracking-wide text-amber-500/70 leading-none mb-0.5">
        trigger
      </div>
      <input
        className="nodrag nowheel w-full bg-transparent border-b border-amber-700/50 focus:border-amber-400 outline-none"
        value={tr.trigger}
        onChange={e => update(id, { trigger: e.target.value })}
        onMouseDown={e => e.stopPropagation()}
        title="Boolean trigger port name"
      />
    </div>
  );
}

const NODE_TYPES = { state: StateNodeView, transition: TransitionNodeView };
const EDGE_TYPES: EdgeTypes = { floating: FloatingEdge };

const EDGE_STROKE = '#94a3b8';
const EDGE_SEL_STROKE = '#38bdf8';

// ---- inner canvas -----------------------------------------------------------

export function MachineCanvas() {
  return (
    <ReactFlowProvider>
      <MachineCanvasInner />
    </ReactFlowProvider>
  );
}

function MachineCanvasInner() {
  const machineId   = useGraphStore(s => s.editingMachineId);
  const innerCanvas = useGraphStore(s => s.innerCanvas);
  const innerError  = useGraphStore(s => s.innerError);
  const addState        = useGraphStore(s => s.addInnerState);
  const addTransition   = useGraphStore(s => s.addInnerTransition);
  const moveNodes       = useGraphStore(s => s.moveInnerNodes);
  const addEdge         = useGraphStore(s => s.addInnerEdge);
  const removeNodes     = useGraphStore(s => s.removeInnerNodes);
  const removeEdge      = useGraphStore(s => s.removeInnerEdge);
  const copySelection   = useGraphStore(s => s.copyInnerSelection);
  const paste           = useGraphStore(s => s.pasteInner);
  const undo            = useGraphStore(s => s.undoInner);
  const redo            = useGraphStore(s => s.redoInner);

  // Selection tracked locally and reflected onto the (controlled, rebuilt)
  // nodes/edges so it survives canvas rebuilds.
  const [selNodeIds, setSelNodeIds] = useState<Set<string>>(new Set());
  const [selEdgeIds, setSelEdgeIds] = useState<Set<string>>(new Set());
  const selNodeIdsRef = useRef(selNodeIds);
  selNodeIdsRef.current = selNodeIds;

  const kindOf = useCallback((id: string): InnerNode['kind'] | undefined =>
    innerCanvas?.nodes.find(n => n.id === id)?.kind, [innerCanvas]);

  // Build the controlled node array from the store. `data` carries only the
  // machineId — the node views read their name/trigger live from the store by id
  // — so name/trigger edits never change this array (no remount, no focus loss).
  const buildNodes = useCallback((sel: Set<string>): RfNode[] => {
    if (!innerCanvas || !machineId) return [];
    return innerCanvas.nodes.map(n => ({
      id: n.id,
      type: n.kind,
      position: n.position,
      selected: sel.has(n.id),
      data: { machineId },
    }));
  }, [innerCanvas, machineId]);

  const [rfNodes, setRfNodes] = useState<RfNode[]>(() => buildNodes(selNodeIdsRef.current));

  // Structural signature: changes only when a node is added/removed/retyped or
  // moved — NOT when its name/trigger config is edited. Rebuilding on every
  // keystroke would remount the React Flow nodes and steal input focus.
  const innerSig = useMemo(
    () => innerCanvas
      ? innerCanvas.nodes.map(n => `${n.id}:${n.kind}:${n.position.x}:${n.position.y}`).join('|')
      : '',
    [innerCanvas],
  );

  // Rebuild on structural change (incl. positions persisted on drag stop) — but
  // not during a drag (deltas live in rfNodes via onNodesChange until stop) and
  // not on name/trigger edits (innerSig is unchanged then).
  useEffect(() => {
    setRfNodes(buildNodes(selNodeIdsRef.current));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [innerSig, machineId]);

  // Reflect programmatic selection (e.g. Ctrl+A select-all) onto the nodes.
  useEffect(() => {
    setRfNodes(curr => {
      let changed = false;
      const next = curr.map(n => {
        const want = selNodeIds.has(n.id);
        if (!!n.selected !== want) { changed = true; return { ...n, selected: want }; }
        return n;
      });
      return changed ? next : curr;
    });
  }, [selNodeIds]);

  // React Flow drives node drag + selection through this for controlled nodes;
  // without it the nodes can be neither moved (live) nor selected.
  const onNodesChange: OnNodesChange = useCallback(
    changes => setRfNodes(curr => applyNodeChanges(changes, curr)), []);

  const rfEdges = useMemo<RfEdge[]>(() => {
    if (!innerCanvas) return [];
    return innerCanvas.edges.map((e, i) => {
      const id = `ie${i}`;
      const selected = selEdgeIds.has(id);
      const stroke = selected ? EDGE_SEL_STROKE : EDGE_STROKE;
      return {
        id,
        source: e.source,
        target: e.target,
        // Endpoints float to the border via the FloatingEdge — no fixed handles.
        type: 'floating',
        animated: true,
        selected,
        markerEnd: { type: MarkerType.ArrowClosed, color: stroke, width: 16, height: 16 },
        style: { stroke, strokeWidth: selected ? 2.5 : 1.5 },
      };
    });
  }, [innerCanvas, selEdgeIds]);

  const onSelectionChange = useCallback((p: OnSelectionChangeParams) => {
    setSelNodeIds(new Set(p.nodes.map(n => n.id)));
    setSelEdgeIds(new Set(p.edges.map(e => e.id)));
  }, []);

  // Only state->transition and transition->state links are valid (alternating).
  // A transition may have MANY incoming state links (the trigger is checked in
  // each of those states) and exactly one outgoing state link.
  const isValidConnection: IsValidConnection = useCallback(c => {
    if (!c.source || !c.target || c.source === c.target) return false;
    const a = kindOf(c.source);
    const b = kindOf(c.target);
    return (a === 'state' && b === 'transition') || (a === 'transition' && b === 'state');
  }, [kindOf]);

  const onConnect = useCallback((c: Connection) => {
    if (!c.source || !c.target) return;
    addEdge(c.source, c.target);
  }, [addEdge]);

  const persist = useCallback((nodes: RfNode[]) => {
    moveNodes(nodes.map(n => ({ id: n.id, pos: { x: n.position.x, y: n.position.y } })));
  }, [moveNodes]);
  const onNodeDragStop = useCallback((_e: React.MouseEvent | React.TouchEvent, node: RfNode, nodes: RfNode[]) => {
    persist(nodes && nodes.length > 0 ? nodes : [node]);
  }, [persist]);
  const onSelectionDragStop = useCallback((_e: React.MouseEvent, nodes: RfNode[]) => {
    persist(nodes);
  }, [persist]);

  const onNodesDelete = useCallback((deleted: RfNode[]) => {
    removeNodes(deleted.map(n => n.id));  // one batched, undoable step
  }, [removeNodes]);

  const onEdgesDelete = useCallback((deleted: RfEdge[]) => {
    deleted.forEach(e => { if (e.source && e.target) removeEdge(e.source, e.target); });
  }, [removeEdge]);

  // Copy / paste / undo / redo / select-all (Ctrl/Cmd + C / V / Z / Y / A),
  // mirroring the main canvas but driving the machine's OWN history/clipboard.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey)) return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      if (e.key === 'c' || e.key === 'C') { copySelection([...selNodeIdsRef.current]); }
      else if (e.key === 'v' || e.key === 'V') { paste(); }
      else if (e.key === 'z' || e.key === 'Z') { e.preventDefault(); undo(); }
      else if (e.key === 'y' || e.key === 'Y') { e.preventDefault(); redo(); }
      else if (e.key === 'a' || e.key === 'A') {
        e.preventDefault();
        const ic = useGraphStore.getState().innerCanvas;
        setSelNodeIds(new Set(ic ? ic.nodes.map(n => n.id) : []));
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [copySelection, paste, undo, redo]);

  if (!machineId || !innerCanvas) return null;

  return (
    <div className="flex-1 bg-slate-900 relative">
      <div className="absolute z-20 top-2 left-2 flex gap-2">
        <button
          className="px-2 py-1 rounded bg-slate-800 border border-slate-600 text-xs text-slate-100 hover:bg-slate-700"
          onClick={() => addState({ x: 120, y: 100 + Math.random() * 80 })}
        >+ State</button>
        <button
          className="px-2 py-1 rounded bg-slate-800 border border-amber-700/60 text-xs text-amber-200 hover:bg-slate-700"
          onClick={() => addTransition({ x: 320, y: 100 + Math.random() * 80 })}
        >+ Transition</button>
      </div>

      {innerError && (
        <div className="absolute z-20 top-2 right-2 max-w-md px-3 py-1.5 rounded bg-red-900/80 border border-red-600 text-xs text-red-100">
          {innerError}
        </div>
      )}

      <ReactFlow
        nodes={rfNodes}
        edges={rfEdges}
        onNodesChange={onNodesChange}
        nodeTypes={NODE_TYPES}
        edgeTypes={EDGE_TYPES}
        connectionMode={ConnectionMode.Loose}
        connectionLineComponent={FloatingConnectionLine}
        onConnect={onConnect}
        isValidConnection={isValidConnection}
        onNodeDragStop={onNodeDragStop}
        onSelectionDragStop={onSelectionDragStop}
        onNodesDelete={onNodesDelete}
        onEdgesDelete={onEdgesDelete}
        onSelectionChange={onSelectionChange}
        deleteKeyCode={['Delete', 'Backspace']}
        selectionKeyCode="Shift"
        multiSelectionKeyCode={['Meta', 'Control']}
        fitView
      >
        <Background color="#334155" gap={20} />
        <Controls />
      </ReactFlow>
    </div>
  );
}
