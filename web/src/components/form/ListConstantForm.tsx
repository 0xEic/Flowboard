// SPDX-License-Identifier: MIT
import { useEffect, useState } from 'react';
import { useGraphStore } from '../../store/graph_store';
import { listInputStructTypes } from '../../lib/extract_ports';

interface Props { nodeId: string }
type Cfg = { elementType?: string; values?: unknown[]; autoTriggerOnInit?: boolean };

const PRIMITIVES = [
  'flowboard::Bool', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int16',
  'flowboard::UInt16', 'flowboard::UInt8', 'flowboard::Double',
  'flowboard::Float', 'flowboard::String', 'flowboard::Char',
];

// List.Constant authors a literal list of any element type (primitive or
// OnboardAPI struct) as a JSON array — the value carries JSON, so no typed ports.
export function ListConstantForm({ nodeId }: Props) {
  const graph            = useGraphStore(s => s.graph);
  const updateNodeConfig = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  const cfg  = (node?.config ?? {}) as Cfg;

  const elementType = cfg.elementType ?? 'flowboard::Double';
  const autoTrigger = cfg.autoTriggerOnInit ?? true;

  // Local text buffer for the JSON array so the user can type freely; commit on
  // valid parse. Re-seed when switching nodes.
  const [text, setText] = useState(() => JSON.stringify(cfg.values ?? [], null, 2));
  const [err, setErr]   = useState<string | null>(null);
  useEffect(() => {
    setText(JSON.stringify((node?.config as Cfg | undefined)?.values ?? [], null, 2));
    setErr(null);
  }, [nodeId]);  // eslint-disable-line react-hooks/exhaustive-deps

  if (!node) return null;

  const structTypes = listInputStructTypes();
  const knownTypes = [...PRIMITIVES, ...structTypes.filter(t => !PRIMITIVES.includes(t))];

  const commitValues = (next: string) => {
    setText(next);
    try {
      const parsed = JSON.parse(next);
      if (!Array.isArray(parsed)) { setErr('Must be a JSON array'); return; }
      setErr(null);
      updateNodeConfig(nodeId, { ...cfg, values: parsed });
    } catch (e) {
      setErr(e instanceof Error ? e.message : 'invalid JSON');
    }
  };

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Element type</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          value={knownTypes.includes(elementType) ? elementType : '__custom'}
          onChange={e => updateNodeConfig(nodeId, { ...cfg, elementType: e.target.value })}
        >
          {knownTypes.map(t => <option key={t} value={t}>{t}</option>)}
          {!knownTypes.includes(elementType) && <option value="__custom">{elementType}</option>}
        </select>
        <div className="text-[10px] text-slate-500 mt-0.5">
          Any primitive or OnboardAPI struct. Drives downstream list element-type checks.
        </div>
      </div>

      <div>
        <label className="block text-slate-300 text-xs mb-1">Values (JSON array)</label>
        <textarea
          className="w-full h-40 font-mono bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600"
          value={text}
          onChange={e => commitValues(e.target.value)}
          spellCheck={false}
          placeholder='[1, 2, 3]  or  [{"Index":0,"Name":"A"}]'
        />
        {err && <div className="text-amber-400 text-[11px] mt-1">{err}</div>}
      </div>

      <label className="flex items-center gap-2 text-xs text-slate-300">
        <input
          type="checkbox"
          checked={autoTrigger}
          onChange={e => updateNodeConfig(nodeId, { ...cfg, autoTriggerOnInit: e.target.checked })}
        />
        Auto-trigger on init
      </label>
    </div>
  );
}
