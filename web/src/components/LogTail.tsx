// SPDX-License-Identifier: MIT
import { useGraphStore } from '../store/graph_store';

export function LogTail() {
  const live = useGraphStore(s => s.live);
  const entries = Object.entries(live).slice(-10);
  return (
    <div className="h-32 bg-slate-800 border-t border-slate-700 overflow-y-auto p-2 text-xs font-mono">
      {entries.length === 0 && (
        <div className="text-slate-500">
          No live values yet — primitive port emissions stream here when subscribed.
        </div>
      )}
      {entries.map(([port, v]) => (
        <div key={port}>
          <span className="text-slate-400">{new Date(v.ts).toISOString().slice(11, 23)}</span>
          {' '}<span className="text-sky-400">{port}</span>
          {' = '}<span>{JSON.stringify(v.value)}</span>
        </div>
      ))}
    </div>
  );
}