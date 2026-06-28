// SPDX-License-Identifier: MIT
//
// Tiny read-only thumbnail of an inner graph, drawn on a container node in the
// main canvas so you can see a StateMachine's states/transitions or a Group's
// subgraph at a glance without opening it. Generic over {points, links}; the
// `machineToPreview` / `groupToPreview` helpers adapt each config shape.

import { useMemo } from 'react';
import type { MachineConfig } from '../../lib/machine_compile';
import type { GroupConfig } from '../../api/types';

export interface PreviewPoint { id: string; x: number; y: number }
export interface PreviewLink { from: string; to: string }
export interface PreviewGraph { points: PreviewPoint[]; links: PreviewLink[] }

// StateMachine config -> dots (states at their canvas positions) + lines
// (transitions between the two states they connect).
export function machineToPreview(cfg: Partial<MachineConfig> | undefined): PreviewGraph {
  const points = (cfg?.states ?? []).map(s => ({
    id: s.name, x: s.position?.x ?? 0, y: s.position?.y ?? 0,
  }));
  const links = (cfg?.transitions ?? []).map(t => ({ from: t.from, to: t.to }));
  return { points, links };
}

// Group subgraph -> dots (inner nodes) + lines (edges). Edge endpoints are
// "nodeId.port" refs, so the node id is everything before the first dot.
export function groupToPreview(grp: GroupConfig | undefined): PreviewGraph {
  const points = (grp?.nodes ?? []).map(n => ({
    id: n.id, x: n.position?.x ?? 0, y: n.position?.y ?? 0,
  }));
  const nodeId = (ref: string) => ref.split('.')[0];
  const links = (grp?.edges ?? []).map(e => ({ from: nodeId(e.from), to: nodeId(e.to) }));
  return { points, links };
}

const PAD = 6;

export function GraphMiniPreview({
  graph, width = 176, height = 60, accent = '#38bdf8', activeIds,
}: {
  graph: PreviewGraph;
  width?: number;
  height?: number;
  accent?: string;
  // Point ids that are currently live/active — drawn larger in emerald so the
  // thumbnail doubles as a live state readout.
  activeIds?: ReadonlySet<string>;
}) {
  const layout = useMemo(() => {
    let pts = graph.points.map(p => ({ ...p }));
    if (pts.length === 0) return null;

    let minX = Math.min(...pts.map(p => p.x));
    let maxX = Math.max(...pts.map(p => p.x));
    let minY = Math.min(...pts.map(p => p.y));
    let maxY = Math.max(...pts.map(p => p.y));

    // Degenerate spread (all states stacked, or missing positions) collapses to
    // a single dot -- fall back to a deterministic grid so structure still reads.
    if (maxX - minX < 1 && maxY - minY < 1) {
      const cols = Math.ceil(Math.sqrt(pts.length));
      pts = pts.map((p, i) => ({ ...p, x: i % cols, y: Math.floor(i / cols) }));
      minX = 0; minY = 0;
      maxX = cols - 1; maxY = Math.floor((pts.length - 1) / cols);
    }

    const spanX = Math.max(1, maxX - minX);
    const spanY = Math.max(1, maxY - minY);
    const s = Math.min((width - 2 * PAD) / spanX, (height - 2 * PAD) / spanY);
    const offX = PAD + ((width - 2 * PAD) - spanX * s) / 2;
    const offY = PAD + ((height - 2 * PAD) - spanY * s) / 2;
    const place = (p: PreviewPoint) => ({ x: offX + (p.x - minX) * s, y: offY + (p.y - minY) * s });

    const byId = new Map(pts.map(p => [p.id, place(p)]));
    const dots = pts.map(p => ({ id: p.id, ...place(p) }));
    const lines = graph.links
      .filter(l => l.from !== l.to && byId.has(l.from) && byId.has(l.to))
      .map(l => ({ a: byId.get(l.from)!, b: byId.get(l.to)! }));
    return { dots, lines };
  }, [graph, width, height]);

  return (
    <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} className="block">
      {!layout ? (
        <text x={width / 2} y={height / 2} textAnchor="middle" dominantBaseline="middle"
              fontSize={9} fill="#64748b" fontStyle="italic">empty</text>
      ) : (
        <>
          {layout.lines.map((l, i) => (
            <line key={i} x1={l.a.x} y1={l.a.y} x2={l.b.x} y2={l.b.y}
                  stroke="#64748b" strokeWidth={1} />
          ))}
          {layout.dots.map(d => {
            const on = activeIds?.has(d.id);
            // Rounded-rectangle "state" shape (matches the canvas state nodes),
            // slightly larger + emerald when live-active.
            const w = on ? 16 : 13;
            const h = on ? 10 : 8;
            return (
              <rect key={d.id} x={d.x - w / 2} y={d.y - h / 2} width={w} height={h} rx={2.5} ry={2.5}
                    fill={on ? '#34d399' : accent} stroke="#0f172a" strokeWidth={0.75} />
            );
          })}
        </>
      )}
    </svg>
  );
}
