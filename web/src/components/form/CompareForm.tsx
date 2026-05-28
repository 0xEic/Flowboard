// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Compare. The `a`/`b` input ports adopt
// `inputType`; the op set depends on it — relational operators for numeric /
// char / bool types, and string-specific operators for String.

const PRIMS = [
  'flowboard::Bool', 'flowboard::Char',
  'flowboard::UInt8', 'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Float', 'flowboard::Double', 'flowboard::String',
];
const STRING = 'flowboard::String';
const BOOL = 'flowboard::Bool';
const shortTag = (t: string) => t.replace('flowboard::', '');

const RELATIONAL_OPS: { value: string; label: string }[] = [
  { value: '>',  label: '>' },
  { value: '>=', label: '≥' },
  { value: '<',  label: '<' },
  { value: '<=', label: '≤' },
  { value: '==', label: '=' },
  { value: '!=', label: '≠' },
];
// Bool ordering isn't meaningful — only equality makes sense.
const BOOL_OPS = RELATIONAL_OPS.filter(o => o.value === '==' || o.value === '!=');
const STRING_OPS: { value: string; label: string }[] = [
  { value: 'equals',     label: 'equals' },
  { value: 'notEquals',  label: 'not equals' },
  { value: 'contains',   label: 'contains' },
  { value: 'startsWith', label: 'starts with' },
  { value: 'endsWith',   label: 'ends with' },
];

function opsFor(inputType: string) {
  if (inputType === STRING) return STRING_OPS;
  if (inputType === BOOL)   return BOOL_OPS;
  return RELATIONAL_OPS;
}

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

type Cfg = { inputType?: string; op?: string };

export function CompareForm({ nodeId }: { nodeId: string }) {
  const graph   = useGraphStore(s => s.graph);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const inputType = cfg.inputType ?? 'flowboard::Double';
  const ops = opsFor(inputType);
  const op = ops.some(o => o.value === cfg.op) ? cfg.op! : ops[0].value;

  // Switching inputType changes the a/b port types → route through request so
  // the type-change modal can warn about connected edges. When the op set also
  // changes (e.g. numeric → String), reset op to the new default in the same
  // patch so we never leave an op that the new type can't parse.
  const setInputType = (next: string) => {
    const nextOps = opsFor(next);
    const keepOp = nextOps.some(o => o.value === op) ? op : nextOps[0].value;
    request(nodeId, { ...cfg, inputType: next, op: keepOp });
  };

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type (a, b)</label>
        <select className={fieldCls} value={inputType} onChange={e => setInputType(e.target.value)}>
          {PRIMS.map(t => <option key={t} value={t}>{shortTag(t)}</option>)}
        </select>
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Operator</label>
        <select className={fieldCls} value={op} onChange={e => update(nodeId, { ...cfg, op: e.target.value })}>
          {ops.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
        </select>
        <div className="text-slate-500 text-[11px] mt-1">out = (a {op} b)</div>
      </div>
    </div>
  );
}
