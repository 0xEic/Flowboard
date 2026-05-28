// SPDX-License-Identifier: MIT
import React, { useMemo, useCallback, useState, useEffect, useRef } from 'react';
import ReactFlow, {
  Background, Controls, MiniMap, ReactFlowProvider, useReactFlow,
  applyNodeChanges,
  type Node as RfNode, type Edge as RfEdge,
  type OnNodesChange, type OnEdgesChange, type Connection, type IsValidConnection,
  type OnConnectStartParams,
} from 'reactflow';
import 'reactflow/dist/style.css';
import { useGraphStore } from '../store/graph_store';
import { OnboardNodeView } from './canvas/OnboardNodeView';
import { nodePorts } from '../lib/node_ports';
import { listElementType, expectedListInputElement, shortElem } from '../lib/list_types';
import { autoLayout } from '../lib/auto_layout';
import type { GraphJson } from '../api/types';
import {
  typeChangeForConnect, extraCandidates, candidateConfig,
} from '../lib/type_compat';

const NODE_TYPES = { onboard: OnboardNodeView };

const EDGE_OK_STROKE     = '#64748b';
const EDGE_BAD_STROKE    = '#ef4444';
// "Signal" edge: any-typed output -> Bool input. Carries no data — fires a true
// pulse on every emission. Rendered dashed + violet to distinguish from data links.
const EDGE_SIGNAL_STROKE = '#c084fc';
// Soft warning (amber): two list ports with known, differing element types.
// Lists share one opaque tag so this isn't a hard type error — the engine runs
// it — but it's almost always a mistake (e.g. concatenating lists of different
// structs), so it's flagged distinctly from a red scalar mismatch.
const EDGE_LIST_WARN_STROKE = '#f59e0b';
// Brighter, thicker strokes for the selected edge so it stands out (keeps the
// red tint when the selected edge is also a type mismatch).
const EDGE_SEL_STROKE     = '#38bdf8';
const EDGE_SEL_BAD_STROKE = '#f87171';

// Resizable nodes get an explicit canvas size (stored per-node, else this
// default). Other node types auto-size to their content.
const RESIZABLE_DEFAULT_SIZE: Record<string, { width: number; height: number }> = {
  'Debug.TriggerButton': { width: 200, height: 96 },
  'Debug.ValueDisplay':  { width: 220, height: 150 },
  'Debug.GraphDisplay':  { width: 252, height: 168 },
  'Note':                { width: 220, height: 120 },
};

// Build the React Flow node array from the graph. `data` deliberately carries
// only id+type — the node views read their config live from the store — so this
// only needs to run when node identity/type/position/size changes, never on a
// config edit.
function buildRfNodes(graph: GraphJson | null, selectedIds: string[]): RfNode[] {
  if (!graph) return [];
  const sel = new Set(selectedIds);
  return graph.nodes.map((n, i) => {
    const node: RfNode = {
      id: n.id,
      type: 'onboard',
      data: { id: n.id, type: n.type },
      position: n.position ?? { x: 80 + i * 260, y: 120 },
      selected: sel.has(n.id),
    };
    // Resizable Debug.* nodes carry an explicit size (persisted, else default)
    // so the node box fills to it and the NodeResizer has something to grab.
    const def = RESIZABLE_DEFAULT_SIZE[n.type];
    if (def) node.style = { width: n.width ?? def.width, height: n.height ?? def.height };
    return node;
  });
}

export function GraphCanvas() {
  return (
    <ReactFlowProvider>
      <GraphCanvasInner />
    </ReactFlowProvider>
  );
}

function GraphCanvasInner() {
  const graph             = useGraphStore(s => s.graph);
  const catalog           = useGraphStore(s => s.catalog);
  const selectedNodeIds   = useGraphStore(s => s.selectedNodeIds);
  const setSelectedIds    = useGraphStore(s => s.setSelectedIds);
  const selectEdge        = useGraphStore(s => s.selectEdge);
  const setPositions      = useGraphStore(s => s.updateNodePositions);
  const addFromType       = useGraphStore(s => s.addNodeFromType);
  const undo              = useGraphStore(s => s.undo);
  const redo              = useGraphStore(s => s.redo);
  const reload            = useGraphStore(s => s.reload);
  const addEdgeAction     = useGraphStore(s => s.addEdge);
  const removeNodeAction  = useGraphStore(s => s.removeNode);
  const removeEdgeAction  = useGraphStore(s => s.removeEdge);
  const updateNodeConfig  = useGraphStore(s => s.updateNodeConfig);
  const copySelection     = useGraphStore(s => s.copySelection);
  const paste             = useGraphStore(s => s.paste);
  const fitViewNonce      = useGraphStore(s => s.fitViewNonce);
  const autoArrangeNonce  = useGraphStore(s => s.autoArrangeNonce);
  const { screenToFlowPosition, setCenter, getZoom, fitView, getNodes } = useReactFlow();

  // Nonces only act on an actual increment. This component unmounts whenever a
  // state-machine inner canvas opens (App swaps GraphCanvas for MachineCanvas)
  // and remounts on close; a plain `nonce !== 0` guard would re-fire on every
  // remount, spontaneously re-running auto-arrange/fit on the main graph. The
  // refs init to the current nonce so a remount with an unchanged nonce is a
  // no-op, while a real toolbar bump still differs and runs.
  const lastFitNonce     = useRef(fitViewNonce);
  const lastArrangeNonce = useRef(autoArrangeNonce);

  // Fit the view when something requests it (e.g. after auto-arrange). Deferred a
  // frame so React Flow has applied the new node positions first.
  useEffect(() => {
    if (fitViewNonce === lastFitNonce.current) return;
    lastFitNonce.current = fitViewNonce;
    if (fitViewNonce === 0) return;
    const r = requestAnimationFrame(() => fitView({ padding: 0.2, duration: 400 }));
    return () => cancelAnimationFrame(r);
  }, [fitViewNonce, fitView]);

  // Auto-arrange here (not in the toolbar) so the layout can use React Flow's
  // real measured node sizes — onboard nodes group ports into collapsible
  // sections, so their on-screen size can't be derived from the port count.
  useEffect(() => {
    if (autoArrangeNonce === lastArrangeNonce.current) return;
    lastArrangeNonce.current = autoArrangeNonce;
    if (autoArrangeNonce === 0) return;
    const st = useGraphStore.getState();
    const g = st.graph;
    if (!g || g.nodes.length === 0) return;
    const measured = new Map<string, { w: number; h: number }>();
    for (const n of getNodes()) {
      const w = n.width ?? 0, h = n.height ?? 0;
      if (w > 0 && h > 0) measured.set(n.id, { w, h });
    }
    st.updateNodePositions(autoLayout(g, st.catalog, measured));
    st.requestFitView();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [autoArrangeNonce, getNodes]);

  // Keep the latest selection available without making it a dep of the node
  // rebuild — rebuilding the controlled `nodes` array mid-selection leaves the
  // React Flow rubber-band rectangle stuck on screen.
  const selectedIdsRef = useRef<string[]>(selectedNodeIds);
  selectedIdsRef.current = selectedNodeIds;

  // Structural signature of the graph: changes only when a node's identity,
  // type, position or size changes (or nodes are added/removed/reordered) — NOT
  // when a node's config is edited. Config edits update `graph` on every
  // keystroke, but the React Flow node array doesn't depend on config (the node
  // views read it live from the store), so we must NOT rebuild the array then:
  // replacing the controlled `nodes` array makes React Flow remount/refocus its
  // nodes, which steals focus from the input being typed into (the "one char at
  // a time" bug, incl. on-canvas inputs like group-terminal port names).
  const nodeSig = useMemo(
    () => graph
      ? graph.nodes
          .map(n => `${n.id}${n.type}${n.position?.x ?? 0}${n.position?.y ?? 0}${n.width ?? 0}${n.height ?? 0}`)
          .join('')
      : '',
    [graph],
  );

  const [rfNodes, setRfNodes] = useState<RfNode[]>(() => buildRfNodes(graph, selectedIdsRef.current));
  // Rebuild only when the structural signature changes. `graph` is read fresh
  // (it updated in the same render that changed nodeSig); depending on nodeSig
  // rather than graph is what skips config-only rebuilds.
  useEffect(() => {
    setRfNodes(buildRfNodes(graph, selectedIdsRef.current));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [nodeSig]);

  // Reflect programmatic selection changes (panel click, paste, add-node) onto
  // the nodes' `selected` flags. The guard makes this a no-op when the change
  // originated from React Flow itself (rfNodes already match), so it never
  // replaces the array during a rubber-band drag.
  useEffect(() => {
    setRfNodes(curr => {
      const sel = new Set(selectedNodeIds);
      let changed = false;
      const next = curr.map(n => {
        const want = sel.has(n.id);
        if (!!n.selected !== want) { changed = true; return { ...n, selected: want }; }
        return n;
      });
      return changed ? next : curr;
    });
  }, [selectedNodeIds]);

  // Edges are controlled + rebuilt from the graph, so React Flow can't keep its
  // own selection state across rebuilds — track it here and reflect it onto the
  // rebuilt edges (both as `selected` for delete/keyboard and as a bright style).
  const [selectedEdgeIds, setSelectedEdgeIds] = useState<Set<string>>(new Set());

  const edges = useMemo<RfEdge[]>(() => {
    if (!graph) return [];
    return graph.edges.map((e, i) => {
      const [src, ...srcRest] = e.from.split('.');
      const [tgt, ...tgtRest] = e.to.split('.');
      const sourceHandle = srcRest.join('.');
      const targetHandle = tgtRest.join('.');

      const sNode = graph.nodes.find(n => n.id === src);
      const tNode = graph.nodes.find(n => n.id === tgt);
      let mismatch = true;  // assume bad unless we can confirm a tag-tag match
      let signal = false;   // any-typed output -> Bool input (trigger pulse)
      let listWarn = false; // list ports with known, differing element types
      let label: string | undefined;
      if (sNode && tNode) {
        const sPorts = nodePorts(sNode, catalog);
        const tPorts = nodePorts(tNode, catalog);
        const sPort = sPorts.outputs.find(p => p.name === sourceHandle);
        const tPort = tPorts.inputs .find(p => p.name === targetHandle);
        if (sPort && tPort) {
          if (sPort.typeTag !== tPort.typeTag && tPort.typeTag === 'flowboard::Bool') {
            // Legit signal edge: the engine fires a `true` pulse per emission.
            signal = true; mismatch = false; label = 'signal';
          } else {
            mismatch = sPort.typeTag !== tPort.typeTag;
            if (mismatch) {
              label = `${sPort.typeTag} → ${tPort.typeTag}`;
            } else if (sPort.typeTag === 'flowboard::List') {
              // Tags match (both lists) but the element types may not. Infer the
              // source's element type and what the target requires (only Combine
              // constrains it); flag a soft mismatch when both are known + differ.
              const srcElem = listElementType(graph, src, sourceHandle);
              const tgtElem = expectedListInputElement(graph, tgt, targetHandle);
              if (srcElem && tgtElem && srcElem !== tgtElem) {
                listWarn = true;
                label = `list: ${shortElem(srcElem)} ≠ ${shortElem(tgtElem)}`;
              }
            }
          }
        } else {
          label = 'port not found';
        }
      } else {
        label = 'node missing';
      }

      const id = `e${i}`;
      const selected = selectedEdgeIds.has(id);
      const stroke = signal
        ? (selected ? EDGE_SEL_STROKE : EDGE_SIGNAL_STROKE)
        : selected
          ? (mismatch ? EDGE_SEL_BAD_STROKE : EDGE_SEL_STROKE)
          : (mismatch ? EDGE_BAD_STROKE : listWarn ? EDGE_LIST_WARN_STROKE : EDGE_OK_STROKE);
      return {
        id,
        source: src,
        target: tgt,
        sourceHandle,
        targetHandle,
        animated: true,
        selected,
        label,
        style: {
          stroke,
          strokeWidth: selected ? 3 : (mismatch || signal || listWarn ? 2 : 1),
          ...(signal ? { strokeDasharray: '6 4' } : {}),
          ...(listWarn ? { strokeDasharray: '2 3' } : {}),
        },
        labelStyle: {
          fill: mismatch ? '#fca5a5' : signal ? EDGE_SIGNAL_STROKE
              : listWarn ? EDGE_LIST_WARN_STROKE : '#94a3b8',
          fontSize: 10,
        },
        labelBgStyle: { fill: '#0f172a' },
      };
    });
  }, [graph, catalog, selectedEdgeIds]);

  const onEdgesChange: OnEdgesChange = useCallback(changes => {
    const sel = changes.filter(c => c.type === 'select') as Array<{ id: string; selected: boolean }>;
    if (!sel.length) return;
    setSelectedEdgeIds(prev => {
      const next = new Set(prev);
      for (const c of sel) { if (c.selected) next.add(c.id); else next.delete(c.id); }
      // Mirror the single selected edge into the store so the Inspector can show
      // its endpoint types. Edge ids are `e<index>` into graph.edges.
      if (next.size === 1) {
        const [only] = [...next];
        const idx = Number(only.slice(1));
        const e = graph?.edges[idx];
        selectEdge(e ? { from: e.from, to: e.to } : null);
      } else {
        selectEdge(null);
      }
      return next;
    });
  }, [graph, selectEdge]);

  const onNodesChange: OnNodesChange = useCallback(changes => {
    setRfNodes(curr => applyNodeChanges(changes, curr));
    // Mirror selection deltas into the store without a setState-in-updater.
    const selChanges = changes.filter(c => c.type === 'select') as Array<{ id: string; selected: boolean }>;
    if (selChanges.length) {
      const sel = new Set(selectedIdsRef.current);
      for (const c of selChanges) { if (c.selected) sel.add(c.id); else sel.delete(c.id); }
      const next = [...sel];
      // Advance the ref synchronously. During a rubber-band drag, several
      // 'select' batches can fire before React commits a render (which is the
      // only other place the ref refreshes). `applyNodeChanges` above already
      // accumulates correctly via its functional updater; without this line the
      // store mirror would read a stale ref and drop earlier batches, leaving
      // the store selection out of sync with rfNodes. The reflect effect then
      // sees a mismatch and rebuilds the node array mid-drag, which cancels the
      // selection rectangle — so releasing the mouse selects nothing.
      selectedIdsRef.current = next;
      setSelectedIds(next);
    }
  }, [setSelectedIds]);

  const persistPositions = useCallback((nodes: RfNode[]) => {
    // One batched update → one undo step for the whole drag (even multi-select).
    setPositions(nodes.map(n => ({ id: n.id, pos: { x: n.position.x, y: n.position.y } })));
  }, [setPositions]);

  // Persist every dragged node — otherwise un-persisted positions snap back to
  // stale store values on the next render (deselect, copy/paste, reload).
  // Dragging a node fires onNodeDragStop; dragging the multi-selection bounding
  // box fires onSelectionDragStop — handle both.
  const onNodeDragStop = useCallback((_e: React.MouseEvent | React.TouchEvent, node: RfNode, nodes: RfNode[]) => {
    persistPositions(nodes && nodes.length > 0 ? nodes : [node]);
  }, [persistPositions]);

  const onSelectionDragStop = useCallback((_e: React.MouseEvent, nodes: RfNode[]) => {
    persistPositions(nodes);
  }, [persistPositions]);

  const onDragOver = useCallback((e: React.DragEvent) => {
    if (e.dataTransfer.types.includes('application/flowboard-node-type')) {
      e.preventDefault();
      e.dataTransfer.dropEffect = 'copy';
    }
  }, []);

  const onDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    const typeName = e.dataTransfer.getData('application/flowboard-node-type');
    if (!typeName) return;
    // Optional config payload (used by the Discover panel to pre-fill
    // domainId / serviceName for a discovered Service/Client).
    let configOverride: Record<string, unknown> | undefined;
    const cfgRaw = e.dataTransfer.getData('application/flowboard-node-config');
    if (cfgRaw) { try { configOverride = JSON.parse(cfgRaw); } catch { /* ignore */ } }
    const pos = screenToFlowPosition({ x: e.clientX, y: e.clientY });
    addFromType(typeName, pos, configOverride);
  }, [screenToFlowPosition, addFromType]);

  const isValidConnection: IsValidConnection = useCallback(c => {
    if (!c.source || !c.target || !c.sourceHandle || !c.targetHandle) return false;
    if (c.source === c.target) return false;
    if (!graph) return false;
    const sNode = graph.nodes.find(n => n.id === c.source);
    const tNode = graph.nodes.find(n => n.id === c.target);
    if (!sNode || !tNode) return false;
    const sPorts = nodePorts(sNode, catalog);
    const tPorts = nodePorts(tNode, catalog);
    const sPort = sPorts.outputs.find(p => p.name === c.sourceHandle);
    const tPort = tPorts.inputs .find(p => p.name === c.targetHandle);
    if (!sPort || !tPort) return false;
    // Type mismatches are allowed — the resulting edge renders red so the
    // user sees it won't pass the engine's type check at reload. Rejecting
    // here would make React Flow treat the drop as "empty canvas" and pop
    // the create-connected-node menu instead, which hides the real port.
    // Transform.Extract's `in` port handles its struct-type mismatch via
    // the dedicated modal in onConnect below.
    return true;
  }, [graph, catalog]);

  const onConnect = useCallback((c: Connection) => {
    // Flag the drag as having landed on a valid handle so onConnectEnd
    // skips the create-node menu.
    if (connectInfoRef.current) connectInfoRef.current.connected = true;
    if (!c.source || !c.target || !c.sourceHandle || !c.targetHandle) return;
    if (!graph) return;

    const sNode = graph.nodes.find(n => n.id === c.source);
    const tNode = graph.nodes.find(n => n.id === c.target);
    if (sNode && tNode) {
      const srcTag = nodePorts(sNode, catalog).outputs.find(p => p.name === c.sourceHandle)?.typeTag;
      const tgtTag = nodePorts(tNode, catalog).inputs.find(p => p.name === c.targetHandle)?.typeTag;
      // If the target can adopt the source's type — which makes the new link a
      // clean type match — apply that config change automatically and connect.
      // No prompt: the result is a match, so there's nothing for the user to decide.
      if (srcTag && srcTag !== tgtTag) {
        const change = typeChangeForConnect(
          tNode.type, (tNode.config ?? {}) as Record<string, unknown>, c.targetHandle, srcTag);
        if (change) updateNodeConfig(tNode.id, change.patch);
      }
    }
    addEdgeAction(`${c.source}.${c.sourceHandle}`, `${c.target}.${c.targetHandle}`);
  }, [graph, catalog, addEdgeAction, updateNodeConfig]);

  // --- Drag-to-empty-space "create connected node" menu ----------------
  // When the user drags a connection off a port and releases on empty
  // canvas (not on another handle), we record the source port's typeTag,
  // filter the catalog to node types that have a matching port on the
  // opposite side, and pop a menu at the drop position. Picking an entry
  // creates the node at the drop location and wires the edge.
  type ConnectInfo = {
    sourceNodeId: string;
    sourceHandle: string;
    handleType: 'source' | 'target';
    typeTag: string;
    connected: boolean;
  };
  const connectInfoRef = useRef<ConnectInfo | null>(null);

  type NodeMenuCandidate = { typeName: string; portName: string };
  const [pendingNodeMenu, setPendingNodeMenu] = useState<{
    screenX: number;
    screenY: number;
    flowX: number;
    flowY: number;
    fromInfo: Omit<ConnectInfo, 'connected'>;
    candidates: NodeMenuCandidate[];
  } | null>(null);

  const onConnectStart = useCallback((_e: React.MouseEvent | React.TouchEvent, p: OnConnectStartParams) => {
    if (!p.nodeId || !p.handleId || !p.handleType || !graph) return;
    const node = graph.nodes.find(n => n.id === p.nodeId);
    if (!node) return;
    const ports = nodePorts(node, catalog);
    const ourPorts = p.handleType === 'source' ? ports.outputs : ports.inputs;
    const port = ourPorts.find(pt => pt.name === p.handleId);
    if (!port) return;
    connectInfoRef.current = {
      sourceNodeId: p.nodeId,
      sourceHandle: p.handleId,
      handleType: p.handleType,
      typeTag: port.typeTag,
      connected: false,
    };
  }, [graph, catalog]);

  const onConnectEnd = useCallback((event: MouseEvent | TouchEvent) => {
    const info = connectInfoRef.current;
    connectInfoRef.current = null;
    // onConnect fires before onConnectEnd on a successful drop, so this
    // flag tells us whether the drop landed on a valid handle.
    if (!info || info.connected) return;

    const ev = 'changedTouches' in event ? event.changedTouches[0] : event as MouseEvent;
    const screenX = ev.clientX;
    const screenY = ev.clientY;
    const flowPos = screenToFlowPosition({ x: screenX, y: screenY });

    // Build candidate list: nodes that have a matching port on the side
    // opposite to ours.
    const wantInput = info.handleType === 'source';  // dragging from an OUTPUT, need to land on an INPUT
    const candidates: NodeMenuCandidate[] = [];
    const seen = new Set<string>();
    const add = (typeName: string, portName: string) => {
      const k = typeName + ':' + portName;
      if (seen.has(k)) return;
      seen.add(k);
      candidates.push({ typeName, portName });
    };
    for (const c of catalog) {
      const ports = wantInput ? c.inputs : c.outputs;
      const port = ports.find(p => p.typeTag === info.typeTag);
      if (port) add(c.typeName, port.name);
    }
    // Config-typed / wildcard nodes (Log, ValueDisplay, GraphDisplay, Extract,
    // KeyValueAccumulator, ConstantSource) that the catalog probe wouldn't
    // surface for this tag at its default config.
    const known = new Set(catalog.map(c => c.typeName));
    for (const c of extraCandidates(info.typeTag, wantInput)) {
      if (known.has(c.typeName)) add(c.typeName, c.portName);
    }
    if (candidates.length === 0) return;

    candidates.sort((a, b) => a.typeName.localeCompare(b.typeName));
    setPendingNodeMenu({
      screenX, screenY, flowX: flowPos.x, flowY: flowPos.y,
      fromInfo: {
        sourceNodeId: info.sourceNodeId,
        sourceHandle: info.sourceHandle,
        handleType:   info.handleType,
        typeTag:      info.typeTag,
      },
      candidates,
    });
  }, [catalog, screenToFlowPosition]);

  const [menuSearch, setMenuSearch] = useState('');
  const closeNodeMenu = useCallback(() => { setPendingNodeMenu(null); setMenuSearch(''); }, []);

  // Reset the filter each time a fresh menu opens.
  useEffect(() => { if (pendingNodeMenu) setMenuSearch(''); }, [pendingNodeMenu]);

  // Dismiss the menu on any click outside it, or on Escape.
  useEffect(() => {
    if (!pendingNodeMenu) return;
    const mouse = (e: MouseEvent) => {
      const target = e.target as HTMLElement | null;
      if (target && target.closest('[data-create-node-menu]')) return;
      closeNodeMenu();
    };
    const key = (e: KeyboardEvent) => { if (e.key === 'Escape') { e.preventDefault(); closeNodeMenu(); } };
    // Defer so the same mousedown that opens the menu doesn't immediately close it.
    const id = window.setTimeout(() => document.addEventListener('mousedown', mouse), 0);
    document.addEventListener('keydown', key);
    return () => {
      window.clearTimeout(id);
      document.removeEventListener('mousedown', mouse);
      document.removeEventListener('keydown', key);
    };
  }, [pendingNodeMenu, closeNodeMenu]);

  // Copy / paste / select-all / undo / redo (Ctrl/Cmd + C / V / A / Z / Y).
  // Ignore when typing in a field so the browser's native editing still works.
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      if (!(e.ctrlKey || e.metaKey)) return;
      // Apply & Reload — works even from inside a field (overrides the browser's
      // native save dialog).
      if (e.key === 's' || e.key === 'S') {
        e.preventDefault();
        reload().then(errs => { if (errs.length) alert(errs.join('\n')); }).catch(console.error);
        return;
      }
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      if (e.key === 'c' || e.key === 'C') { copySelection(); }
      else if (e.key === 'v' || e.key === 'V') { paste(); }
      else if (e.key === 'z' || e.key === 'Z') { e.preventDefault(); undo(); }
      else if (e.key === 'y' || e.key === 'Y') { e.preventDefault(); redo(); }
      else if (e.key === 'a' || e.key === 'A') {
        // Select every node; preventDefault stops the browser's select-all-text.
        e.preventDefault();
        const g = useGraphStore.getState().graph;
        setSelectedIds(g ? g.nodes.map(n => n.id) : []);
      }
    };
    document.addEventListener('keydown', handler);
    return () => document.removeEventListener('keydown', handler);
  }, [copySelection, paste, setSelectedIds, undo, redo, reload]);

  const acceptNodeMenu = useCallback((c: NodeMenuCandidate) => {
    if (!pendingNodeMenu) return;
    const { flowX, flowY, fromInfo } = pendingNodeMenu;
    // Pre-fill the config of config-typed nodes so the connecting port adopts
    // the dragged port's type, instead of inheriting the catalog default
    // (which is what caused a new Extract to reuse a stale/other inputType).
    const configOverride = candidateConfig(c.typeName, c.portName, fromInfo.typeTag);
    addFromType(c.typeName, { x: flowX, y: flowY }, configOverride);
    // addFromType is synchronous and sets selectedNodeId to the new node's id.
    const newId = useGraphStore.getState().selectedNodeId;
    if (newId) {
      const from = fromInfo.handleType === 'source'
        ? `${fromInfo.sourceNodeId}.${fromInfo.sourceHandle}`
        : `${newId}.${c.portName}`;
      const to = fromInfo.handleType === 'source'
        ? `${newId}.${c.portName}`
        : `${fromInfo.sourceNodeId}.${fromInfo.sourceHandle}`;
      addEdgeAction(from, to);
    }
    setPendingNodeMenu(null);
  }, [pendingNodeMenu, addFromType, addEdgeAction]);

  const onNodesDelete = useCallback((deleted: RfNode[]) => {
    deleted.forEach(n => removeNodeAction(n.id));
  }, [removeNodeAction]);

  const onEdgesDelete = useCallback((deleted: RfEdge[]) => {
    deleted.forEach(e => {
      if (e.source && e.target && e.sourceHandle && e.targetHandle) {
        removeEdgeAction(e.source, e.sourceHandle, e.target, e.targetHandle);
      }
    });
    // Edge ids are positional (`e<index>` into graph.edges); deleting reindexes
    // the array, so a stale selected id would land on a different edge. Clear the
    // edge selection on delete so nothing is left selected.
    setSelectedEdgeIds(new Set());
    selectEdge(null);
  }, [removeEdgeAction, selectEdge]);

  return (
    <div className="flex-1 bg-slate-900 relative" onDragOver={onDragOver} onDrop={onDrop}>
      <ReactFlow
        nodes={rfNodes}
        edges={edges}
        nodeTypes={NODE_TYPES}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        onNodeDragStop={onNodeDragStop}
        onSelectionDragStop={onSelectionDragStop}
        onConnect={onConnect}
        onConnectStart={onConnectStart}
        onConnectEnd={onConnectEnd}
        isValidConnection={isValidConnection}
        onNodesDelete={onNodesDelete}
        onEdgesDelete={onEdgesDelete}
        deleteKeyCode={['Delete', 'Backspace']}
        // Ctrl+drag on empty canvas = rubber-band selection (Explorer-style);
        // Ctrl or Shift + click adds/removes nodes from the selection.
        selectionKeyCode="Control"
        multiSelectionKeyCode={['Control', 'Shift']}
        selectionOnDrag={false}
        fitView
      >
        <Background color="#334155" gap={20} />
        <Controls />
        <MiniMap
          maskColor="#1e293b66"
          nodeColor="#475569"
          pannable
          zoomable
          onClick={(_e, position) => setCenter(position.x, position.y, { zoom: getZoom(), duration: 400 })}
        />
      </ReactFlow>

      {pendingNodeMenu && (
        <div
          data-create-node-menu
          className="fixed z-30 bg-slate-800 border border-slate-700 rounded shadow-xl text-xs w-72"
          style={{ left: pendingNodeMenu.screenX, top: pendingNodeMenu.screenY }}
          onMouseDown={e => e.stopPropagation()}
        >
          <div className="flex items-center justify-between px-2 py-1 border-b border-slate-700">
            <span className="text-slate-400 uppercase tracking-wide text-[10px]">
              {pendingNodeMenu.fromInfo.handleType === 'source' ? 'Connect output to…' : 'Connect input from…'}
            </span>
            <button
              className="text-slate-500 hover:text-slate-200"
              onClick={closeNodeMenu}
              title="Close"
            >×</button>
          </div>
          <div className="px-2 py-1 text-[10px] text-slate-500 border-b border-slate-700 break-all">
            type: <code className="text-amber-300">{pendingNodeMenu.fromInfo.typeTag}</code>
          </div>
          <input
            autoFocus
            className="w-full px-2 py-1 bg-slate-900 border-b border-slate-700 text-xs outline-none focus:border-sky-600"
            placeholder="Type to filter…"
            value={menuSearch}
            onChange={e => setMenuSearch(e.target.value)}
            onKeyDown={e => {
              if (e.key === 'Escape') { e.preventDefault(); closeNodeMenu(); }
              else if (e.key === 'Enter') {
                e.preventDefault();
                const q = menuSearch.trim().toLowerCase();
                const list = q ? pendingNodeMenu.candidates.filter(c => c.typeName.toLowerCase().includes(q)) : pendingNodeMenu.candidates;
                if (list.length > 0) acceptNodeMenu(list[0]);
              }
            }}
          />
          <div className="max-h-80 overflow-y-auto">
            {pendingNodeMenu.candidates
              .filter(c => !menuSearch.trim() || c.typeName.toLowerCase().includes(menuSearch.trim().toLowerCase()))
              .map(c => (
                <button
                  key={c.typeName + ':' + c.portName}
                  className="w-full text-left px-2 py-1 hover:bg-slate-700 flex justify-between gap-2"
                  onClick={() => acceptNodeMenu(c)}
                >
                  <span className="text-slate-200 truncate">{c.typeName}</span>
                  <span className="text-slate-500 shrink-0">→ {c.portName}</span>
                </button>
              ))}
          </div>
        </div>
      )}
    </div>
  );
}
