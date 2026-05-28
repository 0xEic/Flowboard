// SPDX-License-Identifier: MIT
import { useState, useEffect } from 'react';
import { useGraphStore } from '../../store/graph_store';

type Props = { nodeId: string };

export function NodeFormJsonView({ nodeId }: Props) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node   = graph?.nodes.find(n => n.id === nodeId);
  const [text, setText] = useState('');
  const [err,  setErr]  = useState<string | null>(null);

  useEffect(() => {
    if (node) setText(JSON.stringify(node.config ?? {}, null, 2));
    // re-sync when the selected node changes
  }, [nodeId]);

  if (!node) return <div className="text-sm text-slate-500">Node not found.</div>;

  return (
    <div className="flex flex-col gap-2">
      <textarea
        className="bg-slate-900 text-slate-100 font-mono text-xs p-2 rounded border border-slate-700 outline-none focus:border-sky-600 h-72"
        value={text}
        onChange={e => {
          setText(e.target.value);
          try { update(nodeId, JSON.parse(e.target.value)); setErr(null); }
          catch (ex) { setErr(ex instanceof Error ? ex.message : String(ex)); }
        }}
        spellCheck={false}
      />
      {err && <div className="text-amber-400 text-xs">{err}</div>}
    </div>
  );
}
