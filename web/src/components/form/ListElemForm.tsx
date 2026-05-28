// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';
import { listInputStructTypes } from '../../lib/extract_ports';

interface Props { nodeId: string }
type Cfg = { elementType?: string; count?: number; maxItems?: number };

const PRIMITIVES = [
  'flowboard::Bool', 'flowboard::Int64', 'flowboard::UInt64',
  'flowboard::Int32', 'flowboard::UInt32', 'flowboard::Int16',
  'flowboard::UInt16', 'flowboard::UInt8', 'flowboard::Double',
  'flowboard::Float', 'flowboard::String', 'flowboard::Char',
];

// Shared config form for Transform.List.Build (count item inputs) and
// Transform.List.Accumulate (maxItems cap). Element type can be any primitive
// or OnboardAPI struct; the typed item input(s) re-type to the choice.
export function ListElemForm({ nodeId }: Props) {
  const graph            = useGraphStore(s => s.graph);
  const updateNodeConfig = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;

  const cfg = (node.config ?? {}) as Cfg;
  const isBuild = node.type === 'Transform.List.Build';
  const elementType = cfg.elementType ?? 'flowboard::Double';

  const structTypes = listInputStructTypes();
  const knownTypes = [...PRIMITIVES, ...structTypes.filter(t => !PRIMITIVES.includes(t))];
  const cls = 'w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs';

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Element type</label>
        <select
          className={cls}
          value={knownTypes.includes(elementType) ? elementType : '__custom'}
          onChange={e => updateNodeConfig(nodeId, { ...cfg, elementType: e.target.value })}
        >
          {knownTypes.map(t => <option key={t} value={t}>{t}</option>)}
          {!knownTypes.includes(elementType) && <option value="__custom">{elementType}</option>}
        </select>
        <div className="text-[10px] text-slate-500 mt-0.5">
          Any primitive or OnboardAPI struct. The item input{isBuild ? 's' : ''} re-type to match.
        </div>
      </div>

      {isBuild ? (
        <div>
          <label className="block text-slate-300 text-xs mb-1">Item count</label>
          <input
            className={cls} type="number" min={0} max={64}
            value={Number(cfg.count ?? 2)}
            onChange={e => updateNodeConfig(nodeId, {
              ...cfg, count: Math.max(0, Math.min(64, Math.trunc(Number(e.target.value) || 0))) })}
          />
          <div className="text-[10px] text-slate-500 mt-0.5">Inputs item0..itemN-1, emitted on trigger.</div>
        </div>
      ) : (
        <div>
          <label className="block text-slate-300 text-xs mb-1">Max items (0 = unbounded)</label>
          <input
            className={cls} type="number" min={0}
            value={Number(cfg.maxItems ?? 0)}
            onChange={e => updateNodeConfig(nodeId, {
              ...cfg, maxItems: Math.max(0, Math.trunc(Number(e.target.value) || 0)) })}
          />
          <div className="text-[10px] text-slate-500 mt-0.5">Appends item on arrival; emits on trigger, clears on reset.</div>
        </div>
      )}
    </div>
  );
}
