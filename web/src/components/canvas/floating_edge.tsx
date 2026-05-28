// SPDX-License-Identifier: MIT
// Floating edges for the state-machine canvas: instead of anchoring to a fixed
// Left/Right handle, an edge's endpoints are computed from node geometry so the
// link attaches at the point on each node's border facing the other node, and
// slides continuously around the whole border as the nodes move. Rendered as a
// straight line. Adapted from React Flow's official "floating edges" example.
import { useCallback } from 'react';
import {
  getStraightPath, useStore, Position,
  type EdgeProps, type ConnectionLineComponentProps, type ReactFlowState,
} from 'reactflow';

interface Rect { x: number; y: number; width: number; height: number }

function rectOf(node: { positionAbsolute?: { x: number; y: number }; position?: { x: number; y: number }; width?: number | null; height?: number | null }): Rect {
  const pos = node.positionAbsolute ?? node.position ?? { x: 0, y: 0 };
  return { x: pos.x, y: pos.y, width: node.width ?? 1, height: node.height ?? 1 };
}

// Point where the line from `b`'s center to `a`'s center crosses `a`'s border.
function intersection(a: Rect, b: Rect): { x: number; y: number } {
  const w = a.width / 2;
  const h = a.height / 2;
  const ax = a.x + w;
  const ay = a.y + h;
  const bx = b.x + b.width / 2;
  const by = b.y + b.height / 2;

  const dx = (bx - ax) / (2 * w) - (by - ay) / (2 * h);
  const dy = (bx - ax) / (2 * w) + (by - ay) / (2 * h);
  const scale = 1 / (Math.abs(dx) + Math.abs(dy) || 1);
  const ix = w * (dx + dy) * scale + ax;
  const iy = h * (-dx + dy) * scale + ay;
  return { x: ix, y: iy };
}

function sideOf(r: Rect, p: { x: number; y: number }): Position {
  const px = Math.round(p.x);
  const py = Math.round(p.y);
  if (px <= Math.round(r.x) + 1) return Position.Left;
  if (px >= Math.round(r.x + r.width) - 1) return Position.Right;
  if (py <= Math.round(r.y) + 1) return Position.Top;
  return Position.Bottom;
}

export function getEdgeParams(source: Rect, target: Rect) {
  const sp = intersection(source, target);
  const tp = intersection(target, source);
  return {
    sx: sp.x, sy: sp.y, tx: tp.x, ty: tp.y,
    sourcePos: sideOf(source, sp), targetPos: sideOf(target, tp),
  };
}

export function FloatingEdge({ id, source, target, markerEnd, style }: EdgeProps) {
  const sourceNode = useStore(useCallback((s: ReactFlowState) => s.nodeInternals.get(source), [source]));
  const targetNode = useStore(useCallback((s: ReactFlowState) => s.nodeInternals.get(target), [target]));
  if (!sourceNode || !targetNode) return null;

  const { sx, sy, tx, ty } = getEdgeParams(rectOf(sourceNode), rectOf(targetNode));
  const [path] = getStraightPath({ sourceX: sx, sourceY: sy, targetX: tx, targetY: ty });
  return <path id={id} className="react-flow__edge-path" d={path} markerEnd={markerEnd} style={style} />;
}

// Straight preview line while dragging a new connection, anchored at the source
// node's border and following the cursor.
export function FloatingConnectionLine({ toX, toY, fromNode }: ConnectionLineComponentProps) {
  if (!fromNode) return null;
  const target: Rect = { x: toX, y: toY, width: 1, height: 1 };
  const { sx, sy } = getEdgeParams(rectOf(fromNode), target);
  const [path] = getStraightPath({ sourceX: sx, sourceY: sy, targetX: toX, targetY: toY });
  return (
    <g>
      <path d={path} fill="none" stroke="#38bdf8" strokeWidth={1.5} />
      <circle cx={toX} cy={toY} r={3} fill="#38bdf8" stroke="#0f172a" strokeWidth={1} />
    </g>
  );
}
