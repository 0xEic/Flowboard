// SPDX-License-Identifier: MIT
import { useEffect, useState } from 'react';
import { useGraphStore } from '../../store/graph_store';
import { TypeSelect } from './TypeSelect';

type Cfg = { inputType?: string; fields?: string[] };

const toFields = (text: string) => text.split('\n').map(s => s.trim()).filter(Boolean);

export function ValueDisplayForm({ nodeId }: { nodeId: string }) {
  const graph                   = useGraphStore(s => s.graph);
  const updateNodeConfig        = useGraphStore(s => s.updateNodeConfig);
  const requestUpdateNodeConfig = useGraphStore(s => s.requestUpdateNodeConfig);
  const revertNodeConfig        = useGraphStore(s => s.revertNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);
  const cfg = (node?.config ?? {}) as Cfg;
  const cfgJoined = (cfg.fields ?? []).join('\n');

  // Keep the raw textarea text locally so Enter can add a blank/trailing line.
  // A purely controlled value derived from the filtered `fields` would strip
  // the newline the instant it's typed (the empty line filters out, then the
  // controlled value snaps back). Resync only on external changes (node switch,
  // Revert), detected by comparing the filtered local text to the stored config.
  const [text, setText] = useState(cfgJoined);
  useEffect(() => {
    setText(prev => (toFields(prev).join('\n') === cfgJoined ? prev : cfgJoined));
  }, [cfgJoined]);

  if (!node) return null;

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type</label>
        <TypeSelect
          value={cfg.inputType ?? ''}
          onChange={t => requestUpdateNodeConfig(nodeId, { ...cfg, inputType: t })}
        />
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Fields (one dot-path per line)</label>
        <textarea
          className="w-full h-24 bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs font-mono"
          value={text}
          placeholder={'Azimuth\nPosition.X'}
          onChange={e => {
            setText(e.target.value);
            updateNodeConfig(nodeId, { ...cfg, fields: toFields(e.target.value) });
          }}
          spellCheck={false}
        />
        <div className="text-slate-500 text-[11px] mt-1 leading-snug">
          Leave empty to show the whole value. Otherwise only these fields of a
          struct value are shown (e.g. <code>Azimuth</code>, <code>Position.X</code>).
        </div>
      </div>
      <button
        className="text-xs px-2 py-1 rounded border border-slate-600 hover:bg-slate-700"
        onClick={() => revertNodeConfig(nodeId)}
      >
        Revert
      </button>
    </div>
  );
}
