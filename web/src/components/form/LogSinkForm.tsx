// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

interface Props {
  nodeId: string;
}

type Cfg = { inputType?: string; prefix?: string; path?: string };

export function LogSinkForm({ nodeId }: Props) {
  const graph                   = useGraphStore(s => s.graph);
  const logTypes                = useGraphStore(s => s.logTypes);
  const updateNodeConfig        = useGraphStore(s => s.updateNodeConfig);
  const requestUpdateNodeConfig = useGraphStore(s => s.requestUpdateNodeConfig);
  const revertNodeConfig        = useGraphStore(s => s.revertNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);
  const cfg  = (node?.config ?? {}) as Cfg;

  // Group the dropdown: primitives first, then struct types alphabetically.
  // The server already sorts, so we only need to partition.
  const grouped = useMemo(() => {
    const prim:   string[] = [];
    const struct: string[] = [];
    for (const t of logTypes) {
      if (t.startsWith('flowboard::')) prim.push(t);
      else                                struct.push(t);
    }
    return { prim, struct };
  }, [logTypes]);

  if (!node) return null;

  // inputType changes go through the request path (modal confirmation);
  // prefix/path apply immediately.
  const patch = (next: Partial<Cfg>) => {
    const merged = { ...cfg, ...next };
    if ('inputType' in next && next.inputType !== cfg.inputType) {
      requestUpdateNodeConfig(nodeId, merged);
    } else {
      updateNodeConfig(nodeId, merged);
    }
  };

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          value={cfg.inputType ?? ''}
          onChange={e => patch({ inputType: e.target.value })}
        >
          <option value="" disabled>Select a type…</option>
          {grouped.prim.length > 0 && (
            <optgroup label="Primitives">
              {grouped.prim.map(t => <option key={t} value={t}>{t}</option>)}
            </optgroup>
          )}
          {grouped.struct.length > 0 && (
            <optgroup label="OnboardAPI types">
              {grouped.struct.map(t => <option key={t} value={t}>{t}</option>)}
            </optgroup>
          )}
        </select>
        {logTypes.length === 0 && (
          <div className="text-slate-500 text-[11px] mt-1 italic">
            No log types known yet — backend may be starting up.
          </div>
        )}
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Prefix</label>
        <input
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          type="text"
          value={cfg.prefix ?? ''}
          placeholder="value"
          onChange={e => patch({ prefix: e.target.value })}
        />
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">File path</label>
        <input
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs font-mono"
          type="text"
          value={cfg.path ?? ''}
          placeholder="(empty = log to default spdlog sink)"
          onChange={e => patch({ path: e.target.value })}
        />
        <div className="text-slate-500 text-[11px] mt-1 leading-snug">
          Lines append to this file (created if missing). Leave empty to log
          through the global spdlog logger (stdout by default).
        </div>
      </div>

      <div>
        <button
          className="text-xs px-2 py-1 rounded border border-slate-600 hover:bg-slate-700"
          onClick={() => revertNodeConfig(nodeId)}
        >
          Revert
        </button>
      </div>
    </div>
  );
}
