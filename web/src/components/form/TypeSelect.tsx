// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';

// Dropdown of every registered type tag (primitives + onboardapi structs),
// sourced from /api/log_types. Shared by the Log / ValueDisplay / KeyValue
// accumulator forms so type fields are picked, not typed.
export function TypeSelect(props: {
  value: string;
  onChange: (next: string) => void;
  id?: string;
}) {
  const logTypes = useGraphStore(s => s.logTypes);
  const grouped = useMemo(() => {
    const prim: string[] = [];
    const struct: string[] = [];
    for (const t of logTypes) {
      if (t.startsWith('flowboard::')) prim.push(t);
      else struct.push(t);
    }
    return { prim, struct };
  }, [logTypes]);

  return (
    <select
      id={props.id}
      className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
      value={props.value}
      onChange={e => props.onChange(e.target.value)}
    >
      <option value="" disabled>Select a type…</option>
      {grouped.prim.length > 0 && (
        <optgroup label="Primitives">
          {grouped.prim.map(t => <option key={t} value={t}>{t}</option>)}
        </optgroup>
      )}
      {grouped.struct.length > 0 && (
        <optgroup label="OnboardAPI types">
          {grouped.struct.map(t => <option key={t} value={t}>{t}</option>)}
        </optgroup>
      )}
    </select>
  );
}
