// SPDX-License-Identifier: MIT
import { create } from 'zustand';
import type { CatalogEntry, GraphJson, GraphNodeJson, GraphEdgeJson } from '../api/types';
import { api, openWs } from '../api/client';
import {
  compile, decompile,
  type InnerCanvas, type InnerNode, type InnerEdge,
  type InnerStateNode, type InnerTransitionNode, type MachineConfig,
} from '../lib/machine_compile';
import { toSavedFile, fromSavedFile } from '../lib/group_compile';
import { nodePorts } from '../lib/node_ports';

type LiveValue = { value: unknown; ts: number };

// Inspector form changes to `inputType` / `outputType` are funnelled through
// requestUpdateNodeConfig; if the type field actually changes the store
// stashes the pending update here for a confirmation modal to pick up.
// All other config edits (prefix, value, op, …) and the connection-mismatch
// path in GraphCanvas use the plain updateNodeConfig action and bypass this.
export type TypeField = 'inputType' | 'outputType';

export type PendingTypeChange = {
  nodeId: string;
  field:  TypeField;
  oldValue: string;
  newValue: string;
  fullNewConfig: Record<string, unknown>;
};

type State = {
  catalog: CatalogEntry[];
  logTypes: string[];   // every type tag with a Sinks.Log factory registered
  throttleTypes: string[];  // every type tag with a Transform.Throttle factory registered
  synchronizeTypes: string[];  // every type tag with a Transform.Synchronize factory registered
  graph:   GraphJson | null;
  cleanGraph: GraphJson | null;
  selectedNodeId: string | null;     // the single selected node (null when 0 or >1)
  selectedNodeIds: string[];         // full selection set (multi-select)
  selectedEdge: { from: string; to: string } | null;  // selected link (null when none)
  fitViewNonce: number;  // bumped to ask the canvas to fit the view (e.g. after auto-arrange)
  autoArrangeNonce: number;  // bumped to ask the canvas to auto-arrange (using measured node sizes)
  clipboard: { nodes: GraphNodeJson[]; edges: GraphEdgeJson[] } | null;
  live:    Record<string, LiveValue>;
  stats:   { pushed: Record<string, number>; dropped: Record<string, number> };
  running: boolean;
  ws?: WebSocket;
  pendingTypeChange: PendingTypeChange | null;
  // Undo/redo history of `graph` snapshots for the active scope. `past` holds
  // states older than the current one (most recent last); `future` holds states
  // undone-past the current one (next-to-redo first). Reset whenever the active
  // scope swaps (load / open|close group) since snapshots are scope-specific.
  past:   GraphJson[];
  future: GraphJson[];

  refreshCatalog:  () => Promise<void>;
  refreshLogTypes: () => Promise<void>;
  refreshThrottleTypes: () => Promise<void>;
  refreshSynchronizeTypes: () => Promise<void>;
  refreshGraph:    () => Promise<void>;
  refreshStats:    () => Promise<void>;
  setGraph: (g: GraphJson) => void;
  loadGraph: (g: GraphJson) => void;   // replace the whole graph (exits any group scope)
  reload:   () => Promise<string[]>;
  pause:  () => Promise<void>;
  resume: () => Promise<void>;
  startWs: () => void;
  stopWs:  () => void;
  subscribe:   (ports: string[]) => void;
  unsubscribe: (ports: string[]) => void;

  select:              (id: string | null) => void;
  setSelectedIds:      (ids: string[]) => void;
  selectEdge:          (edge: { from: string; to: string } | null) => void;
  requestFitView:      () => void;
  requestAutoArrange:  () => void;
  undo:                () => void;
  redo:                () => void;
  copySelection:       () => void;
  paste:               () => void;
  updateNodeConfig:    (nodeId: string, nextConfig: Record<string, unknown>) => void;
  // Inspector-form entry point. Same shape as updateNodeConfig, but if the
  // proposed config changes inputType or outputType it stashes a
  // PendingTypeChange instead of applying — the modal then resolves it.
  requestUpdateNodeConfig: (nodeId: string, nextConfig: Record<string, unknown>) => void;
  confirmTypeChange:   () => void;
  cancelTypeChange:    () => void;
  updateNodePosition:  (nodeId: string, pos: { x: number; y: number }) => void;
  // Batch position update (one history entry) — used when a drag moves several
  // selected nodes at once, so undo reverts the whole drag in a single step.
  updateNodePositions: (updates: { id: string; pos: { x: number; y: number } }[]) => void;
  updateNodeSize:      (nodeId: string, size: { width: number; height: number }) => void;
  setNodeExpandedGroups: (nodeId: string, groups: string[]) => void;
  revertNodeConfig:    (nodeId: string) => void;

  addNodeFromType: (typeName: string, pos: { x: number; y: number },
                    configOverride?: Record<string, unknown>) => void;
  renameNode:      (oldId: string, newId: string) => boolean;
  removeNode:      (id: string) => void;
  addEdge:         (from: string, to: string) => void;
  removeEdge:      (source: string, sourceHandle: string, target: string, targetHandle: string) => void;
  // Expose an inner node's port as a group boundary port: creates a
  // Group.Input/Group.Output terminal wired to it (active scope = open group).
  promotePort:     (nodeId: string, portName: string, direction: 'input' | 'output', typeTag: string) => void;

  // --- Flow.Group inner-canvas editing -------------------------------------------
  // `graph` always points at the ACTIVE scope: the top-level graph, or — while
  // editing a group — that group's inner graph. Descending into a group pushes
  // the outer graph onto groupStack and swaps `graph` to the inner one, so the
  // whole canvas/Inspector/action surface keeps working unchanged. closeGroup
  // folds the edited inner graph back into the outer node's config.group.
  // getRootGraph() folds the entire stack back to the top-level graph (used by
  // reload/export, which must flatten the full tree, not just the open scope).
  groupStack: { id: string; outer: GraphJson }[];
  openGroup:    (id: string) => void;
  closeGroup:   () => void;
  getRootGraph: () => GraphJson | null;

  // --- Flow.StateMachine inner-canvas editing ------------------------------------
  // When editingMachineId is set, the canvas shows the machine's inner FSM
  // (decompiled from its config) instead of the top-level graph. Edits mutate
  // innerCanvas; closeMachine compiles it back into the node's config.
  editingMachineId: string | null;
  innerCanvas:      InnerCanvas | null;
  innerError:       string | null;
  innerCounter:     number;
  // Inner-canvas undo/redo — a SEPARATE history list from the main graph's, so
  // editing a machine doesn't disturb (or get disturbed by) graph history.
  innerPast:        InnerCanvas[];
  innerFuture:      InnerCanvas[];
  innerClipboard:   { nodes: InnerNode[]; edges: InnerEdge[] } | null;

  openMachine:  (id: string) => void;
  closeMachine: () => boolean;   // returns false (and stays open) on compile error
  addInnerState:        (pos: { x: number; y: number }) => void;
  addInnerTransition:   (pos: { x: number; y: number }) => void;
  updateInnerState:     (id: string, patch: Partial<Pick<InnerStateNode, 'name' | 'initial'>>) => void;
  updateInnerTransition:(id: string, patch: Partial<Pick<InnerTransitionNode, 'trigger'>>) => void;
  setInitialState:      (id: string) => void;
  moveInnerNode:        (id: string, pos: { x: number; y: number }) => void;
  moveInnerNodes:       (updates: { id: string; pos: { x: number; y: number } }[]) => void;
  addInnerEdge:         (source: string, target: string) => void;
  removeInnerNode:      (id: string) => void;
  removeInnerNodes:     (ids: string[]) => void;
  removeInnerEdge:      (source: string, target: string) => void;
  undoInner:            () => void;
  redoInner:            () => void;
  copyInnerSelection:   (ids: string[]) => void;
  pasteInner:           () => void;
  arrangeInner:         () => void;
};

function deepClone<T>(v: T): T {
  // structuredClone is available in Node 17+ and modern browsers; this is plenty for graph JSON.
  return typeof structuredClone === 'function'
    ? structuredClone(v)
    : JSON.parse(JSON.stringify(v));
}

// Max retained undo steps (the spec asks for ≥10). Graph objects are treated
// immutably by every mutation (always rebuilt via spread, never edited in
// place), so storing references rather than clones is safe and cheap.
const HISTORY_LIMIT = 50;

// History slice to merge into a graph-mutating `set`: snapshots the CURRENT
// graph onto the undo stack (capped) and clears the redo stack. Call as
// `set(state => ({ graph: next, ...recordHistory(state) }))`.
function recordHistory(state: State): Pick<State, 'past' | 'future'> {
  if (!state.graph) return { past: state.past, future: state.future };
  return { past: [...state.past, state.graph].slice(-HISTORY_LIMIT), future: [] };
}

// Write an inner graph back into the outer graph's group node `id`.
function writeBackGroup(outer: GraphJson, id: string, inner: GraphJson): GraphJson {
  const nodes = outer.nodes.map(n =>
    n.id === id
      ? { ...n, config: { ...(n.config ?? {}), group: { nodes: inner.nodes, edges: inner.edges } } }
      : n);
  return { ...outer, nodes };
}

// Fold the active scope + stack back into the single top-level (root) graph.
function foldToRoot(active: GraphJson, stack: { id: string; outer: GraphJson }[]): GraphJson {
  let cur = active;
  for (let i = stack.length - 1; i >= 0; i--) cur = writeBackGroup(stack[i].outer, stack[i].id, cur);
  return cur;
}

// History slice for an inner-canvas mutation (mirrors recordHistory for graph).
// InnerCanvas is rebuilt immutably by every inner action, so references are safe.
function recordInner(state: State): Pick<State, 'innerPast' | 'innerFuture'> {
  if (!state.innerCanvas) return { innerPast: state.innerPast, innerFuture: state.innerFuture };
  return { innerPast: [...state.innerPast, state.innerCanvas].slice(-HISTORY_LIMIT), innerFuture: [] };
}

// Layered left-to-right auto-layout for the inner FSM canvas (state -> transition
// -> state). Longest-path layering + per-layer stacking with gaps; centred.
function layoutInner(canvas: InnerCanvas): InnerNode[] {
  const ids = canvas.nodes.map(n => n.id);
  if (ids.length === 0) return canvas.nodes;
  const idx = new Map(ids.map((id, i) => [id, i]));
  const layer = new Map(ids.map(id => [id, 0]));
  for (let it = 0; it < canvas.nodes.length; it++) {
    let changed = false;
    for (const e of canvas.edges) {
      if (!layer.has(e.source) || !layer.has(e.target)) continue;
      const w = layer.get(e.source)! + 1;
      if (w > layer.get(e.target)!) { layer.set(e.target, w); changed = true; }
    }
    if (!changed) break;
  }
  const maxL = Math.max(...ids.map(id => layer.get(id)!));
  const layers: string[][] = Array.from({ length: maxL + 1 }, () => []);
  for (const id of ids) layers[layer.get(id)!].push(id);

  const H_GAP = 90, V_GAP = 55;
  const sizeById = new Map(canvas.nodes.map(n =>
    [n.id, n.kind === 'state' ? { w: 150, h: 92 } : { w: 130, h: 70 }]));
  const layerHeight = (L: string[]) =>
    L.reduce((s, id) => s + sizeById.get(id)!.h, 0) + Math.max(0, L.length - 1) * V_GAP;
  const tallest = Math.max(0, ...layers.map(layerHeight));

  const posById = new Map<string, { x: number; y: number }>();
  let x = 0;
  for (const L of layers) {
    const layerW = Math.max(130, ...L.map(id => sizeById.get(id)!.w));
    let y = (tallest - layerHeight(L)) / 2;
    const sorted = [...L].sort((a, b) => idx.get(a)! - idx.get(b)!);
    for (const id of sorted) {
      posById.set(id, { x: Math.round(x), y: Math.round(y) });
      y += sizeById.get(id)!.h + V_GAP;
    }
    x += layerW + H_GAP;
  }
  return canvas.nodes.map(n => ({ ...n, position: posById.get(n.id) ?? n.position }));
}

function sendOrQueue(ws: WebSocket | undefined, op: 'subscribe' | 'unsubscribe', ports: string[]): void {
  if (!ws) return;
  const payload = JSON.stringify({ op, ports });
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(payload);
  } else if (ws.readyState === WebSocket.CONNECTING) {
    ws.addEventListener('open', () => ws.send(payload), { once: true });
  }
  // CLOSING / CLOSED: drop silently — a future startWs() will resubscribe.
}

export const useGraphStore = create<State>((set, get) => ({
  catalog: [],
  logTypes: [],
  throttleTypes: [],
  synchronizeTypes: [],
  graph: null,
  cleanGraph: null,
  selectedNodeId: null,
  selectedNodeIds: [],
  selectedEdge: null,
  fitViewNonce: 0,
  autoArrangeNonce: 0,
  clipboard: null,
  live: {},
  stats: { pushed: {}, dropped: {} },
  running: true,
  pendingTypeChange: null,
  past: [],
  future: [],
  groupStack: [],
  editingMachineId: null,
  innerCanvas: null,
  innerError: null,
  innerCounter: 0,
  innerPast: [],
  innerFuture: [],
  innerClipboard: null,

  refreshCatalog:  async () => set({ catalog:  await api.catalog() }),
  refreshLogTypes: async () => set({ logTypes: await api.logTypes() }),
  refreshThrottleTypes: async () => set({ throttleTypes: await api.throttleTypes() }),
  refreshSynchronizeTypes: async () => set({ synchronizeTypes: await api.synchronizeTypes() }),
  refreshGraph:   async () => {
    // The engine round-trips the dual-format doc reload() sends: prefer the
    // embedded authoring graph so groups + Note annotations survive a hard
    // reload (the flat runtime graph the engine actually runs drops them).
    const g = fromSavedFile(await api.graph());
    set({ graph: g, cleanGraph: deepClone(g), past: [], future: [] });
  },
  refreshStats:   async () => {
    const s = await api.stats();
    const pushed: Record<string, number> = {}, dropped: Record<string, number> = {};
    s.edges.forEach(e => {
      const k = `${e.from}->${e.to}`;
      pushed[k] = e.pushed; dropped[k] = e.dropped;
    });
    set({ stats: { pushed, dropped } });
  },

  setGraph: g => set(state => ({ graph: g, ...recordHistory(state) })),
  loadGraph: g => set({
    graph: g, cleanGraph: deepClone(g),
    groupStack: [], selectedNodeId: null, selectedNodeIds: [], selectedEdge: null,
    editingMachineId: null, innerCanvas: null, innerError: null,
    past: [], future: [],
  }),
  reload: async () => {
    const st = get();
    if (!st.graph) return ['no graph in store'];
    // Reload always sends the FULL tree: fold any open group scopes back to the
    // root, then inline every Flow.Group (the engine only runs flat graphs).
    // The store keeps the un-flattened graph (groups are the frontend's source
    // of truth), so we expand a copy and never overwrite the store with it.
    const root = foldToRoot(st.graph, st.groupStack);
    // Send the dual-format doc: the flat graph the engine runs, plus the grouped
    // authoring graph (groups + Note annotations) embedded under
    // metadata.authoring. The engine stores the whole doc as its source_json and
    // returns it on GET /api/graph, so a hard page reload restores the full
    // editor graph via fromSavedFile (in refreshGraph) instead of losing
    // frontend-only nodes. The loader skips the frontend-only types at runtime.
    const name = (root.metadata?.name as string | undefined) ?? 'graph';
    let doc: GraphJson;
    try { doc = toSavedFile(root, name); }
    catch (e) { return [e instanceof Error ? e.message : String(e)]; }
    const r = await api.reload(doc);
    if (!r.errors || r.errors.length === 0) {
      set({ cleanGraph: deepClone(st.graph) });
    }
    return r.errors ?? [];
  },
  pause:  async () => { await api.pause();  set({ running: false }); },
  resume: async () => { await api.resume(); set({ running: true  }); },

  startWs: () => {
    if (get().ws) return;
    const ws = openWs(msg => {
      if ('port' in msg && msg.value !== undefined && msg.ts !== undefined) {
        set(state => ({ live: { ...state.live, [msg.port]: { value: msg.value, ts: msg.ts } } }));
      }
    });
    set({ ws });
  },
  stopWs: () => { get().ws?.close(); set({ ws: undefined }); },
  // subscribe/unsubscribe may be called before the WebSocket finishes
  // its CONNECTING handshake. The graph fetch typically resolves faster
  // than the WS connect, so the App's "subscribe to all edge sources"
  // effect would otherwise hit `ws.send()` on a still-connecting socket,
  // which throws `InvalidStateError`. Defer the send until 'open'.
  subscribe:   ports => sendOrQueue(get().ws, 'subscribe',   ports),
  unsubscribe: ports => sendOrQueue(get().ws, 'unsubscribe', ports),

  select: id => set({ selectedNodeId: id, selectedNodeIds: id ? [id] : [], selectedEdge: null }),

  setSelectedIds: ids => set({
    selectedNodeIds: ids,
    selectedNodeId: ids.length === 1 ? ids[0] : null,
    ...(ids.length > 0 ? { selectedEdge: null } : {}),
  }),

  requestFitView: () => set(s => ({ fitViewNonce: s.fitViewNonce + 1 })),
  requestAutoArrange: () => set(s => ({ autoArrangeNonce: s.autoArrangeNonce + 1 })),

  // Selecting a link clears any node selection so the Inspector shows the
  // edge's endpoint types instead of a node form.
  selectEdge: edge => set({
    selectedEdge: edge,
    ...(edge ? { selectedNodeId: null, selectedNodeIds: [] } : {}),
  }),

  undo: () => set(state => {
    if (!state.graph || state.past.length === 0) return {};
    const prev = state.past[state.past.length - 1];
    return {
      graph: prev,
      past: state.past.slice(0, -1),
      future: [state.graph, ...state.future].slice(0, HISTORY_LIMIT),
      // The restored graph may not contain the previously-selected node/edge.
      selectedNodeId: null, selectedNodeIds: [], selectedEdge: null,
      pendingTypeChange: null,
    };
  }),

  redo: () => set(state => {
    if (!state.graph || state.future.length === 0) return {};
    const next = state.future[0];
    return {
      graph: next,
      future: state.future.slice(1),
      past: [...state.past, state.graph].slice(-HISTORY_LIMIT),
      selectedNodeId: null, selectedNodeIds: [], selectedEdge: null,
      pendingTypeChange: null,
    };
  }),

  copySelection: () => {
    const state = get();
    if (!state.graph) return;
    const ids = new Set(state.selectedNodeIds);
    if (ids.size === 0) return;
    const nodes = state.graph.nodes.filter(n => ids.has(n.id));
    // Only edges fully internal to the selection are copied.
    const edges = state.graph.edges.filter(e => {
      const s = e.from.split('.')[0];
      const t = e.to.split('.')[0];
      return ids.has(s) && ids.has(t);
    });
    set({ clipboard: { nodes: deepClone(nodes), edges: deepClone(edges) } });
  },

  paste: () => set(state => {
    if (!state.graph || !state.clipboard || state.clipboard.nodes.length === 0) return {};
    const used = new Set(state.graph.nodes.map(n => n.id));
    const idMap = new Map<string, string>();
    const uniqueId = (base: string) => {
      // Strip an existing copy suffix so re-pasting yields foo-copy1, foo-copy2
      // rather than foo-copy-copy-copy.
      const stem = base.replace(/-copy-?\d*$/, '');
      let i = 1;
      let cand = `${stem}-copy${i}`;
      while (used.has(cand)) cand = `${stem}-copy${++i}`;
      used.add(cand);
      return cand;
    };
    const newNodes: GraphNodeJson[] = state.clipboard.nodes.map(n => {
      const nid = uniqueId(n.id);
      idMap.set(n.id, nid);
      return {
        ...deepClone(n),
        id: nid,
        position: { x: (n.position?.x ?? 0) + 40, y: (n.position?.y ?? 0) + 40 },
      };
    });
    const remap = (ref: string) => {
      const dot = ref.indexOf('.');
      const node = dot < 0 ? ref : ref.slice(0, dot);
      const port = dot < 0 ? '' : ref.slice(dot);
      const mapped = idMap.get(node);
      return mapped ? mapped + port : ref;
    };
    const newEdges: GraphEdgeJson[] = state.clipboard.edges.map(e => ({
      ...deepClone(e), from: remap(e.from), to: remap(e.to),
    }));
    const pastedIds = newNodes.map(n => n.id);
    return {
      graph: {
        ...state.graph,
        nodes: [...state.graph.nodes, ...newNodes],
        edges: [...state.graph.edges, ...newEdges],
      },
      selectedNodeIds: pastedIds,
      selectedNodeId: pastedIds.length === 1 ? pastedIds[0] : null,
      ...recordHistory(state),
    };
  }),

  updateNodeConfig: (nodeId, nextConfig) => set(state => {
    if (!state.graph) return {};
    const nodes = state.graph.nodes.map(n =>
      n.id === nodeId ? { ...n, config: nextConfig } : n);
    return { graph: { ...state.graph, nodes }, ...recordHistory(state) };
  }),

  requestUpdateNodeConfig: (nodeId, nextConfig) => {
    const state = get();
    if (!state.graph) return;
    const oldNode = state.graph.nodes.find(n => n.id === nodeId);
    const oldConfig = (oldNode?.config ?? {}) as Record<string, unknown>;
    const typeFields: TypeField[] = ['inputType', 'outputType'];
    // The modal exists to warn that connected edges become mismatched (red).
    // It should only appear when the change actually leaves a real mismatch —
    // not when the user is *fixing* a red edge (e.g. switching inputType to the
    // type the wired source already emits). So resolve the would-be port types
    // under `proposed` and check whether any edge touching this node ends up
    // with differing endpoint tags.
    const leavesMismatch = (proposed: Record<string, unknown>): boolean => {
      const g = state.graph!;
      const tagAt = (endpoint: string, side: 'out' | 'in'): string | undefined => {
        const [id, ...rest] = endpoint.split('.');
        const port = rest.join('.');
        const n = g.nodes.find(x => x.id === id);
        if (!n) return undefined;
        const cfg = id === nodeId ? proposed : n.config;
        const ports = nodePorts({ type: n.type, config: cfg }, state.catalog);
        const list = side === 'out' ? ports.outputs : ports.inputs;
        return list.find(p => p.name === port)?.typeTag;
      };
      return g.edges.some(e => {
        if (!e.from.startsWith(nodeId + '.') && !e.to.startsWith(nodeId + '.')) return false;
        const s = tagAt(e.from, 'out');
        const t = tagAt(e.to, 'in');
        return s !== undefined && t !== undefined && s !== t;
      });
    };
    for (const field of typeFields) {
      const oldV = String(oldConfig[field] ?? '');
      const newV = String(nextConfig[field] ?? '');
      if (oldV !== newV && (oldV !== '' || newV !== '')) {
        if (leavesMismatch(nextConfig)) {
          // Stash and bail — the modal will call confirm/cancel.
          // We also keep any other config tweaks the form bundled with the
          // type change (e.g. ExtractForm clears `field` when inputType
          // changes) so confirming applies them atomically.
          set({ pendingTypeChange: {
            nodeId, field, oldValue: oldV, newValue: newV,
            fullNewConfig: nextConfig,
          }});
          return;
        }
        break;  // No resulting mismatch — fall through to direct apply.
      }
    }
    set(s => {
      if (!s.graph) return {};
      const nodes = s.graph.nodes.map(n =>
        n.id === nodeId ? { ...n, config: nextConfig } : n);
      return { graph: { ...s.graph, nodes }, ...recordHistory(s) };
    });
  },

  confirmTypeChange: () => {
    const pending = get().pendingTypeChange;
    if (!pending) return;
    set(s => {
      if (!s.graph) return { pendingTypeChange: null };
      const nodes = s.graph.nodes.map(n =>
        n.id === pending.nodeId ? { ...n, config: pending.fullNewConfig } : n);
      return { graph: { ...s.graph, nodes }, pendingTypeChange: null, ...recordHistory(s) };
    });
  },

  cancelTypeChange: () => set({ pendingTypeChange: null }),

  updateNodePosition: (nodeId, pos) => set(state => {
    if (!state.graph) return {};
    const nodes = state.graph.nodes.map(n =>
      n.id === nodeId ? { ...n, position: pos } : n);
    return { graph: { ...state.graph, nodes }, ...recordHistory(state) };
  }),

  updateNodePositions: updates => set(state => {
    if (!state.graph || updates.length === 0) return {};
    const byId = new Map(updates.map(u => [u.id, u.pos]));
    const nodes = state.graph.nodes.map(n =>
      byId.has(n.id) ? { ...n, position: byId.get(n.id)! } : n);
    return { graph: { ...state.graph, nodes }, ...recordHistory(state) };
  }),

  updateNodeSize: (nodeId, size) => set(state => {
    if (!state.graph) return {};
    const nodes = state.graph.nodes.map(n =>
      n.id === nodeId ? { ...n, width: size.width, height: size.height } : n);
    return { graph: { ...state.graph, nodes } };
  }),

  // Persist a node's expanded port-group keys. Like updateNodeSize, this is a
  // frontend-only UI state change, so it does NOT record a history entry.
  setNodeExpandedGroups: (nodeId, groups) => set(state => {
    if (!state.graph) return {};
    const nodes = state.graph.nodes.map(n =>
      n.id === nodeId ? { ...n, expandedGroups: groups } : n);
    return { graph: { ...state.graph, nodes } };
  }),

  revertNodeConfig: nodeId => set(state => {
    if (!state.graph || !state.cleanGraph) return {};
    const clean = state.cleanGraph.nodes.find(n => n.id === nodeId);
    if (!clean) return {};
    const nodes = state.graph.nodes.map(n =>
      n.id === nodeId ? { ...n, config: clean.config ?? {} } : n);
    return { graph: { ...state.graph, nodes }, ...recordHistory(state) };
  }),

  addNodeFromType: (typeName, pos, configOverride) => set(state => {
    if (!state.graph) return {};
    const entry = state.catalog.find(c => c.typeName === typeName);

    const prefix = typeName.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'node';
    const used = new Set(state.graph.nodes.map(n => n.id));
    let i = 1;
    while (used.has(`${prefix}-${i}`)) i++;
    const id = `${prefix}-${i}`;

    const newNode = {
      id,
      type: typeName,
      config: {
        ...((entry?.configDefaults as Record<string, unknown>) ?? {}),
        ...(configOverride ?? {}),
      },
      position: { x: pos.x, y: pos.y },
    };
    return {
      graph: { ...state.graph, nodes: [...state.graph.nodes, newNode] },
      selectedNodeId: id,
      selectedNodeIds: [id],
      ...recordHistory(state),
    };
  }),

  renameNode: (oldId, newId) => {
    const state = get();
    if (!state.graph) return false;
    newId = newId.trim();
    if (!newId || newId === oldId) return false;
    if (state.graph.nodes.some(n => n.id === newId)) return false;  // must stay unique
    const prefix = oldId + '.';
    const nodes = state.graph.nodes.map(n => (n.id === oldId ? { ...n, id: newId } : n));
    const edges = state.graph.edges.map(e => ({
      ...e,
      from: e.from.startsWith(prefix) ? newId + '.' + e.from.slice(prefix.length) : e.from,
      to:   e.to.startsWith(prefix)   ? newId + '.' + e.to.slice(prefix.length)   : e.to,
    }));
    set({
      graph: { ...state.graph, nodes, edges },
      selectedNodeId:   state.selectedNodeId === oldId ? newId : state.selectedNodeId,
      selectedNodeIds:  state.selectedNodeIds.map(i => (i === oldId ? newId : i)),
      editingMachineId: state.editingMachineId === oldId ? newId : state.editingMachineId,
      ...recordHistory(state),
    });
    return true;
  },

  removeNode: id => set(state => {
    if (!state.graph) return {};
    const nodes = state.graph.nodes.filter(n => n.id !== id);
    const edges = state.graph.edges.filter(e =>
      !e.from.startsWith(id + '.') && !e.to.startsWith(id + '.'));
    // No such node and no connected edges → nothing changed, so don't push an
    // empty history step (a node delete cascades an onEdgesDelete for the same
    // edges this already removed).
    if (nodes.length === state.graph.nodes.length && edges.length === state.graph.edges.length)
      return {};
    return {
      graph: { ...state.graph, nodes, edges },
      selectedNodeId: state.selectedNodeId === id ? null : state.selectedNodeId,
      selectedNodeIds: state.selectedNodeIds.filter(i => i !== id),
      ...recordHistory(state),
    };
  }),

  addEdge: (from, to) => set(state => {
    if (!state.graph) return {};
    // Derive backpressure from the source port's op prefix, mirroring the engine
    // loader: Report*/Config* (and non-API ports) -> drop_oldest (latest wins);
    // Notify*/Event*/Cmd* -> block (reliable, never drop).
    const srcPort = from.split('.').slice(1).join('.');  // strip leading "nodeId."
    const op = srcPort.split('.')[0] ?? '';
    const reliable = /^(Notify|Event|Cmd)/.test(op);
    const policy = reliable ? ('block' as const) : ('drop_oldest' as const);
    const edges = [...state.graph.edges, { from, to, capacity: 256, policy }];
    return { graph: { ...state.graph, edges }, ...recordHistory(state) };
  }),

  removeEdge: (source, sourceHandle, target, targetHandle) => set(state => {
    if (!state.graph) return {};
    const from = `${source}.${sourceHandle}`;
    const to   = `${target}.${targetHandle}`;
    const edges = state.graph.edges.filter(e => !(e.from === from && e.to === to));
    if (edges.length === state.graph.edges.length) return {};  // nothing removed → no history
    return { graph: { ...state.graph, edges }, ...recordHistory(state) };
  }),

  promotePort: (nodeId, portName, direction, typeTag) => set(state => {
    if (!state.graph) return {};
    const isInput = direction === 'input';
    const termType = isInput ? 'Group.Input' : 'Group.Output';
    const prefix = isInput ? 'group-input' : 'group-output';
    const used = new Set(state.graph.nodes.map(n => n.id));
    let i = 1; while (used.has(`${prefix}-${i}`)) i++;
    const tid = `${prefix}-${i}`;
    const src = state.graph.nodes.find(n => n.id === nodeId);
    const base = src?.position ?? { x: 0, y: 0 };
    const pos = { x: base.x + (isInput ? -240 : 280), y: base.y };
    const termNode = { id: tid, type: termType, config: { portName, typeTag }, position: pos };
    // Input terminal feeds the node's input; output terminal consumes its output.
    const edge = isInput
      ? { from: `${tid}.out`, to: `${nodeId}.${portName}` }
      : { from: `${nodeId}.${portName}`, to: `${tid}.in` };
    const op = edge.from.split('.').slice(1).join('.').split('.')[0] ?? '';
    const policy = /^(Notify|Event|Cmd)/.test(op) ? ('block' as const) : ('drop_oldest' as const);
    return {
      graph: {
        ...state.graph,
        nodes: [...state.graph.nodes, termNode],
        edges: [...state.graph.edges, { ...edge, capacity: 256, policy }],
      },
      selectedNodeId: tid,
      selectedNodeIds: [tid],
      ...recordHistory(state),
    };
  }),

  // --- Flow.Group inner-canvas editing -------------------------------------------

  getRootGraph: () => {
    const st = get();
    return st.graph ? foldToRoot(st.graph, st.groupStack) : null;
  },

  openGroup: id => set(state => {
    if (!state.graph) return {};
    const node = state.graph.nodes.find(n => n.id === id);
    if (!node || node.type !== 'Flow.Group') return {};
    const grp = (node.config as { group?: { nodes: GraphNodeJson[]; edges: GraphEdgeJson[] } } | undefined)?.group
              ?? { nodes: [], edges: [] };
    const inner: GraphJson = {
      version: state.graph.version,
      metadata: { name: id },
      nodes: grp.nodes,
      edges: grp.edges,
    };
    return {
      groupStack: [...state.groupStack, { id, outer: state.graph }],
      graph: inner,
      cleanGraph: deepClone(inner),
      selectedNodeId: null,
      selectedNodeIds: [],
      past: [], future: [],
    };
  }),

  closeGroup: () => set(state => {
    if (state.groupStack.length === 0 || !state.graph) return {};
    const top = state.groupStack[state.groupStack.length - 1];
    const outer = writeBackGroup(top.outer, top.id, state.graph);
    return {
      groupStack: state.groupStack.slice(0, -1),
      graph: outer,
      cleanGraph: deepClone(outer),
      selectedNodeId: null,
      selectedNodeIds: [],
      past: [], future: [],
    };
  }),

  // --- Flow.StateMachine inner-canvas editing ------------------------------------

  openMachine: id => set(state => {
    if (!state.graph) return {};
    const node = state.graph.nodes.find(n => n.id === id);
    if (!node || node.type !== 'Flow.StateMachine') return {};
    const cfg = (node.config ?? {}) as Partial<MachineConfig>;
    const canvas = decompile({
      initial: cfg.initial ?? '',
      states: cfg.states ?? [],
      transitions: cfg.transitions ?? [],
    });
    return {
      editingMachineId: id, innerCanvas: canvas, innerError: null, selectedNodeId: null,
      innerPast: [], innerFuture: [], innerCounter: 0,
    };
  }),

  closeMachine: () => {
    const state = get();
    if (!state.editingMachineId || !state.innerCanvas) {
      set({ editingMachineId: null, innerCanvas: null, innerError: null });
      return true;
    }
    const result = compile(state.innerCanvas);
    if (!result.config) {
      set({ innerError: result.errors.join('; ') });
      return false;
    }
    const id = state.editingMachineId;
    const cfg = result.config;
    set(s => {
      if (!s.graph) return { editingMachineId: null, innerCanvas: null, innerError: null };
      const nodes = s.graph.nodes.map(n =>
        n.id === id ? { ...n, config: cfg as unknown as Record<string, unknown> } : n);
      return {
        graph: { ...s.graph, nodes },
        editingMachineId: null, innerCanvas: null, innerError: null,
        ...recordHistory(s),
      };
    });
    return true;
  },

  addInnerState: pos => set(state => {
    if (!state.innerCanvas) return {};
    const n = state.innerCounter + 1;
    const existing = new Set(
      state.innerCanvas.nodes
        .filter((x): x is InnerStateNode => x.kind === 'state')
        .map(x => x.name));
    let name = `State${n}`;
    while (existing.has(name)) name = `State${name.length}`;
    const node: InnerStateNode = {
      id: `state:__new${n}`, kind: 'state', name,
      initial: existing.size === 0,  // first state added becomes initial
      position: pos,
    };
    return {
      innerCanvas: { ...state.innerCanvas, nodes: [...state.innerCanvas.nodes, node] },
      innerCounter: n,
      ...recordInner(state),
    };
  }),

  addInnerTransition: pos => set(state => {
    if (!state.innerCanvas) return {};
    const n = state.innerCounter + 1;
    const node: InnerTransitionNode = {
      id: `transition:__new${n}`, kind: 'transition', trigger: `trigger${n}`, position: pos,
    };
    return {
      innerCanvas: { ...state.innerCanvas, nodes: [...state.innerCanvas.nodes, node] },
      innerCounter: n,
      ...recordInner(state),
    };
  }),

  updateInnerState: (id, patch) => set(state => {
    if (!state.innerCanvas) return {};
    const nodes = state.innerCanvas.nodes.map(x =>
      x.id === id && x.kind === 'state' ? { ...x, ...patch } : x);
    return { innerCanvas: { ...state.innerCanvas, nodes }, innerError: null, ...recordInner(state) };
  }),

  updateInnerTransition: (id, patch) => set(state => {
    if (!state.innerCanvas) return {};
    const nodes = state.innerCanvas.nodes.map(x =>
      x.id === id && x.kind === 'transition' ? { ...x, ...patch } : x);
    return { innerCanvas: { ...state.innerCanvas, nodes }, innerError: null, ...recordInner(state) };
  }),

  setInitialState: id => set(state => {
    if (!state.innerCanvas) return {};
    const nodes = state.innerCanvas.nodes.map(x =>
      x.kind === 'state' ? { ...x, initial: x.id === id } : x);
    return { innerCanvas: { ...state.innerCanvas, nodes }, innerError: null, ...recordInner(state) };
  }),

  moveInnerNode: (id, pos) => set(state => {
    if (!state.innerCanvas) return {};
    const nodes = state.innerCanvas.nodes.map(x =>
      x.id === id ? { ...x, position: pos } : x);
    return { innerCanvas: { ...state.innerCanvas, nodes }, ...recordInner(state) };
  }),

  moveInnerNodes: updates => set(state => {
    if (!state.innerCanvas || updates.length === 0) return {};
    const byId = new Map(updates.map(u => [u.id, u.pos]));
    const nodes = state.innerCanvas.nodes.map(x =>
      byId.has(x.id) ? { ...x, position: byId.get(x.id)! } : x);
    return { innerCanvas: { ...state.innerCanvas, nodes }, ...recordInner(state) };
  }),

  addInnerEdge: (source, target) => set(state => {
    if (!state.innerCanvas) return {};
    if (state.innerCanvas.edges.some(e => e.source === source && e.target === target)) return {};
    return {
      innerCanvas: {
        ...state.innerCanvas,
        edges: [...state.innerCanvas.edges, { source, target }],
      },
      innerError: null,
      ...recordInner(state),
    };
  }),

  removeInnerNode: id => set(state => {
    if (!state.innerCanvas) return {};
    const nodes = state.innerCanvas.nodes.filter(x => x.id !== id);
    const edges = state.innerCanvas.edges.filter(e => e.source !== id && e.target !== id);
    if (nodes.length === state.innerCanvas.nodes.length && edges.length === state.innerCanvas.edges.length)
      return {};
    return { innerCanvas: { nodes, edges }, innerError: null, ...recordInner(state) };
  }),

  removeInnerNodes: ids => set(state => {
    if (!state.innerCanvas || ids.length === 0) return {};
    const drop = new Set(ids);
    const nodes = state.innerCanvas.nodes.filter(x => !drop.has(x.id));
    const edges = state.innerCanvas.edges.filter(e => !drop.has(e.source) && !drop.has(e.target));
    if (nodes.length === state.innerCanvas.nodes.length && edges.length === state.innerCanvas.edges.length)
      return {};
    return { innerCanvas: { nodes, edges }, innerError: null, ...recordInner(state) };
  }),

  removeInnerEdge: (source, target) => set(state => {
    if (!state.innerCanvas) return {};
    const edges = state.innerCanvas.edges.filter(e => !(e.source === source && e.target === target));
    if (edges.length === state.innerCanvas.edges.length) return {};
    return { innerCanvas: { ...state.innerCanvas, edges }, innerError: null, ...recordInner(state) };
  }),

  undoInner: () => set(state => {
    if (!state.innerCanvas || state.innerPast.length === 0) return {};
    const prev = state.innerPast[state.innerPast.length - 1];
    return {
      innerCanvas: prev,
      innerPast: state.innerPast.slice(0, -1),
      innerFuture: [state.innerCanvas, ...state.innerFuture].slice(0, HISTORY_LIMIT),
      innerError: null,
    };
  }),

  redoInner: () => set(state => {
    if (!state.innerCanvas || state.innerFuture.length === 0) return {};
    const next = state.innerFuture[0];
    return {
      innerCanvas: next,
      innerFuture: state.innerFuture.slice(1),
      innerPast: [...state.innerPast, state.innerCanvas].slice(-HISTORY_LIMIT),
      innerError: null,
    };
  }),

  copyInnerSelection: ids => set(state => {
    if (!state.innerCanvas || ids.length === 0) return {};
    const keep = new Set(ids);
    const nodes = state.innerCanvas.nodes.filter(n => keep.has(n.id));
    const edges = state.innerCanvas.edges.filter(e => keep.has(e.source) && keep.has(e.target));
    if (nodes.length === 0) return {};
    return { innerClipboard: { nodes: deepClone(nodes), edges: deepClone(edges) } };
  }),

  pasteInner: () => set(state => {
    if (!state.innerCanvas || !state.innerClipboard || state.innerClipboard.nodes.length === 0) return {};
    let counter = state.innerCounter;
    const idMap = new Map<string, string>();
    const names = new Set(
      state.innerCanvas.nodes.filter((x): x is InnerStateNode => x.kind === 'state').map(x => x.name));
    const newNodes: InnerNode[] = state.innerClipboard.nodes.map(orig => {
      counter += 1;
      const pos = { x: orig.position.x + 40, y: orig.position.y + 40 };
      if (orig.kind === 'state') {
        const nid = `state:__paste${counter}`;
        idMap.set(orig.id, nid);
        let name = `${orig.name}-copy`;
        while (names.has(name)) name = `${name}-copy`;
        names.add(name);
        return { ...orig, id: nid, name, initial: false, position: pos };
      }
      const nid = `transition:__paste${counter}`;
      idMap.set(orig.id, nid);
      return { ...orig, id: nid, position: pos };
    });
    const newEdges: InnerEdge[] = state.innerClipboard.edges.map(e => ({
      source: idMap.get(e.source)!, target: idMap.get(e.target)!,
    }));
    return {
      innerCanvas: {
        nodes: [...state.innerCanvas.nodes, ...newNodes],
        edges: [...state.innerCanvas.edges, ...newEdges],
      },
      innerCounter: counter,
      innerError: null,
      ...recordInner(state),
    };
  }),

  arrangeInner: () => set(state => {
    if (!state.innerCanvas) return {};
    return {
      innerCanvas: { ...state.innerCanvas, nodes: layoutInner(state.innerCanvas) },
      ...recordInner(state),
    };
  }),
}));
