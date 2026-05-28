// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

interface Props {
  nodeId: string;
}

type Cfg = { outputType?: string; value?: unknown; autoTriggerOnInit?: boolean };

const INT_TYPES = new Set([
  'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Int32', 'flowboard::UInt32',
  'flowboard::Int16', 'flowboard::UInt16',
  'flowboard::UInt8',
]);
const FLOAT_TYPES = new Set(['flowboard::Double', 'flowboard::Float']);

const PRIMITIVES = [
  'flowboard::Bool', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int16',
  'flowboard::UInt16', 'flowboard::UInt8', 'flowboard::Double',
  'flowboard::Float', 'flowboard::String', 'flowboard::Char',
];

// ConstantSource is primitive-only (struct values come from Factory.* nodes).
export function ConstantSourceForm({ nodeId }: Props) {
  const graph                   = useGraphStore(s => s.graph);
  const updateNodeConfig        = useGraphStore(s => s.updateNodeConfig);
  const requestUpdateNodeConfig = useGraphStore(s => s.requestUpdateNodeConfig);
  const revertNodeConfig        = useGraphStore(s => s.revertNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg  = (node.config ?? {}) as Cfg;
  const outputType = cfg.outputType ?? 'flowboard::String';
  const autoTrigger = cfg.autoTriggerOnInit ?? true;

  const defaultFor = (t: string): unknown => {
    if (t === 'flowboard::Bool')   return false;
    if (t === 'flowboard::String' || t === 'flowboard::Char') return '';
    return 0;  // every numeric type
  };

  const setOutputType = (next: string) => {
    requestUpdateNodeConfig(nodeId, { ...cfg, outputType: next, value: defaultFor(next) });
  };
  const setValue = (next: unknown) => updateNodeConfig(nodeId, { ...cfg, value: next });

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Output type</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          value={outputType}
          onChange={e => setOutputType(e.target.value)}
        >
          {PRIMITIVES.map(t => <option key={t} value={t}>{t}</option>)}
        </select>
      </div>

      <ValueEditor outputType={outputType} value={cfg.value} onChange={setValue} />

      <label className="flex items-center gap-2 text-xs text-slate-300">
        <input
          type="checkbox"
          checked={autoTrigger}
          onChange={e => updateNodeConfig(nodeId, { ...cfg, autoTriggerOnInit: e.target.checked })}
        />
        Auto-trigger on init
      </label>

      <button
        className="text-xs px-2 py-1 rounded border border-slate-600 hover:bg-slate-700"
        onClick={() => revertNodeConfig(nodeId)}
      >
        Revert
      </button>
    </div>
  );
}

function ValueEditor(props: {
  outputType: string;
  value: unknown;
  onChange: (next: unknown) => void;
}) {
  const { outputType, value, onChange } = props;
  const label = <label className="block text-slate-300 text-xs mb-1">Value</label>;
  const cls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs';

  if (outputType === 'flowboard::Bool') {
    return (
      <div>
        {label}
        <select className={cls} value={value === true ? 'true' : 'false'}
          onChange={e => onChange(e.target.value === 'true')}>
          <option value="false">false</option>
          <option value="true">true</option>
        </select>
      </div>
    );
  }

  if (INT_TYPES.has(outputType) || FLOAT_TYPES.has(outputType)) {
    const isFloat = FLOAT_TYPES.has(outputType);
    const parse = isFloat
      ? (s: string) => (s === '' ? 0 : Number(s))
      : (s: string) => (s === '' ? 0 : Math.trunc(Number(s)));
    return (
      <div>
        {label}
        <input
          className={cls}
          type="number"
          step={isFloat ? 'any' : '1'}
          value={value === undefined || value === null ? '' : String(value)}
          onChange={e => onChange(parse(e.target.value))}
        />
      </div>
    );
  }

  if (outputType === 'flowboard::Char') {
    return (
      <div>
        {label}
        <input className={cls} type="text" maxLength={1}
          value={typeof value === 'string' ? value : ''}
          onChange={e => onChange(e.target.value.slice(0, 1))} />
      </div>
    );
  }

  // String
  return (
    <div>
      {label}
      <input className={cls} type="text"
        value={typeof value === 'string' ? value : ''}
        onChange={e => onChange(e.target.value)} />
    </div>
  );
}
