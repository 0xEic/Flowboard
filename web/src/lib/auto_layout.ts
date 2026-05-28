// SPDX-License-Identifier: MIT
import type { GraphJson, CatalogEntry } from '../api/types';
import { nodePorts } from './node_ports';

// Layered (Sugiyama-style) auto-layout for the dataflow graph: assigns nodes to
// left-to-right layers by longest-path depth, orders nodes within each layer with
// a barycenter heuristic to reduce edge crossings (so few links pass over nodes),
// and stacks them with gaps so nothing overlaps. Returns new positions for every
// node; apply via updateNodePositions (one undo step).

export type NodePos = { id: string; pos: { x: number; y: number } };

const H_GAP = 120;   // horizontal gap between layers
const V_GAP = 36;    // vertical gap between nodes in a layer
const DEFAULT_W = 200;

// A node's rendered size. Prefer the real measured size from React Flow
// (`measured`) — onboard nodes group ports into collapsible sections, so their
// DOM height/width can't be derived from the raw port count. Fall back to the
// node's explicit size, then to a port-count estimate for never-rendered nodes.
function nodeSize(
  node: { id: string; type: string; config?: Record<string, unknown>; width?: number; height?: number },
  catalog: CatalogEntry[],
  measured?: Map<string, { w: number; h: number }>,
): { w: number; h: number } {
  const m = measured?.get(node.id);
  if (m && m.w > 0 && m.h > 0) return m;
  if (node.width && node.height) return { w: node.width, h: node.height };
  const { inputs, outputs } = nodePorts(node, catalog);
  const rows = Math.max(inputs.length, outputs.length, 1);
  return { w: DEFAULT_W, h: 46 + rows * 24 + 12 };
}

export function autoLayout(
  graph: GraphJson,
  catalog: CatalogEntry[],
  measured?: Map<string, { w: number; h: number }>,
): NodePos[] {
  const nodes = graph.nodes;
  if (nodes.length === 0) return [];

  const ids = nodes.map(n => n.id);
  const idSet = new Set(ids);
  const indexOf = new Map(ids.map((id, i) => [id, i]));

  // Edge adjacency by node id (strip the ".port" suffix). Ignore self-loops and
  // edges referencing unknown nodes.
  const out = new Map<string, string[]>();
  const inc = new Map<string, string[]>();
  ids.forEach(id => { out.set(id, []); inc.set(id, []); });
  for (const e of graph.edges) {
    const s = e.from.split('.')[0];
    const t = e.to.split('.')[0];
    if (s === t || !idSet.has(s) || !idSet.has(t)) continue;
    out.get(s)!.push(t);
    inc.get(t)!.push(s);
  }

  // Longest-path layering via bounded relaxation (sources at layer 0). The
  // iteration cap keeps it terminating even if the graph has cycles.
  const layer = new Map<string, number>(ids.map(id => [id, 0]));
  for (let iter = 0; iter < nodes.length; iter++) {
    let changed = false;
    for (const e of graph.edges) {
      const s = e.from.split('.')[0];
      const t = e.to.split('.')[0];
      if (s === t || !idSet.has(s) || !idSet.has(t)) continue;
      const want = layer.get(s)! + 1;
      if (want > layer.get(t)!) { layer.set(t, want); changed = true; }
    }
    if (!changed) break;
  }

  // Bucket node ids into layers, seeded in stable input order.
  const maxLayer = Math.max(...ids.map(id => layer.get(id)!));
  const layers: string[][] = Array.from({ length: maxLayer + 1 }, () => []);
  for (const id of ids) layers[layer.get(id)!].push(id);

  // Crossing reduction: a few alternating sweeps, ordering each layer by the
  // average position (barycenter) of its neighbours in the adjacent layer.
  const orderPos = new Map<string, number>();
  const reindex = () => layers.forEach(L => L.forEach((id, i) => orderPos.set(id, i)));
  reindex();
  const barycenter = (id: string, neigh: Map<string, string[]>): number => {
    const ns = neigh.get(id)!;
    if (ns.length === 0) return orderPos.get(id)!;
    let sum = 0;
    for (const n of ns) sum += orderPos.get(n)!;
    return sum / ns.length;
  };
  for (let sweep = 0; sweep < 4; sweep++) {
    const downward = sweep % 2 === 0;
    const range = downward
      ? [...Array(layers.length).keys()]
      : [...Array(layers.length).keys()].reverse();
    for (const li of range) {
      const neigh = downward ? inc : out;
      const stable = new Map(layers[li].map((id, i) => [id, i]));
      layers[li].sort((a, b) => {
        const ba = barycenter(a, neigh);
        const bb = barycenter(b, neigh);
        if (ba === bb) return stable.get(a)! - stable.get(b)!;  // keep stable on ties
        return ba - bb;
      });
      reindex();
    }
  }

  // Coordinate assignment. Each layer occupies a column; x advances by that
  // layer's widest node. Within a layer, nodes stack vertically by their height.
  // Layers are vertically centred around a shared midline so the result looks
  // balanced rather than top-anchored.
  const sizeById = new Map<string, { w: number; h: number }>(
    nodes.map(n => [n.id, nodeSize(n, catalog, measured)]),
  );
  const layerHeights = layers.map(L =>
    L.reduce((sum, id) => sum + sizeById.get(id)!.h, 0) + Math.max(0, L.length - 1) * V_GAP);
  const tallest = Math.max(0, ...layerHeights);

  const result: NodePos[] = [];
  let x = 0;
  for (let li = 0; li < layers.length; li++) {
    const L = layers[li];
    const layerW = Math.max(DEFAULT_W, ...L.map(id => sizeById.get(id)!.w));
    let y = (tallest - layerHeights[li]) / 2;  // centre this layer
    // Within-layer stable id order already set; keep insertion order tie-break.
    const sorted = [...L].sort((a, b) =>
      (orderPos.get(a)! - orderPos.get(b)!) || (indexOf.get(a)! - indexOf.get(b)!));
    for (const id of sorted) {
      const sz = sizeById.get(id)!;
      result.push({ id, pos: { x: Math.round(x), y: Math.round(y) } });
      y += sz.h + V_GAP;
    }
    x += layerW + H_GAP;
  }
  return result;
}
