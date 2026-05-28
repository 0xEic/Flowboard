// SPDX-License-Identifier: MIT
import { useEffect } from 'react';
import { TopBar } from './components/TopBar';
import { Sidebar } from './components/Sidebar';
import { GraphCanvas } from './components/GraphCanvas';
import { MachineCanvas } from './components/MachineCanvas';
import { Inspector } from './components/Inspector';
import { LogTail } from './components/LogTail';
import { TypeChangeModal } from './components/TypeChangeModal';
import { useGraphStore } from './store/graph_store';
import { nodePorts } from './lib/node_ports';
import { resolveTapKey } from './lib/group_compile';

export function App() {
  const refreshCatalog  = useGraphStore(s => s.refreshCatalog);
  const refreshLogTypes = useGraphStore(s => s.refreshLogTypes);
  const refreshThrottleTypes = useGraphStore(s => s.refreshThrottleTypes);
  const refreshSynchronizeTypes = useGraphStore(s => s.refreshSynchronizeTypes);
  const refreshGraph    = useGraphStore(s => s.refreshGraph);
  const refreshStats    = useGraphStore(s => s.refreshStats);
  const startWs        = useGraphStore(s => s.startWs);
  const stopWs         = useGraphStore(s => s.stopWs);
  const subscribe      = useGraphStore(s => s.subscribe);
  const graph          = useGraphStore(s => s.graph);
  const catalog        = useGraphStore(s => s.catalog);
  const groupStack     = useGraphStore(s => s.groupStack);
  const editingMachineId = useGraphStore(s => s.editingMachineId);

  // Initial load + WS lifecycle
  useEffect(() => {
    refreshCatalog().catch(console.error);
    refreshLogTypes().catch(console.error);
    refreshThrottleTypes().catch(console.error);
    refreshSynchronizeTypes().catch(console.error);
    refreshGraph().catch(console.error);
    refreshStats().catch(console.error);
    startWs();
    const iv = setInterval(() => refreshStats().catch(console.error), 2000);
    return () => { stopWs(); clearInterval(iv); };
  }, []);

  // Subscribe to every output port of every node so the canvas Liveview
  // shows values even on unwired outputs. The WS hub dedups subscribes per
  // connection, so resubscribing on graph changes is safe and idempotent.
  useEffect(() => {
    if (!graph || catalog.length === 0) return;
    // Engine taps are namespaced by the active group path; a group's exposed
    // output port maps to its inner producer's tap. resolveTapKey handles both.
    const prefix = groupStack.length ? groupStack.map(g => g.id).join('/') + '/' : '';
    const scope = { nodes: graph.nodes, edges: graph.edges };
    const ports = new Set<string>();
    for (const n of graph.nodes) {
      const { outputs } = nodePorts(n, catalog);
      for (const p of outputs) ports.add(resolveTapKey(`${n.id}.${p.name}`, prefix, scope));
    }
    if (ports.size) subscribe([...ports]);
  }, [graph, catalog, groupStack]);

  return (
    <div className="h-screen flex flex-col">
      <TopBar />
      <div className="flex-1 flex overflow-hidden">
        {editingMachineId ? (
          <MachineCanvas />
        ) : (
          <>
            <Sidebar />
            <GraphCanvas />
            <Inspector />
          </>
        )}
      </div>
      <LogTail />
      <TypeChangeModal />
    </div>
  );
}