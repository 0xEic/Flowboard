// SPDX-License-Identifier: MIT
//
// Pure flatten pass for Flow.Group nodes. A group is a frontend-only container;
// the engine only runs flat graphs, so before sending/exporting we inline every
// group into the top-level node/edge list. Groups nest, so this recurses.
//
// Boundary ports are terminal nodes inside the group:
//   Group.Input  (config.portName, typeTag): output handle "out" → an INPUT on
//                the group; external edges into <gid>.<portName> are redirected
//                to whatever "out" feeds inside (fan-out allowed).
//   Group.Output (config.portName, typeTag): input handle "in" → an OUTPUT on
//                the group; external edges out of <gid>.<portName> are redirected
//                from whatever feeds "in" inside (single producer).
//
// Inner node ids are namespaced "<groupId>/<innerId>" so they stay unique, and
// recursively "<outer>/<inner>/<leaf>".

import type { GraphJson, GraphNodeJson, GraphEdgeJson, GroupConfig } from '../api/types';

const GROUP_TYPE = 'Flow.Group';
const INPUT_TYPE = 'Group.Input';
const OUTPUT_TYPE = 'Group.Output';

const isTerminal = (n: GraphNodeJson) => n.type === INPUT_TYPE || n.type === OUTPUT_TYPE;

// "node.port.path" -> ["node", "port.path"]; "node" -> ["node", ""].
function splitRef(ref: string): [string, string] {
  const i = ref.indexOf('.');
  return i < 0 ? [ref, ''] : [ref.slice(0, i), ref.slice(i + 1)];
}
const joinRef = (node: string, port: string) => (port ? `${node}.${port}` : node);

function groupOf(node: GraphNodeJson): GroupConfig {
  const g = (node.config as { group?: GroupConfig } | undefined)?.group;
  return { nodes: g?.nodes ?? [], edges: g?.edges ?? [] };
}
const portNameOf = (t: GraphNodeJson) =>
  (t.config?.portName as string | undefined) ?? t.id;

type Scope = { nodes: GraphNodeJson[]; edges: GraphEdgeJson[] };

// Inline a single group `g` within a scope (nodes + edges that live alongside it).
function inlineGroup(g: GraphNodeJson, nodes: GraphNodeJson[], edges: GraphEdgeJson[]): Scope {
  const gid = g.id;
  const group = groupOf(g);
  // Expand nested groups first so the inner graph is group-free.
  const inner = expandScope(group.nodes, group.edges);

  const termIds = new Set(inner.nodes.filter(isTerminal).map(t => t.id));
  const ns = (innerId: string) => `${gid}/${innerId}`;

  // Boundary maps. Edges between two terminals (a pure pass-through) are not
  // supported in v1 and are skipped.
  const inputAlias = new Map<string, string[]>();   // portName -> namespaced consumer endpoints
  const outputAlias = new Map<string, string>();    // portName -> namespaced producer endpoint
  for (const t of inner.nodes) {
    if (t.type === INPUT_TYPE) {
      const consumers = inner.edges
        .filter(e => splitRef(e.from)[0] === t.id)
        .map(e => splitRef(e.to))
        .filter(([cn]) => !termIds.has(cn))
        .map(([cn, cp]) => joinRef(ns(cn), cp));
      inputAlias.set(portNameOf(t), [...(inputAlias.get(portNameOf(t)) ?? []), ...consumers]);
    } else if (t.type === OUTPUT_TYPE) {
      const producers = inner.edges
        .filter(e => splitRef(e.to)[0] === t.id)
        .map(e => splitRef(e.from))
        .filter(([pn]) => !termIds.has(pn))
        .map(([pn, pp]) => joinRef(ns(pn), pp));
      if (producers.length > 1)
        throw new Error(`group "${gid}" output "${portNameOf(t)}" has multiple sources`);
      if (producers.length === 1) outputAlias.set(portNameOf(t), producers[0]);
    }
  }

  // Namespaced real inner nodes (drop terminals).
  const nsNodes = inner.nodes
    .filter(n => !isTerminal(n))
    .map(n => ({ ...n, id: ns(n.id) }));

  // Inner edges not touching a terminal, namespaced.
  const nsInnerEdges: GraphEdgeJson[] = inner.edges
    .filter(e => !termIds.has(splitRef(e.from)[0]) && !termIds.has(splitRef(e.to)[0]))
    .map(e => {
      const [fn, fp] = splitRef(e.from);
      const [tn, tp] = splitRef(e.to);
      return { ...e, from: joinRef(ns(fn), fp), to: joinRef(ns(tn), tp) };
    });

  // Rewrite scope edges that cross this group's boundary.
  const rewritten: GraphEdgeJson[] = [];
  for (const e of edges) {
    const [fn, fp] = splitRef(e.from);
    const [tn, tp] = splitRef(e.to);
    if (fn !== gid && tn !== gid) { rewritten.push(e); continue; }
    let froms = [e.from];
    let tos = [e.to];
    if (fn === gid) {
      const prod = outputAlias.get(fp);
      if (!prod) continue;            // dangling group output → drop the edge
      froms = [prod];
    }
    if (tn === gid) {
      const cons = inputAlias.get(tp);
      if (!cons || cons.length === 0) continue;  // dangling group input → drop
      tos = cons;
    }
    for (const f of froms) for (const t of tos) rewritten.push({ ...e, from: f, to: t });
  }

  return {
    nodes: nodes.filter(n => n.id !== gid).concat(nsNodes),
    edges: rewritten.concat(nsInnerEdges),
  };
}

// Fully inline every group in a scope.
function expandScope(nodes: GraphNodeJson[], edges: GraphEdgeJson[]): Scope {
  let curNodes = nodes;
  let curEdges = edges;
  // Inline groups one at a time until none remain (handles group→group edges
  // across successive passes).
  for (;;) {
    const g = curNodes.find(n => n.type === GROUP_TYPE);
    if (!g) break;
    ({ nodes: curNodes, edges: curEdges } = inlineGroup(g, curNodes, curEdges));
  }
  return { nodes: curNodes, edges: curEdges };
}

// Public: return a flat graph with all Flow.Group nodes inlined.
export function expandGroups(graph: GraphJson): GraphJson {
  const { nodes, edges } = expandScope(graph.nodes, graph.edges);
  return { ...graph, nodes, edges };
}

// Resolve an editor port reference ("<node>.<port>" in `scope`, under the
// group-path `prefix`, e.g. "outer/") to the engine tap key it maps to after
// flattening. A normal node's output keeps its (prefixed) name; a group's
// OUTPUT port resolves to its inner producer's namespaced key, recursively
// through nesting. Used so live values display under group boundary ports even
// though the engine only taps the flattened inner producers.
export function resolveTapKey(
  ref: string,
  prefix: string,
  scope: { nodes: GraphNodeJson[]; edges: GraphEdgeJson[] },
): string {
  const [nodeId, port] = splitRef(ref);
  const node = scope.nodes.find(n => n.id === nodeId);
  if (node && node.type === GROUP_TYPE) {
    const grp = groupOf(node);
    const term = grp.nodes.find(n => n.type === OUTPUT_TYPE && portNameOf(n) === port);
    if (term) {
      const inner = grp.edges.find(e => splitRef(e.to)[0] === term.id);
      if (inner) return resolveTapKey(inner.from, `${prefix}${nodeId}/`, grp);
    }
  }
  return `${prefix}${ref}`;
}

// Frontend-only node types the engine doesn't know about. Group/terminal nodes
// are already removed by expandGroups; Note nodes are pure canvas annotations.
const FRONTEND_ONLY_TYPES = new Set([GROUP_TYPE, INPUT_TYPE, OUTPUT_TYPE, 'Note']);

// Build the dual-format save artifact: top-level nodes/edges flattened so the
// engine can run the file directly, with the grouped authoring graph embedded
// under metadata.authoring so the web can restore groups on load. Throws if a
// group boundary is invalid (e.g. multi-source output).
export function toSavedFile(root: GraphJson, name: string): GraphJson {
  const flat = stripFrontendOnly(expandGroups(root));
  // Embedded authoring copy carries the name but never a nested authoring key.
  const rootMeta = { ...(root.metadata ?? {}) };
  delete rootMeta.authoring;
  const authoring: GraphJson = { ...root, metadata: { ...rootMeta, name } };
  return { ...flat, metadata: { ...(flat.metadata ?? {}), name, authoring } };
}

// Inverse of toSavedFile for loading: prefer the embedded grouped authoring
// graph (restores groups/notes); fall back to the flat graph for plain files.
export function fromSavedFile(g: GraphJson): GraphJson {
  return g.metadata?.authoring ?? g;
}

// Drop frontend-only nodes (and any edges touching them) so the graph is safe
// to send to the engine. Run after expandGroups.
export function stripFrontendOnly(graph: GraphJson): GraphJson {
  const dropped = new Set(graph.nodes.filter(n => FRONTEND_ONLY_TYPES.has(n.type)).map(n => n.id));
  if (dropped.size === 0) return graph;
  return {
    ...graph,
    nodes: graph.nodes.filter(n => !dropped.has(n.id)),
    edges: graph.edges.filter(e => !dropped.has(splitRef(e.from)[0]) && !dropped.has(splitRef(e.to)[0])),
  };
}
