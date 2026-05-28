// SPDX-License-Identifier: MIT
import type { CatalogEntry, DiscoveryResponse, GraphJson, StatsJson } from './types';

async function ok<T = unknown>(r: Response): Promise<T> {
  if (!r.ok) throw new Error(`HTTP ${r.status}: ${await r.text()}`);
  return r.json() as Promise<T>;
}

export const api = {
  catalog:  (): Promise<CatalogEntry[]> => fetch('/api/types')    .then(r => ok<CatalogEntry[]>(r)),
  graph:    (): Promise<GraphJson>      => fetch('/api/graph')    .then(r => ok<GraphJson>(r)),
  stats:    (): Promise<StatsJson>      => fetch('/api/stats')    .then(r => ok<StatsJson>(r)),
  logTypes: (): Promise<string[]>       => fetch('/api/log_types').then(r => ok<string[]>(r)),
  throttleTypes: (): Promise<string[]>  => fetch('/api/throttle_types').then(r => ok<string[]>(r)),
  synchronizeTypes: (): Promise<string[]> => fetch('/api/synchronize_types').then(r => ok<string[]>(r)),
  reload:  (g: GraphJson): Promise<{ ok?: true; errors?: string[] }> =>
    fetch('/api/graph', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(g),
    }).then(r => r.json() as Promise<{ ok?: true; errors?: string[] }>),
  pause:   (): Promise<void> => fetch('/api/pause',  { method: 'POST' }).then(() => {}),
  resume:  (): Promise<void> => fetch('/api/resume', { method: 'POST' }).then(() => {}),
  discovery: (domain = 1, source: 'graph' | 'live' | 'both' = 'both'): Promise<DiscoveryResponse> =>
    fetch(`/api/discovery?domain=${encodeURIComponent(domain)}&source=${source}`)
      .then(r => ok<DiscoveryResponse>(r)),
  button: (nodeId: string, value: boolean): Promise<void> =>
    fetch('/api/button', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ nodeId, value }),
    }).then(() => {}),
};

export type WsMessage =
  | { port: string; value: unknown; ts: number }
  | { error: string };

export function openWs(onMessage: (msg: WsMessage) => void): WebSocket {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onmessage = ev => {
    try { onMessage(JSON.parse(ev.data) as WsMessage); }
    catch (e) { console.error('ws parse error', e); }
  };
  return ws;
}