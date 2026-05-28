// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';
import { TypeSelect } from './TypeSelect';

type Cfg = {
  keyType?: string;
  valueType?: string;
  elementTypeTag?: string;
  orderBy?: string;
  emitOnEmpty?: boolean;
};

export function KeyValueAccumulatorForm({ nodeId }: { nodeId: string }) {
  const graph            = useGraphStore(s => s.graph);
  const updateNodeConfig = useGraphStore(s => s.updateNodeConfig);
  const revertNodeConfig = useGraphStore(s => s.revertNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as Cfg;
  const patch = (next: Partial<Cfg>) => updateNodeConfig(nodeId, { ...cfg, ...next });

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Key type</label>
        <TypeSelect value={cfg.keyType ?? ''} onChange={t => patch({ keyType: t })} />
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Value type</label>
        <TypeSelect value={cfg.valueType ?? ''} onChange={t => patch({ valueType: t })} />
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Element type hint (optional)</label>
        <input
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs font-mono"
          type="text"
          value={cfg.elementTypeTag ?? ''}
          placeholder="(echoed on list items; usually = value type)"
          onChange={e => patch({ elementTypeTag: e.target.value })}
        />
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Order</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          value={cfg.orderBy ?? 'insertion'}
          onChange={e => patch({ orderBy: e.target.value })}
        >
          <option value="insertion">insertion</option>
          <option value="key">key</option>
        </select>
      </div>
      <label className="flex items-center gap-2 text-xs text-slate-300">
        <input
          type="checkbox"
          checked={cfg.emitOnEmpty ?? true}
          onChange={e => patch({ emitOnEmpty: e.target.checked })}
        />
        Emit on no-op remove
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
