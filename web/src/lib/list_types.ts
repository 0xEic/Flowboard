// SPDX-License-Identifier: MIT
// Graph-aware element-type inference for flowboard::List ports.
//
// Lists travel on a single opaque `flowboard::List` tag (the engine dispatches
// list ops generically), so the catalog can't distinguish a list of one struct
// from another. This module resolves the *element* type of a list port by
// tracing the graph: producers (Extract on a List field) know it from the field
// descriptors; the single-input list ops (Sort/Filter/GetAt/Find) pass it
// through; Combine carries it from its inputs; MapField re-projects to a field
// type. Everything else (e.g. KeyValueAccumulator's synthetic pairs) resolves to
// '' = unknown/wildcard, which never flags a mismatch.
import { ALL_FIELD_DESCRIPTORS } from '../generated/field_descriptors.generated';
import type { GraphJson, GraphNodeJson } from '../api/types';

function srcOfInput(graph: GraphJson, nodeId: string, inPort: string):
    { id: string; port: string } | undefined {
  const edge = graph.edges.find(e => e.to === `${nodeId}.${inPort}`);
  if (!edge) return undefined;
  const [id, ...rest] = edge.from.split('.');
  return { id, port: rest.join('.') };
}

// Element type tag carried by a list-typed OUTPUT port, or '' if unknown.
export function listElementType(
  graph: GraphJson, nodeId: string, outPort: string, visited = new Set<string>(),
): string {
  const key = `${nodeId}.${outPort}`;
  if (visited.has(key)) return '';        // cycle guard
  visited.add(key);

  const node: GraphNodeJson | undefined = graph.nodes.find(n => n.id === nodeId);
  if (!node) return '';
  const cfg = (node.config ?? {}) as Record<string, unknown>;

  const fromInput = (inPort: string): string => {
    const s = srcOfInput(graph, nodeId, inPort);
    return s ? listElementType(graph, s.id, s.port, visited) : '';
  };

  switch (node.type) {
    case 'Transform.Extract': {
      const it = cfg.inputType as string | undefined;
      const f  = cfg.field as string | undefined;
      if (!it || !f) return '';
      const d = ALL_FIELD_DESCRIPTORS[it]?.find(x => x.name === f);
      return d && d.kind === 'List' ? d.elementTypeTag : '';
    }
    case 'Transform.List.Build':
    case 'Transform.List.Accumulate':
    case 'Transform.List.Constant':
      // These create a list whose element type is chosen in config.
      return (cfg.elementType as string) || '';
    case 'Transform.List.Sort':
    case 'Transform.List.Filter':
      return fromInput('in');
    case 'Transform.List.Combine':
      return fromInput('a') || fromInput('b');
    case 'Transform.List.GetAt':
    case 'Transform.List.Find':
      // The `value` port is a single element (still carried as a list value).
      return outPort === 'value' ? fromInput('in') : '';
    case 'Transform.List.MapField': {
      const elem = fromInput('in');         // upstream element struct
      const f = cfg.field as string | undefined;
      if (!elem || !f) return '';
      const d = ALL_FIELD_DESCRIPTORS[elem]?.find(x => x.name === f);
      return d ? d.elementTypeTag : '';
    }
    default:
      return '';                            // unknown producer → wildcard
  }
}

// The element type a list INPUT effectively requires, or '' if it accepts any.
// Only Combine constrains its inputs: each must match the other (concatenating
// lists of different element types yields a heterogeneous list).
export function expectedListInputElement(
  graph: GraphJson, nodeId: string, inPort: string,
): string {
  const node = graph.nodes.find(n => n.id === nodeId);
  if (!node || node.type !== 'Transform.List.Combine') return '';
  const other = inPort === 'a' ? 'b' : 'a';
  const s = srcOfInput(graph, nodeId, other);
  return s ? listElementType(graph, s.id, s.port) : '';
}

// Short, human-readable element label (drops a leading namespace for brevity).
export function shortElem(tag: string): string {
  const sep = tag.lastIndexOf('::');
  return sep >= 0 ? tag.slice(sep + 2) : tag;
}
