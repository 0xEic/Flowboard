// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Filter. `in`/`out` adopt `inputType`;
// `closedBehavior` chooses what `out` does when the gate is false — hold the
// last passed value (default) or emit a typed default value.

const PRIMS = [
  'flowboard::Bool', 'flowboard::Char',
  'flowboard::UInt8', 'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Float', 'flowboard::Double', 'flowboard::String',
];
const shortTag = (t: string) => t.replace('flowboard::', '');

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

type Cfg = { inputType?: string; closedBehavior?: string; defaultValue?: unknown };

export function FilterForm({ nodeId }: { nodeId: string }) {
  const graph   = useGraphStore(s => s.graph);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const inputType = cfg.inputType ?? 'flowboard::Double';
  const closedBehavior = cfg.closedBehavior ?? 'hold';
  const kind = kindOf(inputType);

  // inputType drives the in/out port types → route through request so the
  // type-change modal can warn about connected edges. Reset the default value
  // to one of the new type so it never carries a wrong-typed leftover.
  const setInputType = (next: string) =>
    request(nodeId, { ...cfg, inputType: next, defaultValue: defaultFor(kindOf(next)) });

  const defValue = cfg.defaultValue;
  const defStr = defValue === null || defValue === undefined ? '' : String(defValue);

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type (in, out)</label>
        <select className={fieldCls} value={inputType} onChange={e => setInputType(e.target.value)}>
          {PRIMS.map(t => <option key={t} value={t}>{shortTag(t)}</option>)}
        </select>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">When gate is closed</label>
        <select
          className={fieldCls}
          value={closedBehavior}
          onChange={e => update(nodeId, { ...cfg, closedBehavior: e.target.value })}
        >
          <option value="hold">Hold last value</option>
          <option value="default">Emit default value</option>
        </select>
        <div className="text-slate-500 text-[11px] mt-1">
          {closedBehavior === 'default'
            ? 'out is set to the default value while the gate is false.'
            : 'out keeps the last value that passed while the gate was open.'}
        </div>
      </div>

      {closedBehavior === 'default' && (
        <div>
          <label className="block text-slate-300 text-xs mb-1">Default value</label>
          {kind === 'boolean' ? (
            <select
              className={fieldCls}
              value={defStr === 'true' ? 'true' : 'false'}
              onChange={e => update(nodeId, { ...cfg, defaultValue: coerce(e.target.value, 'boolean') })}
            >
              <option value="false">false</option>
              <option value="true">true</option>
            </select>
          ) : (
            <input
              className={fieldCls}
              type={kind === 'number' ? 'number' : 'text'}
              step={kind === 'number' ? 'any' : undefined}
              value={defStr}
              onChange={e => update(nodeId, { ...cfg, defaultValue: coerce(e.target.value, kind) })}
              spellCheck={false}
            />
          )}
        </div>
      )}
    </div>
  );
}
