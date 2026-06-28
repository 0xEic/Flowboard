// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Synchronize (barrier / join). Each in/out
// pair has its own type (inputTypes[i]); the "Number of inputs" control grows or
// shrinks that list. The valid type set comes from /api/synchronize_types. The
// emission-order list reorders the value outputs between before/afterOutput.

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

type Cfg = { inputType?: string; inputTypes?: string[]; inputCount?: number; order?: number[] };

// Normalise `order` into a permutation of [0, n).
function reconcileOrder(order: number[] | undefined, n: number): number[] {
  const kept = (order ?? []).filter(i => Number.isInteger(i) && i >= 0 && i < n);
  const seen = new Set(kept);
  for (let i = 0; i < n; i++) if (!seen.has(i)) kept.push(i);
  return kept;
}

// Resolve the per-pair type list, back-filling from the scalar inputType so old
// configs (inputType + inputCount only) still render one type per pair.
function resolveTypes(cfg: Cfg): string[] {
  const fallback = cfg.inputType || 'flowboard::Double';
  if (Array.isArray(cfg.inputTypes) && cfg.inputTypes.length > 0) {
    return cfg.inputTypes.slice(0, 64).map(t => (typeof t === 'string' && t) ? t : fallback);
  }
  const n = Math.max(1, Math.min(64, Number(cfg.inputCount ?? 2)));
  return Array.from({ length: n }, () => fallback);
}

export function SyncForm({ nodeId }: { nodeId: string }) {
  const graph  = useGraphStore(s => s.graph);
  const types  = useGraphStore(s => s.synchronizeTypes);
  const update = useGraphStore(s => s.updateNodeConfig);
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
  const inputTypes = resolveTypes(cfg);
  const n = inputTypes.length;
  const order = reconcileOrder(cfg.order, n);

  // Always write the canonical inputTypes array, keeping inputCount in sync for
  // any reader that still uses it. Per-pair type edits apply directly (a wired
  // edge whose type no longer matches simply renders red).
  const setTypeAt = (i: number, t: string) => {
    const next = inputTypes.slice();
    next[i] = t;
    update(nodeId, { ...cfg, inputTypes: next, inputCount: next.length });
  };

  const setCount = (raw: number) => {
    const cnt = Math.max(1, Math.min(64, Math.trunc(raw) || 1));
    const next = inputTypes.slice(0, cnt);
    while (next.length < cnt) next.push('flowboard::Double');
    update(nodeId, { ...cfg, inputTypes: next, inputCount: cnt, order: reconcileOrder(cfg.order, cnt) });
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
        <label className="block text-slate-300 text-xs mb-1">Per-input types</label>
        <div className="flex flex-col gap-1">
          {/* key={i} is intentional: rows are only ever appended/removed at the
              tail (setCount) — there is no reorder within this list. */}
          {inputTypes.map((t, i) => (
            <div key={i} className="flex items-center gap-2">
              <span className="text-slate-400 text-xs font-mono w-20 shrink-0">in{i}/out{i}</span>
              <select className={fieldCls} value={t} onChange={e => setTypeAt(i, e.target.value)}>
                {grouped.prim.length > 0 && (
                  <optgroup label="Primitives">
                    {grouped.prim.map(o => <option key={o} value={o}>{o.replace('flowboard::', '')}</option>)}
                  </optgroup>
                )}
                {grouped.struct.length > 0 && (
                  <optgroup label="OnboardAPI types">
                    {grouped.struct.map(o => <option key={o} value={o}>{o}</option>)}
                  </optgroup>
                )}
                {!grouped.prim.includes(t) && !grouped.struct.includes(t) && (
                  <option value={t}>{t}</option>
                )}
              </select>
            </div>
          ))}
        </div>
        <div className="text-slate-500 text-[11px] mt-1">
          Each in/out pair carries its own type. New ports take effect on Apply &amp; Reload.
          The <span className="font-mono">forceOutput</span> input emits each output's current value
          (or its inline default on the canvas node) plus afterOutput — for firing a Cmd/config from a button.
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
