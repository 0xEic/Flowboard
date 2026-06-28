// SPDX-License-Identifier: MIT
//
// Bespoke properties form for Python.Script nodes. Lets the user add/remove
// typed input and output ports (which become the node's canvas links), set the
// python executable, and edit the script in a CodeMirror editor that can be
// popped into a larger modal. All edits write straight to node config; the
// canvas handles (via nodePorts) and the editor autocomplete update live.
// Removing a port that has connected edges asks for confirmation first, then
// deletes those edges.
import { useState, useEffect } from 'react';
import { useGraphStore } from '../../store/graph_store';
import { TypeSelect } from './TypeSelect';
import { PythonCodeEditor, type PortSpec } from './PythonCodeEditor';

type Props = { nodeId: string };
type Cfg = {
  inputs?: PortSpec[];
  outputs?: PortSpec[];
  script?: string;
  pythonExe?: string;
};

const cls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs';

export function PythonScriptForm({ nodeId }: Props) {
  const graph      = useGraphStore(s => s.graph);
  const update     = useGraphStore(s => s.updateNodeConfig);
  const removeEdge = useGraphStore(s => s.removeEdge);
  const node       = graph?.nodes.find(n => n.id === nodeId);

  const [expanded, setExpanded] = useState(false);
  // Row awaiting remove-confirm, keyed "inputs:<i>" / "outputs:<i>".
  const [confirm, setConfirm] = useState<string | null>(null);

  // Esc closes the expand modal.
  useEffect(() => {
    if (!expanded) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') setExpanded(false); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [expanded]);

  if (!node) return null;
  const cfg: Cfg = (node.config ?? {}) as Cfg;
  const inputs  = Array.isArray(cfg.inputs)  ? cfg.inputs  : [];
  const outputs = Array.isArray(cfg.outputs) ? cfg.outputs : [];
  const script  = typeof cfg.script === 'string' ? cfg.script : '';

  const writePorts = (which: 'inputs' | 'outputs', next: PortSpec[]) => {
    setConfirm(null);  // any add/edit/remove dismisses a pending remove-confirm
    update(nodeId, { ...cfg, [which]: next });
  };

  const addPort = (which: 'inputs' | 'outputs') => {
    const list = which === 'inputs' ? inputs : outputs;
    writePorts(which, [...list, { name: '', type: 'flowboard::Double' }]);
  };

  const setPort = (which: 'inputs' | 'outputs', i: number, patch: Partial<PortSpec>) => {
    const list = (which === 'inputs' ? inputs : outputs).map((p, idx) =>
      idx === i ? { ...p, ...patch } : p);
    writePorts(which, list);
  };

  // Full edge records whose source or target is this node's named port.
  const edgesForPort = (portName: string) =>
    (graph?.edges ?? []).filter(e =>
      e.from === `${nodeId}.${portName}` || e.to === `${nodeId}.${portName}`);

  const doRemove = (which: 'inputs' | 'outputs', i: number) => {
    const list = which === 'inputs' ? inputs : outputs;
    const name = (list[i]?.name ?? '').trim();
    if (name) {
      // removeEdge records one undo entry per edge (the store has no batch
      // remove), so removing a port with N edges takes N+1 undo steps.
      for (const e of edgesForPort(name)) {
        const [src, ...srest] = e.from.split('.');
        const [tgt, ...trest] = e.to.split('.');
        removeEdge(src, srest.join('.'), tgt, trest.join('.'));
      }
    }
    writePorts(which, list.filter((_, idx) => idx !== i));
    setConfirm(null);
  };

  const requestRemove = (which: 'inputs' | 'outputs', i: number) => {
    const list = which === 'inputs' ? inputs : outputs;
    const name = (list[i]?.name ?? '').trim();
    const n = name ? edgesForPort(name).length : 0;
    if (n === 0) doRemove(which, i);     // nothing wired → remove immediately
    else setConfirm(`${which}:${i}`);    // has edges → confirm first
  };

  const renderRows = (which: 'inputs' | 'outputs', list: PortSpec[]) => (
    <div className="space-y-1">
      {list.map((p, i) => {
        const key  = `${which}:${i}`;
        const name = (p.name ?? '').trim();
        const n    = name ? edgesForPort(name).length : 0;
        if (confirm === key) {
          return (
            <div key={key}
                 className="flex items-center gap-2 text-[11px] bg-slate-900 border border-amber-700 rounded px-2 py-1">
              <span className="flex-1 text-amber-300">
                Remove '{name || '(unnamed)'}'? deletes {n} edge(s)
              </span>
              <button type="button" className="text-slate-300 hover:text-slate-100"
                      onClick={() => setConfirm(null)}>Cancel</button>
              <button type="button" className="text-rose-400 hover:text-rose-300"
                      onClick={() => doRemove(which, i)}>Remove</button>
            </div>
          );
        }
        return (
          <div key={key} className="flex items-center gap-1">
            <input
              className={cls + ' flex-1'}
              placeholder="name"
              value={p.name ?? ''}
              spellCheck={false}
              onChange={e => setPort(which, i, { name: e.target.value })}
            />
            <div className="flex-1">
              <TypeSelect
                value={p.type ?? 'flowboard::Double'}
                onChange={t => setPort(which, i, { type: t })}
              />
            </div>
            <button
              type="button"
              className="px-1.5 text-slate-400 hover:text-rose-400"
              title="Remove port"
              onClick={() => requestRemove(which, i)}
            >✕</button>
          </div>
        );
      })}
      <button
        type="button"
        className="text-[11px] text-sky-400 hover:text-sky-200"
        onClick={() => addPort(which)}
      >+ Add {which === 'inputs' ? 'input' : 'output'}</button>
    </div>
  );

  return (
    <div className="space-y-3">
      <div>
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Inputs</div>
        {renderRows('inputs', inputs)}
      </div>

      <div>
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Outputs</div>
        {renderRows('outputs', outputs)}
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Python executable</label>
        <input
          className={cls}
          placeholder="(default: python / python3, or $FLOWBOARD_PYTHON)"
          value={cfg.pythonExe ?? ''}
          spellCheck={false}
          onChange={e => update(nodeId, { ...cfg, pythonExe: e.target.value })}
        />
      </div>

      <div>
        <div className="flex items-center justify-between mb-1">
          <label className="text-slate-300 text-xs">Script</label>
          <button
            type="button"
            className="text-[11px] text-sky-400 hover:text-sky-200"
            title="Open editor in a bigger view"
            onClick={() => setExpanded(true)}
          >⤢ Expand</button>
        </div>
        <PythonCodeEditor
          value={script}
          inputs={inputs}
          outputs={outputs}
          height="280px"
          onChange={v => update(nodeId, { ...cfg, script: v })}
        />
        <div className="text-[10px] text-slate-500 mt-0.5">
          Define on_input(port, value); call emit('out', value). New ports take
          effect on Apply &amp; Reload.
        </div>
      </div>

      {expanded && (
        <div
          className="fixed inset-0 z-50 bg-black/60 flex items-center justify-center p-6"
          onMouseDown={() => setExpanded(false)}
        >
          <div
            className="bg-slate-800 border border-slate-600 rounded-lg shadow-xl w-[80vw] max-w-5xl flex flex-col"
            onMouseDown={e => e.stopPropagation()}
          >
            <div className="flex items-center justify-between px-3 py-2 border-b border-slate-700">
              <span className="text-sm font-semibold">{node.id} — script</span>
              <button
                type="button"
                className="text-slate-400 hover:text-slate-100 text-lg leading-none"
                title="Close"
                onClick={() => setExpanded(false)}
              >✕</button>
            </div>
            <div className="p-3">
              <PythonCodeEditor
                value={script}
                inputs={inputs}
                outputs={outputs}
                height="70vh"
                onChange={v => update(nodeId, { ...cfg, script: v })}
              />
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
