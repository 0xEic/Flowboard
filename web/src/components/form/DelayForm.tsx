// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Delay. The carried type (in + out) is any
// registered type tag (primitives + onboardapi structs), sourced from the shared
// `logTypes` list — same set TypeSelect uses for Log / ValueDisplay.

type Cfg = { inputType?: string; delayMs?: number };

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

export function DelayForm({ nodeId }: { nodeId: string }) {
  const graph    = useGraphStore(s => s.graph);
  const logTypes = useGraphStore(s => s.logTypes);
  const update   = useGraphStore(s => s.updateNodeConfig);
  const request  = useGraphStore(s => s.requestUpdateNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);

  const grouped = useMemo(() => {
    const prim: string[] = [];
    const struct: string[] = [];
    for (const t of logTypes) {
      if (t.startsWith('flowboard::')) prim.push(t);
      else struct.push(t);
    }
    return { prim, struct };
  }, [logTypes]);

  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const delayMs = cfg.delayMs ?? 1000;

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Type (in, out)</label>
        <select
          className={fieldCls}
          value={cfg.inputType ?? ''}
          onChange={e => request(nodeId, { ...cfg, inputType: e.target.value })}
        >
          <option value="" disabled>Select a type…</option>
          {grouped.prim.length > 0 && (
            <optgroup label="Primitives">
              {grouped.prim.map(t => <option key={t} value={t}>{t.replace('flowboard::', '')}</option>)}
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
            No types known yet — backend may be starting up.
          </div>
        )}
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Delay (ms)</label>
        <input
          className={fieldCls}
          type="number"
          min={0}
          value={delayMs}
          onChange={e => update(nodeId, { ...cfg, delayMs: Math.max(0, Math.trunc(Number(e.target.value))) })}
        />
        <div className="text-slate-500 text-[11px] mt-1">
          Each value is re-emitted on `out` this many milliseconds after it arrives.
        </div>
      </div>
    </div>
  );
}
