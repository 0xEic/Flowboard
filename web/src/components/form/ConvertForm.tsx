// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// Property-based config for Transform.Convert. The available "mode" and its
// fields depend on the chosen input/output primitive types.

const PRIMS = [
  'flowboard::Bool', 'flowboard::Char',
  'flowboard::UInt8', 'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Float', 'flowboard::Double', 'flowboard::String',
];
const INTEGER = new Set([
  'flowboard::UInt8', 'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int64', 'flowboard::UInt64',
]);
const NUMERIC = new Set([...INTEGER, 'flowboard::Float', 'flowboard::Double']);
const BOOL = 'flowboard::Bool';
const STRING = 'flowboard::String';
const FLOAT = new Set(['flowboard::Float', 'flowboard::Double']);
const shortTag = (t: string) => t.replace('flowboard::', '');

type OutKind = 'text' | 'number' | 'boolean';
function outKind(tag: string): OutKind {
  if (tag === BOOL) return 'boolean';
  if (tag === STRING || tag === 'flowboard::Char') return 'text';
  return 'number';
}
function coerce(raw: string, kind: OutKind): unknown {
  if (kind === 'number')  return raw === '' ? 0 : Number(raw);
  if (kind === 'boolean') return raw === 'true';
  return raw;
}

const MODE_LABELS: Record<string, string> = {
  convert: 'Convert (type-aware)',
  lookup: 'Lookup (enum table)',
  scale: 'Scale & offset',
  threshold: 'Threshold → Bool',
  boolmap: 'Boolean map',
  case: 'Text transform',
};

// Modes valid for a given (input, output) type pair.
function availableModes(inT: string, outT: string): string[] {
  const modes = ['convert'];
  if (INTEGER.has(inT)) modes.push('lookup');
  if (NUMERIC.has(inT) && NUMERIC.has(outT)) modes.push('scale');
  if (NUMERIC.has(inT) && outT === BOOL) modes.push('threshold');
  if (inT === BOOL) modes.push('boolmap');
  if (inT === STRING && outT === STRING) modes.push('case');
  return modes;
}

type Mapping = { when: number; value: unknown };
type Cfg = {
  inputType?: string; outputType?: string; mode?: string; template?: string; decimals?: number;
  mappings?: Mapping[]; fallback?: unknown;
  scale?: number; offset?: number; clamp?: boolean; clampMin?: number; clampMax?: number;
  threshold?: number; compareOp?: string;
  trueValue?: unknown; falseValue?: unknown;
  textTransform?: string;
};

const fieldCls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';
const numCls = 'w-24 bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600';

function ValueInput(props: { kind: OutKind; value: unknown; onChange: (v: unknown) => void }) {
  const str = props.value === null || props.value === undefined ? '' : String(props.value);
  if (props.kind === 'boolean') {
    return (
      <select className={fieldCls} value={str === 'true' ? 'true' : 'false'}
        onChange={e => props.onChange(coerce(e.target.value, 'boolean'))}>
        <option value="false">false</option>
        <option value="true">true</option>
      </select>
    );
  }
  return (
    <input className={fieldCls} type={props.kind === 'number' ? 'number' : 'text'}
      step={props.kind === 'number' ? 'any' : undefined} value={str}
      onChange={e => props.onChange(coerce(e.target.value, props.kind))} spellCheck={false} />
  );
}

export function ConvertForm({ nodeId }: { nodeId: string }) {
  const graph   = useGraphStore(s => s.graph);
  const update  = useGraphStore(s => s.updateNodeConfig);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const inputType  = cfg.inputType  ?? 'flowboard::Double';
  const outputType = cfg.outputType ?? 'flowboard::String';
  const modes = availableModes(inputType, outputType);
  const mode = modes.includes(cfg.mode ?? 'convert') ? (cfg.mode ?? 'convert') : 'convert';
  const kind = outKind(outputType);
  const mappings = cfg.mappings ?? [];
  const num = (v: number | undefined, d: number) => (v === undefined ? d : v);

  const set     = (patch: Partial<Cfg>) => update(nodeId, { ...cfg, ...patch });
  const setType = (patch: Partial<Cfg>) => request(nodeId, { ...cfg, ...patch });
  const setMappings = (m: Mapping[]) => set({ mappings: m });

  return (
    <div className="space-y-3">
      <div className="grid grid-cols-2 gap-2">
        <div>
          <label className="block text-slate-300 text-xs mb-1">Input type</label>
          <select className={fieldCls} value={inputType} onChange={e => setType({ inputType: e.target.value })}>
            {PRIMS.map(t => <option key={t} value={t}>{shortTag(t)}</option>)}
          </select>
        </div>
        <div>
          <label className="block text-slate-300 text-xs mb-1">Output type</label>
          <select className={fieldCls} value={outputType} onChange={e => setType({ outputType: e.target.value })}>
            {PRIMS.map(t => <option key={t} value={t}>{shortTag(t)}</option>)}
          </select>
        </div>
      </div>

      {modes.length > 1 && (
        <div>
          <label className="block text-slate-300 text-xs mb-1">Mode</label>
          <select className={fieldCls} value={mode} onChange={e => set({ mode: e.target.value })}>
            {modes.map(m => <option key={m} value={m}>{MODE_LABELS[m]}</option>)}
          </select>
        </div>
      )}

      {/* convert: template (->String) + decimals (float input) */}
      {mode === 'convert' && outputType === STRING && (
        <>
          <div>
            <label className="block text-slate-300 text-xs mb-1">Template</label>
            <input className={fieldCls} value={cfg.template ?? '{value}'} placeholder="{value}"
              onChange={e => set({ template: e.target.value })} spellCheck={false} />
            <div className="text-slate-500 text-[11px] mt-1">{'{value}'} is replaced by the converted value.</div>
          </div>
          {FLOAT.has(inputType) && (
            <div>
              <label className="block text-slate-300 text-xs mb-1">Decimals</label>
              <input className={numCls} type="number" min={0} max={15} value={num(cfg.decimals, 6)}
                onChange={e => set({ decimals: Math.max(0, Math.trunc(Number(e.target.value))) })} />
            </div>
          )}
        </>
      )}

      {/* scale: out = in*scale + offset, optional clamp */}
      {mode === 'scale' && (
        <div className="space-y-2">
          <div className="flex items-center gap-2 text-xs">
            <span className="text-slate-400">out =</span>
            <span className="text-slate-300">in ×</span>
            <input className={numCls} type="number" step="any" value={num(cfg.scale, 1)}
              onChange={e => set({ scale: Number(e.target.value) })} />
            <span className="text-slate-300">+</span>
            <input className={numCls} type="number" step="any" value={num(cfg.offset, 0)}
              onChange={e => set({ offset: Number(e.target.value) })} />
          </div>
          <label className="flex items-center gap-2 text-xs text-slate-300">
            <input type="checkbox" checked={!!cfg.clamp} onChange={e => set({ clamp: e.target.checked })} />
            Clamp result
          </label>
          {cfg.clamp && (
            <div className="flex items-center gap-2 text-xs">
              <span className="text-slate-400">min</span>
              <input className={numCls} type="number" step="any" value={num(cfg.clampMin, 0)}
                onChange={e => set({ clampMin: Number(e.target.value) })} />
              <span className="text-slate-400">max</span>
              <input className={numCls} type="number" step="any" value={num(cfg.clampMax, 0)}
                onChange={e => set({ clampMax: Number(e.target.value) })} />
            </div>
          )}
        </div>
      )}

      {/* threshold: out = in OP value */}
      {mode === 'threshold' && (
        <div className="flex items-center gap-2 text-xs">
          <span className="text-slate-400">out = in</span>
          <select className="bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
            value={cfg.compareOp ?? 'gt'} onChange={e => set({ compareOp: e.target.value })}>
            <option value="gt">&gt;</option>
            <option value="gte">&ge;</option>
            <option value="lt">&lt;</option>
            <option value="lte">&le;</option>
            <option value="eq">=</option>
            <option value="neq">&ne;</option>
          </select>
          <input className={numCls} type="number" step="any" value={num(cfg.threshold, 0)}
            onChange={e => set({ threshold: Number(e.target.value) })} />
        </div>
      )}

      {/* boolmap: true/false -> values (typed by output) */}
      {mode === 'boolmap' && (
        <div className="space-y-2">
          <div className="flex items-center gap-2">
            <span className="text-emerald-300 text-xs w-12">true →</span>
            <div className="flex-1"><ValueInput kind={kind} value={cfg.trueValue} onChange={v => set({ trueValue: v })} /></div>
          </div>
          <div className="flex items-center gap-2">
            <span className="text-rose-300 text-xs w-12">false →</span>
            <div className="flex-1"><ValueInput kind={kind} value={cfg.falseValue} onChange={v => set({ falseValue: v })} /></div>
          </div>
        </div>
      )}

      {/* case: text transform */}
      {mode === 'case' && (
        <div>
          <label className="block text-slate-300 text-xs mb-1">Transform</label>
          <select className={fieldCls} value={cfg.textTransform ?? 'none'} onChange={e => set({ textTransform: e.target.value })}>
            <option value="none">none</option>
            <option value="upper">UPPERCASE</option>
            <option value="lower">lowercase</option>
            <option value="trim">trim whitespace</option>
          </select>
        </div>
      )}

      {/* lookup: enum table + fallback */}
      {mode === 'lookup' && (
        <div className="space-y-2">
          <label className="block text-slate-300 text-xs">Lookup table (input = … → output)</label>
          {mappings.length === 0 && <div className="text-slate-500 text-[11px]">No entries. Add one below.</div>}
          {mappings.map((m, i) => (
            <div key={i} className="flex items-center gap-1">
              <input className={numCls} type="number" value={String(m.when)}
                onChange={e => setMappings(mappings.map((x, j) => j === i ? { ...x, when: Math.trunc(Number(e.target.value)) } : x))} />
              <span className="text-slate-500">→</span>
              <div className="flex-1">
                <ValueInput kind={kind} value={m.value}
                  onChange={v => setMappings(mappings.map((x, j) => j === i ? { ...x, value: v } : x))} />
              </div>
              <button className="text-rose-400 hover:text-rose-300 px-1" title="Remove"
                onClick={() => setMappings(mappings.filter((_, j) => j !== i))}>✕</button>
            </div>
          ))}
          <button className="px-2 py-1 text-xs rounded bg-slate-700 hover:bg-slate-600"
            onClick={() => setMappings([...mappings, { when: 0, value: coerce('', kind) }])}>+ Entry</button>
          <div className="pt-1">
            <label className="block text-slate-300 text-xs mb-1">Fallback (unmapped inputs)</label>
            <ValueInput kind={kind} value={cfg.fallback} onChange={v => set({ fallback: v })} />
          </div>
        </div>
      )}
    </div>
  );
}
