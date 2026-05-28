// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Throttle. The valid input types are
// whatever the runtime registered (primitives + onboardapi structs), fetched
// via /api/throttle_types — so the dropdown mirrors Sinks.Log. `in`/`out` both
// adopt `inputType`.

type Cfg = { inputType?: string; minPeriodMs?: number };

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

export function ThrottleForm({ nodeId }: { nodeId: string }) {
  const graph         = useGraphStore(s => s.graph);
  const throttleTypes = useGraphStore(s => s.throttleTypes);
  const update        = useGraphStore(s => s.updateNodeConfig);
  const request       = useGraphStore(s => s.requestUpdateNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);

  // Partition into primitives (flowboard::*) and onboardapi struct types.
  const grouped = useMemo(() => {
    const prim: string[] = [];
    const struct: string[] = [];
    for (const t of throttleTypes) {
      if (t.startsWith('flowboard::')) prim.push(t);
      else struct.push(t);
    }
    return { prim, struct };
  }, [throttleTypes]);

  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const minPeriodMs = cfg.minPeriodMs ?? 100;

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type (in, out)</label>
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
        {throttleTypes.length === 0 && (
          <div className="text-slate-500 text-[11px] mt-1 italic">
            No throttle types known yet — backend may be starting up.
          </div>
        )}
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Min period (ms)</label>
        <input
          className={fieldCls}
          type="number"
          min={0}
          value={minPeriodMs}
          onChange={e => update(nodeId, { ...cfg, minPeriodMs: Math.max(0, Math.trunc(Number(e.target.value))) })}
        />
        <div className="text-slate-500 text-[11px] mt-1">
          Forwards at most one value per this interval; values arriving sooner are dropped.
        </div>
      </div>
    </div>
  );
}
