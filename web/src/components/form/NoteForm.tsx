// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

// The only way to edit a Note's text. The canvas note is read-only; this single
// multiline field drives config.text.
export function NoteForm({ nodeId }: { nodeId: string }) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node = graph?.nodes.find(n => n.id === nodeId);
  if (!node) return null;
  const cfg = (node.config ?? {}) as { text?: string };

  return (
    <div className="space-y-2">
      <label className="block text-slate-300 text-xs mb-1">Text</label>
      <textarea
        className="w-full h-48 bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600"
        value={cfg.text ?? ''}
        placeholder="Write your note…"
        onChange={e => update(nodeId, { ...cfg, text: e.target.value })}
        spellCheck={false}
      />
      <div className="text-slate-500 text-[11px] leading-snug">
        Shown on the canvas note. The note is read-only on the canvas — edit it here.
      </div>
    </div>
  );
}
