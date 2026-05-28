// SPDX-License-Identifier: MIT
import { useMemo } from 'react';
import { useGraphStore } from '../../store/graph_store';
import { listInputStructTypes, fieldsForInputType } from '../../lib/extract_ports';

interface Props {
  nodeId: string;
}

export function ExtractForm({ nodeId }: Props) {
  const graph                  = useGraphStore(s => s.graph);
  const updateNodeConfig       = useGraphStore(s => s.updateNodeConfig);
  const requestUpdateNodeConfig = useGraphStore(s => s.requestUpdateNodeConfig);
  const revertNodeConfig       = useGraphStore(s => s.revertNodeConfig);

  const node = graph?.nodes.find(n => n.id === nodeId);
  const cfg  = (node?.config ?? {}) as { inputType?: string; field?: string };

  const inputTypes = useMemo(() => listInputStructTypes(), []);
  const fields     = useMemo(() => fieldsForInputType(cfg.inputType), [cfg.inputType]);

  if (!node) return null;

  const setInputType = (inputType: string) => {
    // Type change goes through the request path so the user gets a
    // confirmation modal (existing connections to/from this node's
    // ports may become type-mismatched after the switch).
    const legal = fieldsForInputType(inputType).some(f => f.name === cfg.field);
    requestUpdateNodeConfig(nodeId, { inputType, field: legal ? cfg.field : '' });
  };
  const setField = (field: string) => {
    // Field change doesn't alter the input type, apply directly.
    updateNodeConfig(nodeId, { ...cfg, field });
  };

  return (
    <div className="space-y-3">
      <div>
        <label className="block text-slate-300 text-xs mb-1">Input type</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs"
          value={cfg.inputType ?? ''}
          onChange={e => setInputType(e.target.value)}
        >
          <option value="" disabled>Select a struct…</option>
          {inputTypes.map(t => (
            <option key={t} value={t}>{t}</option>
          ))}
        </select>
      </div>
      <div>
        <label className="block text-slate-300 text-xs mb-1">Field</label>
        <select
          className="w-full bg-slate-900 border border-slate-700 rounded px-2 py-1 text-xs disabled:opacity-40"
          value={cfg.field ?? ''}
          disabled={!cfg.inputType}
          onChange={e => setField(e.target.value)}
        >
          <option value="" disabled>{cfg.inputType ? 'Select a field…' : 'Pick input type first'}</option>
          {fields.map(f => (
            <option key={f.name} value={f.name}>
              {f.name} — {f.kind}, {f.elementTypeTag}
            </option>
          ))}
        </select>
      </div>
      <div>
        <button
          className="text-xs px-2 py-1 rounded border border-slate-600 hover:bg-slate-700"
          onClick={() => revertNodeConfig(nodeId)}
        >
          Revert
        </button>
      </div>
    </div>
  );
}
