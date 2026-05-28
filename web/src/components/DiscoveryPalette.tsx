// SPDX-License-Identifier: MIT
import { useCallback, useEffect, useMemo, useState } from 'react';
import { api } from '../api/client';
import type { DiscoveryEndpoint } from '../api/types';

// Dragging a discovered endpoint adds the OPPOSITE interface so the user
// connects TO it rather than duplicating it: discover a Service -> drop a
// Client (and vice versa), pre-filled with the same domain + serviceName.
function connectNodeType(e: DiscoveryEndpoint): string {
  const opposite = e.direction === 'Service' ? 'Client' : 'Service';
  return `M_${e.interfaceType}.${opposite}`;
}

const sourceBadge: Record<DiscoveryEndpoint['source'], string> = {
  live:  'bg-emerald-700 text-emerald-100',
  graph: 'bg-slate-600 text-slate-200',
  both:  'bg-sky-700 text-sky-100',
};

export function DiscoveryPalette() {
  const [domain, setDomain]   = useState(1);
  const [items, setItems]     = useState<DiscoveryEndpoint[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError]     = useState<string | null>(null);
  const [search, setSearch]   = useState('');
  const q = search.trim().toLowerCase();

  const refresh = useCallback(() => {
    setLoading(true);
    setError(null);
    api.discovery(domain, 'both')
      .then(r => setItems(r.endpoints))
      .catch(e => setError(String(e)))
      .finally(() => setLoading(false));
  }, [domain]);

  useEffect(() => { refresh(); }, [refresh]);

  const groups = useMemo(() => {
    const filtered = q
      ? items.filter(e =>
          e.interfaceType.toLowerCase().includes(q) ||
          e.serviceName.toLowerCase().includes(q) ||
          e.direction.toLowerCase().includes(q))
      : items;
    const m = new Map<string, DiscoveryEndpoint[]>();
    for (const e of filtered) {
      const arr = m.get(e.interfaceType) ?? [];
      arr.push(e);
      m.set(e.interfaceType, arr);
    }
    for (const arr of m.values()) {
      arr.sort((a, b) =>
        a.direction.localeCompare(b.direction) || a.serviceName.localeCompare(b.serviceName));
    }
    return [...m.entries()].sort(([a], [b]) => a.localeCompare(b));
  }, [items, q]);

  return (
    <aside className="w-64 bg-slate-800 border-r border-slate-700 overflow-y-auto p-3 text-sm">
      <div className="flex items-center justify-between mb-2">
        <div className="font-semibold">Discover</div>
        <button
          className="px-2 py-0.5 text-xs rounded bg-slate-700 hover:bg-slate-600 disabled:opacity-50"
          onClick={refresh}
          disabled={loading}
          title="Re-scan the network"
        >
          {loading ? '…' : 'Refresh'}
        </button>
      </div>

      <label className="flex items-center gap-2 text-xs text-slate-400 mb-2">
        Domain
        <input
          type="number"
          min={0}
          className="w-16 px-2 py-1 bg-slate-900 border border-slate-700 rounded text-xs text-slate-100 outline-none focus:border-sky-600"
          value={domain}
          onChange={e => setDomain(Number(e.target.value) || 0)}
        />
      </label>

      <input
        className="w-full mb-2 px-2 py-1 bg-slate-900 border border-slate-700 rounded text-xs outline-none focus:border-sky-600"
        placeholder="Search services…"
        value={search}
        onChange={e => setSearch(e.target.value)}
        spellCheck={false}
      />

      <p className="text-[11px] text-slate-500 mb-2">
        Drag an entry onto the canvas to add the node that connects to it
        (a Service drops a Client, and vice&nbsp;versa) with its
        domain&nbsp;/&nbsp;serviceName pre-filled.
      </p>

      {error && <div className="text-rose-400 text-xs mb-2 break-words">{error}</div>}

      {groups.map(([iface, eps]) => (
        <div key={iface} className="mb-3">
          <div className="text-slate-400 uppercase text-xs mb-1">{iface}</div>
          {eps.map((e, i) => (
            <div
              key={`${e.direction}:${e.serviceName}:${e.actorId ?? i}`}
              className="px-2 py-1 mb-1 rounded bg-slate-900/40 hover:bg-slate-700 text-xs cursor-grab active:cursor-grabbing select-none border border-slate-700/60"
              title={[
                `connect with: ${connectNodeType(e)}`,
                `serviceName: ${e.serviceName}`,
                `domain: ${e.domainId}`,
                e.hostName ? `host: ${e.hostName}` : '',
                e.procName ? `proc: ${e.procName} (${e.processId})` : '',
              ].filter(Boolean).join('\n')}
              draggable
              onDragStart={ev => {
                ev.dataTransfer.setData('application/flowboard-node-type', connectNodeType(e));
                ev.dataTransfer.setData(
                  'application/flowboard-node-config',
                  JSON.stringify({ domainId: e.domainId, serviceName: e.serviceName }),
                );
                ev.dataTransfer.effectAllowed = 'copy';
              }}
            >
              <div className="flex items-center justify-between gap-2">
                <span className="font-medium text-slate-100 truncate">{e.serviceName}</span>
                <span className={`shrink-0 px-1 rounded text-[10px] ${sourceBadge[e.source]}`}>
                  {e.source}
                </span>
              </div>
              <div className="text-slate-400">
                {e.direction}
                {e.hostName ? <span className="text-slate-500"> · {e.hostName}</span> : null}
              </div>
            </div>
          ))}
        </div>
      ))}

      {!loading && groups.length === 0 && !error && (
        <div className="text-slate-500 text-xs">
          {items.length === 0
            ? `No Services or Clients found on domain ${domain}.`
            : 'No endpoints match the search.'}
        </div>
      )}
    </aside>
  );
}
