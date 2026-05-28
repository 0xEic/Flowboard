// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Threshold. `in` adopts `inputType`, `out`
// is Bool. The comparison `value` input is typed to match `inputType`.

const PRIMS = [
  'flowboard::Bool', 'flowboard::Char',
  'flowboard::UInt8', 'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Float', 'flowboard::Double', 'flowboard::String',
];
const shortTag = (t: string) => t.replace('flowboard::', '');

const OPS: { value: string; label: string }[] = [
  { value: '>',  label: '>' },
  { value: '>=', label: '≥' },
  { value: '<',  label: '<' },
  { value: '<=', label: '≤' },
  { value: '==', label: '=' },
  { value: '!=', label: '≠' },
];

type Kind = 'text' | 'number' | 'boolean';
function kindOf(tag: string): Kind {
  if (tag === 'flowboard::Bool') return 'boolean';
  if (tag === 'flowboard::String' || tag === 'flowboard::Char') return 'text';
  return 'number';
}
function defaultFor(kind: Kind): unknown {
  if (kind === 'boolean') return false;
  if (kind === 'number') return 0;
  return '';
}
function coerce(raw: string, kind: Kind): unknown {
  if (kind === 'number') return raw === '' ? 0 : Number(raw);
  if (kind === 'boolean') return raw === 'true';
  return raw;
}

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

type Cfg = { inputType?: string; op?: string; value?: unknown };

export function ThresholdForm({ nodeId }: { nodeId: string }) {
  const graph   = useGraphStore(s => s.graph);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const inputType = cfg.inputType ?? 'flowboard::Double';
  const op = OPS.some(o => o.value === cfg.op) ? cfg.op! : OPS[0].value;
  const kind = kindOf(inputType);

  // inputType drives the `in` port type → route through request so the
  // type-change modal can warn about connected edges. Reset the threshold value
  // to one of the new type so it never carries a wrong-typed leftover.
  const setInputType = (next: string) =>
    request(nodeId, { ...cfg, inputType: next, value: defaultFor(kindOf(next)) });

  const valStr = cfg.value === null || cfg.value === undefined ? '' : String(cfg.value);

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type (in)</label>
        <select className={fieldCls} value={inputType} onChange={e => setInputType(e.target.value)}>
          {PRIMS.map(t => <option key={t} value={t}>{shortTag(t)}</option>)}
        </select>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Operator</label>
        <select className={fieldCls} value={op} onChange={e => update(nodeId, { ...cfg, op: e.target.value })}>
          {OPS.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
        </select>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Threshold value</label>
        {kind === 'boolean' ? (
          <select
            className={fieldCls}
            value={valStr === 'true' ? 'true' : 'false'}
            onChange={e => update(nodeId, { ...cfg, value: coerce(e.target.value, 'boolean') })}
          >
            <option value="false">false</option>
            <option value="true">true</option>
          </select>
        ) : (
          <input
            className={fieldCls}
            type={kind === 'number' ? 'number' : 'text'}
            step={kind === 'number' ? 'any' : undefined}
            value={valStr}
            onChange={e => update(nodeId, { ...cfg, value: coerce(e.target.value, kind) })}
            spellCheck={false}
          />
        )}
        <div className="text-slate-500 text-[11px] mt-1">out = (in {op} value)</div>
      </div>
    </div>
  );
}
