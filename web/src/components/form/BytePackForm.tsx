// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// Shared form for Bytes.Pack / Bytes.Unpack: a field table (name + type +
// byte offset), an endian select, and (Pack only) output length + auto-emit.
type Field = { name?: string; type?: string; offset?: number; bitOffset?: number };
type Cfg = { fields?: Field[]; endian?: string; length?: number; autoTriggerOnNewInput?: boolean };

const PRIMS = [
  'flowboard::Bool', 'flowboard::Char', 'flowboard::UInt8',
  'flowboard::Int16', 'flowboard::UInt16', 'flowboard::Int32', 'flowboard::UInt32',
  'flowboard::Int64', 'flowboard::UInt64', 'flowboard::Float', 'flowboard::Double',
];
const cls = 'bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs';

export function BytePackForm({ nodeId }: { nodeId: string }) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const isPack = node.type === 'Bytes.Pack';
  const cfg = (node.config ?? {}) as Cfg;
  const fields = Array.isArray(cfg.fields) ? cfg.fields : [];

  const writeFields = (next: Field[]) => update(nodeId, { ...cfg, fields: next });
  const setField = (i: number, patch: Partial<Field>) =>
    writeFields(fields.map((f, idx) => (idx === i ? { ...f, ...patch } : f)));
  const addField = () =>
    writeFields([...fields, { name: '', type: 'flowboard::UInt8', offset: fields.length }]);
  const removeField = (i: number) => writeFields(fields.filter((_, idx) => idx !== i));

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Endian</label>
        <select className={cls + ' w-full'} value={cfg.endian ?? 'little'}
          onChange={e => update(nodeId, { ...cfg, endian: e.target.value })}>
          <option value="little">little</option>
          <option value="big">big</option>
        </select>
      </div>

      {isPack && (
        <div className="grid grid-cols-2 gap-2">
          <div>
            <label className="block text-slate-300 text-xs mb-1">Length (0 = auto)</label>
            <input className={cls + ' w-full'} type="number" min={0} value={Number(cfg.length ?? 0)}
              onChange={e => update(nodeId, { ...cfg, length: Math.max(0, Math.trunc(Number(e.target.value) || 0)) })} />
          </div>
          <label className="flex items-end gap-2 text-xs text-slate-300 pb-1">
            <input type="checkbox" checked={cfg.autoTriggerOnNewInput ?? true}
              onChange={e => update(nodeId, { ...cfg, autoTriggerOnNewInput: e.target.checked })} />
            Auto-emit on input
          </label>
        </div>
      )}

      <div>
        <label className="block text-slate-300 text-xs mb-1">Fields</label>
        <div className="space-y-1">
          {fields.map((f, i) => (
            <div key={i} className="flex items-center gap-1">
              <input className={cls + ' flex-1'} placeholder="name" value={f.name ?? ''} spellCheck={false}
                onChange={e => setField(i, { name: e.target.value })} />
              <select className={cls} value={f.type ?? 'flowboard::UInt8'}
                onChange={e => setField(i, { type: e.target.value })}>
                {PRIMS.map(t => <option key={t} value={t}>{t.replace('flowboard::', '')}</option>)}
              </select>
              <input className={cls + ' w-16'} type="number" min={0} title="byte offset"
                value={Number(f.offset ?? 0)}
                onChange={e => setField(i, { offset: Math.max(0, Math.trunc(Number(e.target.value) || 0)) })} />
              <input className={cls + ' w-12'} type="number" min={0} max={7} placeholder="bit" title="bit offset within byte (0-7); empty = whole-byte"
                value={f.bitOffset ?? ''}
                onChange={e => {
                  const raw = e.target.value;
                  if (raw === '') {
                    // Empty → drop the bitOffset key entirely so the field stays in byte mode.
                    const { bitOffset, ...rest } = f;
                    writeFields(fields.map((g, idx) => (idx === i ? rest : g)));
                  } else {
                    setField(i, { bitOffset: Math.min(7, Math.max(0, Math.trunc(Number(raw) || 0))) });
                  }
                }} />
              <button type="button" className="px-1.5 text-slate-400 hover:text-rose-400"
                title="Remove field" onClick={() => removeField(i)}>✕</button>
            </div>
          ))}
          <button type="button" className="text-[11px] text-sky-400 hover:text-sky-200" onClick={addField}>+ Add field</button>
        </div>
        <div className="text-[10px] text-slate-500 mt-1">
          {isPack
            ? 'Each field is an input placed at its byte offset; output is a List<UInt8>. Set the optional bit column (0–7) to place a field at byte·8+bit (LSB-first).'
            : 'Each field is decoded from the input List<UInt8> at its byte offset. Set the optional bit column (0–7) to read at byte·8+bit (LSB-first).'}
          {' '}Changes take effect on Apply &amp; Reload.
        </div>
      </div>
    </div>
  );
}
