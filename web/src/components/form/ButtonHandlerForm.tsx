// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

interface Props { nodeId: string }
type OutputCfg = { match?: 'byIndex' | 'byName'; index?: number; buttonName?: string; output?: string };

// Input.ButtonHandler config: a list of bool outputs, each matched to a joystick
// button by index or name. (The generic schema form can't edit arrays, so this
// dedicated editor adds/removes/edits the entries.)
export function ButtonHandlerForm({ nodeId }: Props) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;

  const cfg = (node.config ?? {}) as { outputs?: OutputCfg[] };
  const outputs = Array.isArray(cfg.outputs) ? cfg.outputs : [];
  const setOutputs = (next: OutputCfg[]) => update(nodeId, { ...cfg, outputs: next });
  const patch = (i: number, p: Partial<OutputCfg>) =>
    setOutputs(outputs.map((o, j) => (j === i ? { ...o, ...p } : o)));
  const remove = (i: number) => setOutputs(outputs.filter((_, j) => j !== i));
  const add = () => setOutputs([...outputs, { match: 'byIndex', index: 0 }]);

  const cls = 'bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-[11px] outline-none focus:border-sky-600';

  return (
    <div className="space-y-2">
      <div className="text-[10px] text-slate-500">
        Wire EventButton.ButtonIndex / ButtonName / IsSelected to the inputs. Each
        output below emits IsSelected when its button fires.
      </div>

      {outputs.length === 0 && (
        <div className="text-slate-500 text-[11px]">No outputs yet — add one below.</div>
      )}

      {outputs.map((o, i) => {
        const byIndex = (o.match ?? 'byIndex') !== 'byName';
        const defName = byIndex ? `button${Number(o.index ?? 0)}` : (o.buttonName || 'name');
        return (
          <div key={i} className="border border-slate-700 rounded p-1.5 space-y-1 bg-slate-900/40">
            <div className="flex items-center gap-1">
              <select
                className={cls}
                value={byIndex ? 'byIndex' : 'byName'}
                onChange={e => patch(i, { match: e.target.value as OutputCfg['match'] })}
              >
                <option value="byIndex">by index</option>
                <option value="byName">by name</option>
              </select>
              {byIndex ? (
                <input
                  className={`${cls} flex-1`} type="number" min={0}
                  value={Number(o.index ?? 0)}
                  onChange={e => patch(i, { index: Math.max(0, Math.trunc(Number(e.target.value) || 0)) })}
                  placeholder="button index"
                />
              ) : (
                <input
                  className={`${cls} flex-1`} type="text"
                  value={o.buttonName ?? ''}
                  onChange={e => patch(i, { buttonName: e.target.value })}
                  placeholder="button name"
                />
              )}
              <button
                className="text-rose-400 hover:text-rose-300 px-1"
                title="Remove output"
                onClick={() => remove(i)}
              >✕</button>
            </div>
            <input
              className={`${cls} w-full`} type="text"
              value={o.output ?? ''}
              onChange={e => patch(i, { output: e.target.value })}
              placeholder={`output port name (default: ${defName})`}
            />
          </div>
        );
      })}

      <button
        className="px-2 py-1 text-xs rounded bg-slate-700 hover:bg-slate-600"
        onClick={add}
      >+ Output</button>
    </div>
  );
}
