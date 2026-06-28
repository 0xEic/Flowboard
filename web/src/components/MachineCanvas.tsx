// SPDX-License-Identifier: MIT
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ReactFlow, {
  Background, Controls, ReactFlowProvider, NodeResizer,
  Handle, Position, ConnectionMode, MarkerType, applyNodeChanges,
  type Node as RfNode, type Edge as RfEdge, type EdgeTypes,
  type Connection, type IsValidConnection, type NodeProps,
  type OnSelectionChangeParams, type OnNodesChange, type OnEdgesChange,
} from 'reactflow';
import 'reactflow/dist/style.css';
import { useGraphStore } from '../store/graph_store';
import { FloatingConnectionLine } from './canvas/floating_edge';
import { GraphMiniPreview, machineToPreview } from './canvas/GraphMiniPreview';
import { TriggerEdge, type TriggerEdgeData } from './canvas/trigger_edge';
import { MachineInspector } from './MachineInspector';
import type { InnerStateNode, InnerNoteNode } from '../lib/machine_compile';

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
  const toggleInitial = useGraphStore(s => s.toggleInitialState);
  const openSub = useGraphStore(s => s.openSubmachine);
  const live = useGraphStore(s => s.live);
  // The active-state output ports are dotted by the path of owning composite
  // states (e.g. fsm.active.On.Idle). When we're inside a submachine the canvas
  // states are bare local names, so prepend the drill-down path or the live
  // highlight never matches at depth.
  const stack = useGraphStore(s => s.machineStack);
  if (!node || node.kind !== 'state') return null;
  const st = node as InnerStateNode;
  const pathPrefix = stack.map(s => s.stateName).join('.');
  const fullName = pathPrefix ? `${pathPrefix}.${st.name}` : st.name;
  const active = live[`${machineId}.active.${fullName}`]?.value === true;

  return (
    <div
      className={[
        'relative w-full h-full rounded-lg border px-3 py-2 min-w-[140px] min-h-[56px] text-xs',
        active ? 'bg-emerald-700 border-emerald-400 text-white'
               : 'bg-slate-800 text-slate-100',
        selected ? 'ring-1 ring-sky-500 border-sky-500'
                 : active ? '' : 'border-slate-600',
      ].join(' ')}
    >
      <NodeResizer
        color="#38bdf8"
        isVisible={selected}
        minWidth={140}
        minHeight={56}
        handleStyle={{ width: 8, height: 8 }}
        onResizeEnd={(_e, p) => update(id, { width: Math.round(p.width), height: Math.round(p.height) })}
      />
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
      <div className="flex items-center justify-between gap-2">
        <label
          className="nodrag mt-1 flex w-fit items-center gap-1 text-[10px] text-slate-300 cursor-pointer"
          onMouseDown={e => e.stopPropagation()}
        >
          <input
            type="checkbox"
            checked={st.initial}
            onChange={() => toggleInitial(id)}
            title="Mark as an initial state (one per chain; many chains can run at once)"
          />
          initial
        </label>
        {st.machine && (
          <button
            className="nodrag mt-1 flex items-center gap-1 rounded border border-sky-700/60 px-1 text-[10px] text-sky-300 hover:bg-slate-700"
            onMouseDown={e => e.stopPropagation()}
            onClick={() => openSub(id)}
            title="Open this state's substate machine"
          >⤢ {st.machine.states?.length ?? 0}</button>
        )}
      </div>
      {st.machine && (st.machine.states?.length ?? 0) > 0 && (
        <div
          className="nodrag mt-2 -mx-3 -mb-2 border-t border-slate-700 bg-slate-900/40 cursor-pointer hover:bg-slate-900/70 rounded-b-lg overflow-hidden"
          onMouseDown={e => e.stopPropagation()}
          onClick={() => openSub(id)}
          title="Open this state's substate machine"
        >
          <GraphMiniPreview
            graph={machineToPreview(st.machine)}
            width={140}
            height={48}
            activeIds={new Set(
              (st.machine.states ?? [])
                .map(s => s.name)
                .filter(n => live[`${machineId}.active.${fullName}.${n}`]?.value === true),
            )}
          />
        </div>
      )}
    </div>
  );
}

// Canvas-only annotation on the inner state-machine canvas. Editable inline
// (reads its text live from the store by id so typing doesn't churn the
// controlled node array), resizable, and ignored by the engine.
function InnerNoteView({ id, selected }: NodeProps<{ machineId: string }>) {
  const node = useGraphStore(s => s.innerCanvas?.nodes.find(n => n.id === id));
  const updateNote = useGraphStore(s => s.updateInnerNote);
  if (!node || node.kind !== 'note') return null;
  const nt = node as InnerNoteNode;
  return (
    <div
      className={[
        'relative w-full h-full flex flex-col rounded text-xs bg-amber-100/90 text-slate-800 border',
        selected ? 'border-amber-500 ring-1 ring-amber-500' : 'border-amber-300/70',
      ].join(' ')}
    >
      <NodeResizer
        color="#f59e0b"
        isVisible={selected}
        minWidth={120}
        minHeight={56}
        handleStyle={{ width: 8, height: 8 }}
        onResizeEnd={(_e, p) => updateNote(id, { width: Math.round(p.width), height: Math.round(p.height) })}
      />
      {/* Drag handle: the textarea below fills the body and is `nodrag`, so this
          strip is where you grab the note to move (or click to select) it. */}
      <div
        className="shrink-0 flex items-center justify-center h-4 cursor-move rounded-t bg-amber-200/70 text-amber-700/60"
        title="Drag to move"
      >
        <span className="text-[9px] leading-none tracking-[0.25em] select-none">•••</span>
      </div>
      <textarea
        className="nodrag nowheel flex-1 w-full resize-none bg-transparent outline-none px-2 py-1 placeholder:text-amber-700/50 placeholder:italic"
        value={nt.text}
        placeholder="Note…"
        onChange={e => updateNote(id, { text: e.target.value })}
        onMouseDown={e => e.stopPropagation()}
        title="Canvas note (ignored by the engine)"
      />
    </div>
  );
}

const NODE_TYPES = { state: StateNodeView, note: InnerNoteView };
const EDGE_TYPES: EdgeTypes = { trigger: TriggerEdge };

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
  const machineDepth = useGraphStore(s => s.machineStack.length);
  const addState        = useGraphStore(s => s.addInnerState);
  const addNote         = useGraphStore(s => s.addInnerNote);
  const moveNodes       = useGraphStore(s => s.moveInnerNodes);
  const addEdge         = useGraphStore(s => s.addInnerEdge);
  const removeNodes     = useGraphStore(s => s.removeInnerNodes);
  const removeEdgeAt    = useGraphStore(s => s.removeInnerEdgeAt);
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

  // Drilling into / out of a substate swaps the whole inner canvas. Drop stale
  // selection so the inspector doesn't point at an id from the previous level.
  useEffect(() => {
    setSelNodeIds(new Set());
    setSelEdgeIds(new Set());
  }, [machineDepth, machineId]);

  const isState = useCallback((id: string): boolean =>
    innerCanvas?.nodes.some(n => n.id === id && n.kind === 'state') ?? false, [innerCanvas]);

  // Single-selection routed to the properties panel. A state wins only when it's
  // the sole selection (no edges), and likewise for a transition, so a mixed or
  // multi selection — or a lone Note — falls through to the machine summary.
  const onlySelectedNode = selNodeIds.size === 1 && selEdgeIds.size === 0
    ? [...selNodeIds][0] : null;
  const selectedStateId = onlySelectedNode && isState(onlySelectedNode) ? onlySelectedNode : null;
  const selEdgeId = selEdgeIds.size === 1 && selNodeIds.size === 0 ? [...selEdgeIds][0] : null;
  const selectedEdgeIndex = selEdgeId ? parseInt(selEdgeId.slice(2), 10) : null;

  // Build the controlled node array from the store. `data` carries only the
  // machineId — the node views read their name/trigger live from the store by id
  // — so name/trigger edits never change this array (no remount, no focus loss).
  const buildNodes = useCallback((sel: Set<string>): RfNode[] => {
    if (!innerCanvas || !machineId) return [];
    return innerCanvas.nodes.map(n => {
      const rf: RfNode = {
        id: n.id,
        type: n.kind,
        position: n.position,
        selected: sel.has(n.id),
        data: { machineId },
      };
      // Persisted size fills the node box so the NodeResizer has something to
      // grab; absent = auto-size to content.
      if (n.width != null && n.height != null) rf.style = { width: n.width, height: n.height };
      return rf;
    });
  }, [innerCanvas, machineId]);

  const [rfNodes, setRfNodes] = useState<RfNode[]>(() => buildNodes(selNodeIdsRef.current));

  // Structural signature: changes only when a node is added/removed/retyped or
  // moved — NOT when its name/trigger config is edited. Rebuilding on every
  // keystroke would remount the React Flow nodes and steal input focus.
  const innerSig = useMemo(
    () => innerCanvas
      ? innerCanvas.nodes.map(n => `${n.id}:${n.kind}:${n.position.x}:${n.position.y}:${n.width ?? 0}:${n.height ?? 0}`).join('|')
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

  const rfEdges = useMemo<RfEdge<TriggerEdgeData>[]>(() => {
    if (!innerCanvas) return [];
    return innerCanvas.edges.map((e, i) => {
      const id = `ie${i}`;
      const selected = selEdgeIds.has(id);
      const stroke = selected ? EDGE_SEL_STROKE : EDGE_STROKE;
      return {
        id,
        source: e.source,
        target: e.target,
        type: 'trigger',
        animated: false,           // path styling + label leader handled in TriggerEdge
        selected,
        data: { index: i, edge: e },
        markerEnd: { type: MarkerType.ArrowClosed, color: stroke, width: 16, height: 16 },
        style: { stroke, strokeWidth: selected ? 2.5 : 1.5 },
      };
    });
  }, [innerCanvas, selEdgeIds]);

  const onSelectionChange = useCallback((p: OnSelectionChangeParams) => {
    setSelNodeIds(new Set(p.nodes.map(n => n.id)));
    setSelEdgeIds(new Set(p.edges.map(e => e.id)));
  }, []);

  // Edges are controlled (built from selEdgeIds), so React Flow can't track
  // their selection on its own. Mirror select-changes here — crucially the
  // select:false that a pane click emits via resetSelectedElements — back into
  // selEdgeIds; rebuilding the edges then syncs `selected` into the RF store so
  // onSelectionChange settles too. Without this an edge stays selected after
  // clicking empty canvas (nodes don't, because onNodesChange already syncs).
  const onEdgesChange: OnEdgesChange = useCallback(changes => {
    const sel = changes.filter(c => c.type === 'select') as Array<{ id: string; selected: boolean }>;
    if (!sel.length) return;
    setSelEdgeIds(prev => {
      const next = new Set(prev);
      for (const c of sel) { if (c.selected) next.add(c.id); else next.delete(c.id); }
      return next;
    });
  }, []);

  // Both endpoints must be states. Self-connections (source === target) are
  // allowed -- a self-loop is a valid transition pattern.
  const isValidConnection: IsValidConnection = useCallback(c => {
    if (!c.source || !c.target) return false;
    return isState(c.source) && isState(c.target);
  }, [isState]);

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

  // Map React Flow edge ids `ie${i}` back to the canvas edge index. Process
  // deletions highest-index-first so earlier removals don't shift later indices.
  const onEdgesDelete = useCallback((deleted: RfEdge[]) => {
    const indices = deleted
      .map(e => parseInt(e.id.slice(2), 10))
      .filter(i => Number.isFinite(i))
      .sort((a, b) => b - a);
    indices.forEach(i => removeEdgeAt(i));
  }, [removeEdgeAt]);

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
    <>
    <div className="flex-1 bg-slate-900 relative">
      <div className="absolute z-20 top-2 left-2 flex gap-2">
        <button
          className="px-2 py-1 rounded bg-slate-800 border border-slate-600 text-xs text-slate-100 hover:bg-slate-700"
          onClick={() => addState({ x: 120, y: 100 + Math.random() * 80 })}
        >+ State</button>
        <button
          className="px-2 py-1 rounded bg-slate-800 border border-amber-600/70 text-xs text-amber-200 hover:bg-slate-700"
          onClick={() => addNote({ x: 120, y: 100 + Math.random() * 80 })}
        >+ Note</button>
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
        onEdgesChange={onEdgesChange}
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
        // Match the main canvas: Ctrl+drag on empty space = rubber-band select;
        // Ctrl or Shift + click = add/remove from the selection.
        selectionKeyCode="Control"
        multiSelectionKeyCode={['Control', 'Shift']}
        selectionOnDrag={false}
        fitView
      >
        <Background color="#334155" gap={20} />
        <Controls />
      </ReactFlow>
    </div>
    <MachineInspector
      selectedStateId={selectedStateId}
      selectedEdgeIndex={selectedEdgeIndex}
      selectionCount={selNodeIds.size + selEdgeIds.size}
    />
    </>
  );
}
