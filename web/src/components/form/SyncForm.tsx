// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Synchronize (barrier / join). Lets you set
// the shared value type (any registered primitive or onboardapi struct, fetched
// via /api/synchronize_types), the number of in/out pairs, and the emission order
// of the value outputs (between beforeOutput and afterOutput) with up/down controls.

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

type Cfg = { inputType?: string; inputCount?: number; order?: number[] };

// Normalise `order` into a permutation of [0, n): keep valid existing entries
// (in their current order), then append any missing indices.
function reconcileOrder(order: number[] | undefined, n: number): number[] {
  const kept = (order ?? []).filter(i => Number.isInteger(i) && i >= 0 && i < n);
  const seen = new Set(kept);
  for (let i = 0; i < n; i++) if (!seen.has(i)) kept.push(i);
  return kept;
}

export function SyncForm({ nodeId }: { nodeId: string }) {
  const graph   = useGraphStore(s => s.graph);
  const types   = useGraphStore(s => s.synchronizeTypes);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);

  // Partition into primitives (flowboard::*) and onboardapi struct types.
  const grouped = useMemo(() => {
    const prim: string[] = [];
    const struct: string[] = [];
    for (const t of types) {
      if (t.startsWith('flowboard::')) prim.push(t);
      else struct.push(t);
    }
    return { prim, struct };
  }, [types]);

  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const inputType = cfg.inputType ?? 'flowboard::Double';
  const n = Math.max(1, Math.min(64, Number(cfg.inputCount ?? 2)));
  const order = reconcileOrder(cfg.order, n);

  // inputType changes every port's type → route through the type-change modal.
  const setType = (next: string) => request(nodeId, { ...cfg, inputType: next });

  const setCount = (next: number) => {
    const cnt = Math.max(1, Math.min(64, Math.trunc(next) || 1));
    update(nodeId, { ...cfg, inputCount: cnt, order: reconcileOrder(cfg.order, cnt) });
  };

  const move = (pos: number, dir: -1 | 1) => {
    const j = pos + dir;
    if (j < 0 || j >= order.length) return;
    const next = [...order];
    [next[pos], next[j]] = [next[j], next[pos]];
    update(nodeId, { ...cfg, order: next });
  };

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Value type (all in/out pairs)</label>
        <select className={fieldCls} value={inputType} onChange={e => setType(e.target.value)}>
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
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Number of inputs</label>
        <input
          className={fieldCls}
          type="number"
          min={1}
          max={64}
          value={n}
          onChange={e => setCount(Number(e.target.value))}
        />
        <div className="text-slate-500 text-[11px] mt-1">
          Waits until every input has a value, then emits them together.
        </div>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Output emission order</label>
        <div className="flex flex-col gap-1">
          <div className="text-[11px] text-emerald-300 px-1">▸ beforeOutput</div>
          {order.map((idx, pos) => (
            <div key={idx} className="flex items-center gap-1 bg-slate-900/40 border border-slate-700/60 rounded px-1.5 py-0.5">
              <span className="text-slate-500 text-[10px] w-4 text-right">{pos + 1}.</span>
              <span className="flex-1 text-slate-200 text-xs font-mono">out{idx}</span>
              <button className="text-slate-500 hover:text-slate-200 px-1" title="Move up"
                onClick={() => move(pos, -1)} disabled={pos === 0}>▲</button>
              <button className="text-slate-500 hover:text-slate-200 px-1" title="Move down"
                onClick={() => move(pos, 1)} disabled={pos === order.length - 1}>▼</button>
            </div>
          ))}
          <div className="text-[11px] text-sky-300 px-1">▸ afterOutput</div>
        </div>
      </div>
    </div>
  );
}
