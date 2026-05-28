// SPDX-License-Identifier: MIT
export type PortDescriptor = {
  name: string;
  typeTag: string;
  direction: 'input' | 'output';
};

export type CatalogEntry = {
  typeName: string;
  inputs:  PortDescriptor[];
  outputs: PortDescriptor[];
  configSchema?:   Record<string, unknown>;
  configDefaults?: Record<string, unknown>;
};

export type GraphNodeJson = {
  id: string;
  type: string;
  config?: Record<string, unknown>;
  position?: { x: number; y: number };
  // Canvas size (frontend-only; the engine loader ignores these). Currently
  // used by resizable Debug.* nodes to persist their dimensions.
  width?: number;
  height?: number;
  // Expanded port-group keys ("in:Op" / "out:Op") for OnboardAPI-style nodes
  // whose ports are grouped into collapsible sections (frontend-only; the
  // engine ignores it). Absent = every group collapsed (the default).
  expandedGroups?: string[];
};

// A Flow.Group node stores its inner subgraph here (config.group). Frontend-only:
// expandGroups() inlines it into the flat graph before the engine ever sees it.
// Groups nest (inner nodes may themselves be Flow.Group). Boundary ports are
// represented by Group.Input / Group.Output terminal nodes inside `nodes`, whose
// config is { portName, typeTag }.
export type GroupConfig = {
  nodes: GraphNodeJson[];
  edges: GraphEdgeJson[];
};

export type GraphEdgeJson = {
  from: string;
  to:   string;
  capacity?: number;
  policy?: 'block' | 'drop_oldest';
};

export type GraphJson = {
  version: string;
  // `authoring` holds the un-flattened (grouped) graph for round-tripping. On
  // save the top-level nodes/edges are flattened (engine-runnable) and the
  // grouped graph is embedded here; the engine ignores metadata, the web
  // restores from it. Recursive by design.
  metadata?: { name?: string; description?: string; authoring?: GraphJson };
  nodes: GraphNodeJson[];
  edges: GraphEdgeJson[];
};

export type StatsJson = {
  edges: { from: string; to: string; pushed: number; dropped: number }[];
};

export type DiscoveryEndpoint = {
  interfaceType: string;
  direction: 'Service' | 'Client';
  serviceName: string;
  domainId: number;
  source: 'graph' | 'live' | 'both';
  hostName?: string;
  procName?: string;
  processId?: number;
  actorId?: string;
};

export type DiscoveryResponse = {
  domain: number;
  source: string;
  endpoints: DiscoveryEndpoint[];
};