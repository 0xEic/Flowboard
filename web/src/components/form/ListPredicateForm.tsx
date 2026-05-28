// SPDX-License-Identifier: MIT
import { useEffect, useState } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Property-based editor for the List.Find / List.Filter predicate
// ({ field, op, value }). Replaces the raw-JSON config so users don't need to
// hand-type JSON literals for `value`.

type Cfg = { field?: string; op?: string; value?: unknown };
type Kind = 'text' | 'number' | 'boolean';

const OPS: { v: string; label: string }[] = [
  { v: 'eq',       label: '=  equals' },
  { v: 'neq',      label: '≠  not equal' },
  { v: 'lt',       label: '<  less than' },
  { v: 'lte',      label: '≤  less or equal' },
  { v: 'gt',       label: '>  greater than' },
  { v: 'gte',      label: '≥  greater or equal' },
  { v: 'contains', label: 'contains (text)' },
];

function kindOf(v: unknown): Kind {
  if (typeof v === 'number') return 'number';
  if (typeof v === 'boolean') return 'boolean';
  return 'text';
}

export function ListPredicateForm({ nodeId }: { nodeId: string }) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  const cfg  = (node?.config ?? {}) as Cfg;

  // The value's kind isn't stored in config (the value is just JSON), so track
  // it locally, seeded from the current value and re-synced when the node changes.
  const [kind, setKind] = useState<Kind>(kindOf(cfg.value));
  useEffect(() => { setKind(kindOf(cfg.value)); }, [nodeId]);  // eslint-disable-line react-hooks/exhaustive-deps

  if (!node) return null;
  const op    = cfg.op ?? 'eq';
  const field = cfg.field ?? '';
  const valueStr = cfg.value === null || cfg.value === undefined ? '' : String(cfg.value);

  const set = (patch: Partial<Cfg>) => update(nodeId, { ...cfg, ...patch });

  const coerce = (raw: string, k: Kind): unknown => {
    if (k === 'number')  return raw === '' ? 0 : Number(raw);
    if (k === 'boolean') return raw === 'true';
    return raw;
  };
  const commitValue = (raw: string, k: Kind) => set({ value: coerce(raw, k) });

  const changeKind = (k: Kind) => { setKind(k); commitValue(valueStr, k); };

  const changeOp = (next: string) => {
    // `contains` only makes sense for text; switch the value to text to match.
    if (next === 'contains' && kind !== 'text') { setKind('text'); set({ op: next, value: valueStr }); }
    else set({ op: next });
  };

  const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Field path (optional)</label>
        <input
          className={fieldCls}
          value={field}
          placeholder="e.g. key  ·  value.Label  (empty = whole item)"
          onChange={e => set({ field: e.target.value })}
          spellCheck={false}
        />
        <div className="text-slate-500 text-[11px] mt-1 leading-snug">
          Which field of each list item to test. Leave empty to compare the item itself.
        </div>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Condition</label>
        <select className={fieldCls} value={op} onChange={e => changeOp(e.target.value)}>
          {OPS.map(o => <option key={o.v} value={o.v}>{o.label}</option>)}
        </select>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Compared value</label>
        <div className="flex gap-2">
          <select
            className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600"
            value={kind}
            disabled={op === 'contains'}
            onChange={e => changeKind(e.target.value as Kind)}
            title={op === 'contains' ? "'contains' compares text" : 'Value type'}
          >
            <option value="text">Text</option>
            <option value="number">Number</option>
            <option value="boolean">Boolean</option>
          </select>
          {kind === 'boolean' ? (
            <select
              className={fieldCls}
              value={valueStr === 'true' ? 'true' : 'false'}
              onChange={e => commitValue(e.target.value, 'boolean')}
            >
              <option value="false">false</option>
              <option value="true">true</option>
            </select>
          ) : (
            <input
              className={fieldCls}
              type={kind === 'number' ? 'number' : 'text'}
              step={kind === 'number' ? 'any' : undefined}
              value={valueStr}
              placeholder={kind === 'number' ? '0' : 'value to match'}
              onChange={e => commitValue(e.target.value, kind)}
              spellCheck={false}
            />
          )}
        </div>
        <div className="text-slate-500 text-[11px] mt-1 leading-snug">
          Numbers compare numerically, text lexicographically. The match is found
          when an item's field satisfies the condition against this value.
        </div>
      </div>
    </div>
  );
}
