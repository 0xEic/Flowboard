// SPDX-License-Identifier: MIT
import { useRef } from 'react';
import { useGraphStore } from '../store/graph_store';
import { toSavedFile, fromSavedFile } from '../lib/group_compile';
import { SHOW_INTRO_EVENT } from './IntroTour';
import { openHelp } from '../help/HelpPanel';
import type { GraphJson } from '../api/types';

export function TopBar() {
  const graph   = useGraphStore(s => s.graph);
  const reload  = useGraphStore(s => s.reload);
  const setGraph = useGraphStore(s => s.setGraph);
  const pause   = useGraphStore(s => s.pause);
  const resume  = useGraphStore(s => s.resume);
  const running = useGraphStore(s => s.running);
  const editingMachineId = useGraphStore(s => s.editingMachineId);
  const closeMachine     = useGraphStore(s => s.closeMachine);
  const closeSubmachine  = useGraphStore(s => s.closeSubmachine);
  const machineStack     = useGraphStore(s => s.machineStack);
  const groupStack       = useGraphStore(s => s.groupStack);
  const loadGraph        = useGraphStore(s => s.loadGraph);
  const fileInputRef     = useRef<HTMLInputElement>(null);

  // Auto-arrange the active graph: layered left-to-right layout with no
  // overlapping nodes and crossing-reduced ordering. The canvas performs the
  // layout (it has React Flow's measured node sizes); applied as a single undo
  // step (Ctrl+Z reverts it), then fits the view to the result.
  const onAutoArrange = () => useGraphStore.getState().requestAutoArrange();

  // Save As: dual-format download. The top-level nodes/edges are flattened so
  // `flowboard.exe <file>` runs the file directly (no "unknown node type
  // 'Flow.Group'"), while the grouped authoring graph is embedded under
  // metadata.authoring so loading the file back restores groups/notes. The
  // in-session store keeps the grouped version for editing.
  const saveConfigAs = () => {
    const root = useGraphStore.getState().getRootGraph();
    if (!root) return;
    const name = window.prompt('Save config as:', root.metadata?.name ?? 'config');
    if (!name) return;  // cancelled or empty
    let file: GraphJson;
    try { file = toSavedFile(root, name); }
    catch (e) { alert(`Cannot export: ${e instanceof Error ? e.message : String(e)}`); return; }
    // Persist the new name on the (grouped) store graph at the top level.
    if (useGraphStore.getState().groupStack.length === 0)
      setGraph({ ...root, metadata: { ...(root.metadata ?? {}), name } });
    const blob = new Blob([JSON.stringify(file, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${name}.json`;
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  };

  // Load a saved config from disk: restores the grouped authoring graph when the
  // file embeds one (dual-format), else loads the flat graph as-is.
  const onLoadFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const f = e.target.files?.[0];
    e.target.value = '';  // allow re-selecting the same file
    if (!f) return;
    const reader = new FileReader();
    reader.onload = () => {
      try { loadGraph(fromSavedFile(JSON.parse(String(reader.result)) as GraphJson)); }
      catch (ex) { alert(`Failed to load config: ${ex instanceof Error ? ex.message : String(ex)}`); }
    };
    reader.readAsText(f);
  };

  // Close group scopes until the stack is `depth` deep (0 = top-level graph).
  const popTo = (depth: number) => {
    let n = useGraphStore.getState().groupStack.length - depth;
    while (n-- > 0) useGraphStore.getState().closeGroup();
  };

  return (
    <div data-tour="topbar" className="flex items-center gap-3 px-4 py-2 bg-slate-800 border-b border-slate-700">
      <div className="font-semibold">Flowboard</div>
      {editingMachineId ? (
        <div className="flex items-center gap-1 text-sm">
          <button
            className="text-slate-400 hover:text-slate-100"
            onClick={() => closeMachine()}
            title="Compile the inner canvas back into the machine and return to the graph"
          >graph</button>
          {/* root machine + nested substate path; intermediate levels pop back up */}
          {[{ label: editingMachineId, depth: 0 },
            ...machineStack.map((m, i) => ({ label: m.stateName, depth: i + 1 }))
          ].map((seg, i, arr) => (
            <span key={seg.depth} className="flex items-center gap-1">
              <span className="text-slate-600">/</span>
              {i < arr.length - 1 ? (
                <button
                  className="text-slate-400 hover:text-slate-100"
                  onClick={() => { while (useGraphStore.getState().machineStack.length > seg.depth) { if (!closeSubmachine()) break; } }}
                  title={`Back to ${seg.label}`}
                >{seg.label}</button>
              ) : (
                <span className="text-sky-300">{seg.label}</span>
              )}
            </span>
          ))}
        </div>
      ) : groupStack.length > 0 ? (
        <div className="flex items-center gap-1 text-sm">
          <button
            className="text-slate-400 hover:text-slate-100"
            onClick={() => popTo(0)}
            title="Back to the top-level graph"
          >graph</button>
          {groupStack.map((s, i) => (
            <span key={s.id} className="flex items-center gap-1">
              <span className="text-slate-600">/</span>
              {i < groupStack.length - 1 ? (
                <button
                  className="text-slate-400 hover:text-slate-100"
                  onClick={() => popTo(i + 1)}
                  title={`Back to ${s.id}`}
                >{s.id}</button>
              ) : (
                <span className="text-sky-300">{s.id}</span>
              )}
            </span>
          ))}
        </div>
      ) : (
        <div className="text-slate-400 text-sm">
          {graph?.metadata?.name ?? '(unnamed)'}
        </div>
      )}
      <div className="ml-auto flex items-center gap-2">
        {editingMachineId ? (
          <button
            className="px-3 py-1 bg-sky-700 hover:bg-sky-600 rounded text-sm"
            onClick={() => {
              const ok = closeMachine();
              if (!ok) alert(useGraphStore.getState().innerError ?? 'state machine has errors');
            }}
          >Done</button>
        ) : (
          <>
            <button
              className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm"
              onClick={onAutoArrange}
              disabled={!graph || graph.nodes.length === 0}
              title="Auto-arrange nodes: layered left-to-right, no overlaps, fewer crossing links (undoable)"
            >Auto-arrange</button>
            <button
              data-tour="apply"
              className="px-3 py-1 bg-sky-700 hover:bg-sky-600 rounded text-sm"
              onClick={async () => {
                const errs = await reload();
                if (errs.length) alert(errs.join('\n'));
              }}
              title="Apply &amp; reload the graph into the engine (Ctrl+S)"
            >Apply &amp; Reload</button>
            <button
              className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm"
              onClick={saveConfigAs}
              disabled={!graph}
              title="Download an engine-runnable JSON (groups embedded for re-editing)"
            >Save Config as…</button>
            <button
              className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm"
              onClick={() => fileInputRef.current?.click()}
              title="Load a config JSON from disk (restores groups if present)"
            >Load…</button>
            <input
              ref={fileInputRef}
              type="file"
              accept="application/json,.json"
              className="hidden"
              onChange={onLoadFile}
            />
            {running
              ? <button className="px-3 py-1 bg-amber-700 hover:bg-amber-600 rounded text-sm" onClick={pause}>Pause</button>
              : <button className="px-3 py-1 bg-emerald-700 hover:bg-emerald-600 rounded text-sm" onClick={resume}>Resume</button>}
            <button
              className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm"
              onClick={() => openHelp()}
              title="Open the in-app node reference"
              aria-label="Open node help"
            >Help</button>
            <button
              className="w-7 h-7 flex items-center justify-center rounded-full bg-slate-700 hover:bg-slate-600 text-sm font-semibold"
              onClick={() => window.dispatchEvent(new Event(SHOW_INTRO_EVENT))}
              title="Show the quick introduction tour"
              aria-label="Show introduction tour"
            >?</button>
          </>
        )}
      </div>
    </div>
  );
}