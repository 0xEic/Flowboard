// SPDX-License-Identifier: MIT
//
// Edge type for the StateMachine inner canvas. The edge IS the transition:
// it carries a trigger string and renders a draggable label and a draggable
// quadratic-Bezier control point. The transition path always stays solid;
// once the label is dragged clear of the line a thin dashed leader connects
// the label back to the middle of the path so the association reads visually.
//
// Endpoint geometry reuses getEdgeParams from floating_edge: the line attaches
// at the point on each state node's border facing the other node and slides
// continuously as the nodes move. Each endpoint also carries an optional offset
// (source/target) that, when set, is projected back onto that state's border --
// so the two endpoint handles (and the tandem body-drag) move the attach points
// ALONG the border, never off it.

import { useCallback, useEffect, useRef, useState } from 'react';
import {
  EdgeLabelRenderer, useReactFlow, useStore,
  type EdgeProps, type ReactFlowState,
} from 'reactflow';
import { useGraphStore } from '../../store/graph_store';
import type { InnerEdge } from '../../lib/machine_compile';
import { getEdgeParams } from './floating_edge';

interface Rect { x: number; y: number; width: number; height: number }
function rectOf(node: { positionAbsolute?: { x: number; y: number }; position?: { x: number; y: number }; width?: number | null; height?: number | null }): Rect {
  const pos = node.positionAbsolute ?? node.position ?? { x: 0, y: 0 };
  return { x: pos.x, y: pos.y, width: node.width ?? 1, height: node.height ?? 1 };
}

// Snap a point onto the rectangle's perimeter: clamp it inside, then push it out
// to whichever of the four sides is nearest. Used so a slid endpoint always
// lands ON the state border (the line is drawn to this border point) rather than
// floating off it when the endpoint offset has a component pointing away.
function projectToBorder(r: Rect, p: { x: number; y: number }): { x: number; y: number } {
  const cx = Math.min(Math.max(p.x, r.x), r.x + r.width);
  const cy = Math.min(Math.max(p.y, r.y), r.y + r.height);
  const dl = cx - r.x;             // distance to left edge
  const dr = r.x + r.width - cx;   // right
  const dt = cy - r.y;             // top
  const db = r.y + r.height - cy;  // bottom
  const m = Math.min(dl, dr, dt, db);
  if (m === dl) return { x: r.x, y: cy };
  if (m === dr) return { x: r.x + r.width, y: cy };
  if (m === dt) return { x: cx, y: r.y };
  return { x: cx, y: r.y + r.height };
}

// Shortest distance from point p to segment a-b (flow coords). Used to pick the
// segment a Ctrl-click should insert a joint into.
function distToSegment(p: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }): number {
  const vx = b.x - a.x, vy = b.y - a.y;
  const wx = p.x - a.x, wy = p.y - a.y;
  const c1 = vx * wx + vy * wy;
  if (c1 <= 0) return Math.hypot(p.x - a.x, p.y - a.y);
  const c2 = vx * vx + vy * vy;
  if (c2 <= c1) return Math.hypot(p.x - b.x, p.y - b.y);
  const t = c1 / c2;
  return Math.hypot(p.x - (a.x + t * vx), p.y - (a.y + t * vy));
}

// Data carried on the React Flow edge object -- the canvas builds this in
// MachineCanvas so the renderer can look up the canonical edge by index.
export interface TriggerEdgeData {
  index: number;
  edge: InnerEdge;
}

// Self-loop layout: arc above the node centre. The control point is in flow
// coordinates relative to the straight midpoint, which for a self-loop sits at
// the node's centre -- so the control point IS the offset from the centre.
function selfLoopGeometry(rect: Rect, controlPoint: { x: number; y: number }) {
  const cx = rect.x + rect.width / 2;
  const cy = rect.y + rect.height / 2;
  const apexX = cx + controlPoint.x;
  const apexY = cy + controlPoint.y;
  // Endpoints land on the top of the node, slightly offset left/right of centre.
  const offset = Math.min(24, rect.width / 3);
  const sx = cx - offset;
  const sy = rect.y;
  const tx = cx + offset;
  const ty = rect.y;
  return { sx, sy, tx, ty, apexX, apexY };
}

export function TriggerEdge(props: EdgeProps<TriggerEdgeData>) {
  const { id, source, target, selected, markerEnd, style, data } = props;
  const flow = useReactFlow();
  const update = useGraphStore(s => s.updateInnerEdge);
  // Live state, for the timed-transition countdown/progress animation.
  const live = useGraphStore(s => s.live);
  const machineId = useGraphStore(s => s.editingMachineId);
  const machineStack = useGraphStore(s => s.machineStack);

  const sourceNode = useStore(useCallback((s: ReactFlowState) => s.nodeInternals.get(source), [source]));
  const targetNode = useStore(useCallback((s: ReactFlowState) => s.nodeInternals.get(target), [target]));

  const [draftLabel, setDraftLabel] = useState<{ x: number; y: number } | null>(null);
  const [draftControl, setDraftControl] = useState<{ x: number; y: number } | null>(null);
  const [draftSource, setDraftSource] = useState<{ x: number; y: number } | null>(null);
  const [draftTarget, setDraftTarget] = useState<{ x: number; y: number } | null>(null);
  const [draftWaypoint, setDraftWaypoint] = useState<{ i: number; x: number; y: number } | null>(null);
  const draggingRef = useRef<null | 'label' | 'control' | 'endpoint' | 'source' | 'target' | 'joint'>(null);

  if (!sourceNode || !targetNode || !data) return null;
  const { index, edge } = data;
  const isSelfLoop = source === target;

  // ---- geometry ----------------------------------------------------------
  const sourceRect = rectOf(sourceNode);
  const targetRect = rectOf(targetNode);

  let sx: number, sy: number, tx: number, ty: number;
  let midX: number, midY: number;  // straight midpoint for label anchor
  let apexX: number, apexY: number;  // bezier apex (control point in flow coords)
  let path: string;

  const controlPoint = draftControl ?? edge.controlPoint;
  const labelOffset = draftLabel ?? edge.labelOffset;
  // Per-endpoint offsets, each projected back onto its node's border below so the
  // attach point slides ALONG the border (staying on it) instead of detaching.
  // The body-drag moves both together; the endpoint handles move one at a time.
  const sourceOffset = draftSource ?? edge.sourceOffset ?? { x: 0, y: 0 };
  const targetOffset = draftTarget ?? edge.targetOffset ?? { x: 0, y: 0 };
  const sourceSlid = sourceOffset.x !== 0 || sourceOffset.y !== 0;
  const targetSlid = targetOffset.x !== 0 || targetOffset.y !== 0;

  if (isSelfLoop) {
    const cp = controlPoint ?? { x: 0, y: -60 };
    const g = selfLoopGeometry(sourceRect, cp);
    // Base endpoints stay pinned to the node border (projected when slid); the
    // apex floats above as the loop arc.
    const s = sourceSlid ? projectToBorder(sourceRect, { x: g.sx + sourceOffset.x, y: g.sy + sourceOffset.y }) : { x: g.sx, y: g.sy };
    const t = targetSlid ? projectToBorder(sourceRect, { x: g.tx + targetOffset.x, y: g.ty + targetOffset.y }) : { x: g.tx, y: g.ty };
    sx = s.x; sy = s.y; tx = t.x; ty = t.y;
    apexX = g.apexX; apexY = g.apexY;
    midX = sourceRect.x + sourceRect.width / 2;
    midY = sourceRect.y + sourceRect.height / 2 + cp.y;  // label anchored near apex
    // Quadratic curve from sx,sy up through (apexX, apexY) down to tx,ty.
    path = `M ${sx} ${sy} Q ${apexX} ${apexY} ${tx} ${ty}`;
  } else {
    const ep = getEdgeParams(sourceRect, targetRect);
    // Project each offset endpoint back onto its node's border so the attach
    // point slides ALONG the border and the line extends to meet it.
    const s = sourceSlid ? projectToBorder(sourceRect, { x: ep.sx + sourceOffset.x, y: ep.sy + sourceOffset.y }) : { x: ep.sx, y: ep.sy };
    const t = targetSlid ? projectToBorder(targetRect, { x: ep.tx + targetOffset.x, y: ep.ty + targetOffset.y }) : { x: ep.tx, y: ep.ty };
    sx = s.x; sy = s.y; tx = t.x; ty = t.y;
    midX = (sx + tx) / 2;
    midY = (sy + ty) / 2;
    if (controlPoint) {
      apexX = midX + controlPoint.x;
      apexY = midY + controlPoint.y;
      path = `M ${sx} ${sy} Q ${apexX} ${apexY} ${tx} ${ty}`;
    } else {
      apexX = midX;
      apexY = midY;
      path = `M ${sx} ${sy} L ${tx} ${ty}`;
    }
  }

  // Polyline joints ("waypoints"), each an offset from the straight midpoint.
  // The dragged one is overlaid live from draftWaypoint. Joints route the edge
  // as straight segments and take precedence over the single-curve control point.
  const waypoints = (draftWaypoint
    ? edge.waypoints.map((w, i) => (i === draftWaypoint.i ? { x: draftWaypoint.x, y: draftWaypoint.y } : w))
    : edge.waypoints);
  const wpAbs = waypoints.map(w => ({ x: midX + w.x, y: midY + w.y }));
  const hasWaypoints = !isSelfLoop && wpAbs.length > 0;
  if (hasWaypoints) {
    path = `M ${sx} ${sy} ` + wpAbs.map(p => `L ${p.x} ${p.y}`).join(' ') + ` L ${tx} ${ty}`;
  }

  const labelX = midX + labelOffset.x;
  const labelY = midY + labelOffset.y;
  const isDraggingLabel = draggingRef.current === 'label';

  // Anchor the leader at the visual middle of the path. For a polyline that's the
  // midpoint of its central segment; for a curve, the t=0.5 Bezier point (which
  // reduces to (midX, midY) when there is no control point).
  let lineMidX: number, lineMidY: number;
  if (hasWaypoints) {
    const pts = [{ x: sx, y: sy }, ...wpAbs, { x: tx, y: ty }];
    const k = Math.floor((pts.length - 1) / 2);
    lineMidX = (pts[k].x + pts[k + 1].x) / 2;
    lineMidY = (pts[k].y + pts[k + 1].y) / 2;
  } else {
    lineMidX = 0.25 * sx + 0.5 * apexX + 0.25 * tx;
    lineMidY = 0.25 * sy + 0.5 * apexY + 0.25 * ty;
  }
  // Only draw the leader once the label has been pulled clear of the line,
  // so a label resting on the path doesn't sprout a zero-length stub.
  const showLeader = Math.hypot(labelX - lineMidX, labelY - lineMidY) > 8;

  // ---- timed-transition live countdown -----------------------------------
  // While the source state is active the engine streams remaining ms on
  // `<fsm>.timer.<path><from>-><to>`. Gate on the source's active.<path><from>
  // tap so a stale value never animates an inactive link. `path`-prefix matches
  // the engine's dotted naming (machineStack = drilled-in composite states).
  let timerRemaining: number | null = null;
  let timerProgress = 0;
  if (edge.kind === 'timed' && machineId && edge.delayMs > 0) {
    const pfx = machineStack.map(s => s.stateName).join('.');
    const dot = pfx ? pfx + '.' : '';
    const fromName = source.replace(/^state:/, '');
    const toName = target.replace(/^state:/, '');
    const activeNow = live[`${machineId}.active.${dot}${fromName}`]?.value === true;
    const rem = live[`${machineId}.timer.${dot}${fromName}->${toName}`]?.value;
    if (activeNow && typeof rem === 'number') {
      timerRemaining = Math.max(0, rem);
      timerProgress = Math.min(1, Math.max(0, (edge.delayMs - timerRemaining) / edge.delayMs));
    }
  }
  const timerRunning = timerRemaining !== null;

  // ---- drag handlers -----------------------------------------------------

  // Convert a pointer event to flow coordinates via React Flow's viewport.
  const toFlow = useCallback((evt: PointerEvent | React.PointerEvent) =>
    flow.screenToFlowPosition({ x: evt.clientX, y: evt.clientY }), [flow]);

  // Cancel hook for an in-flight drag: removes the window listeners and clears
  // draft state. Called by onUp normally, and by the unmount effect if the
  // component is torn down mid-drag (otherwise the listeners would close over
  // setState on an unmounted component).
  const cancelDragRef = useRef<(() => void) | null>(null);

  const startLabelDrag = useCallback((evt: React.PointerEvent) => {
    if ((evt.target as HTMLElement).tagName === 'INPUT') return;  // edit, don't drag
    evt.stopPropagation();
    evt.preventDefault();
    draggingRef.current = 'label';
    const origin = toFlow(evt);
    const startOffset = { ...edge.labelOffset };

    const onMove = (e: PointerEvent) => {
      const here = toFlow(e);
      setDraftLabel({
        x: startOffset.x + (here.x - origin.x),
        y: startOffset.y + (here.y - origin.y),
      });
    };
    const detach = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      cancelDragRef.current = null;
    };
    const onUp = (e: PointerEvent) => {
      const here = toFlow(e);
      const finalOffset = {
        x: startOffset.x + (here.x - origin.x),
        y: startOffset.y + (here.y - origin.y),
      };
      detach();
      draggingRef.current = null;
      setDraftLabel(null);
      update(index, { labelOffset: finalOffset });
    };
    cancelDragRef.current = detach;
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  }, [toFlow, edge.labelOffset, update, index]);

  const startControlDrag = useCallback((evt: React.PointerEvent) => {
    evt.stopPropagation();
    evt.preventDefault();
    draggingRef.current = 'control';
    const origin = toFlow(evt);
    const startCp = edge.controlPoint ?? { x: 0, y: 0 };

    const onMove = (e: PointerEvent) => {
      const here = toFlow(e);
      setDraftControl({
        x: startCp.x + (here.x - origin.x),
        y: startCp.y + (here.y - origin.y),
      });
    };
    const detach = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      cancelDragRef.current = null;
    };
    const onUp = (e: PointerEvent) => {
      const here = toFlow(e);
      const finalCp = {
        x: startCp.x + (here.x - origin.x),
        y: startCp.y + (here.y - origin.y),
      };
      detach();
      draggingRef.current = null;
      setDraftControl(null);
      update(index, { controlPoint: finalCp });
    };
    cancelDragRef.current = detach;
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  }, [toFlow, edge.controlPoint, update, index]);

  const resetControl = useCallback((evt: React.MouseEvent) => {
    evt.stopPropagation();
    update(index, { controlPoint: isSelfLoop ? { x: 0, y: -60 } : null });
  }, [update, index, isSelfLoop]);

  // Drag the link body to slide BOTH endpoints together. The pointer is only
  // claimed as a drag once it moves past a small threshold -- below that the
  // gesture stays a plain click so React Flow can still select the edge.
  const startEndpointDrag = useCallback((evt: React.PointerEvent) => {
    if (evt.button !== 0) return;
    const origin = toFlow(evt);
    const startS = edge.sourceOffset ?? { x: 0, y: 0 };
    const startT = edge.targetOffset ?? { x: 0, y: 0 };
    let armed = false;  // becomes true once we cross the drag threshold

    const onMove = (e: PointerEvent) => {
      const here = toFlow(e);
      const dx = here.x - origin.x;
      const dy = here.y - origin.y;
      if (!armed && Math.hypot(dx, dy) < 4) return;  // still a click, not a drag
      armed = true;
      draggingRef.current = 'endpoint';
      setDraftSource({ x: startS.x + dx, y: startS.y + dy });
      setDraftTarget({ x: startT.x + dx, y: startT.y + dy });
    };
    const detach = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      cancelDragRef.current = null;
    };
    const onUp = (e: PointerEvent) => {
      detach();
      draggingRef.current = null;
      if (!armed) return;  // no movement -> leave selection click untouched
      const here = toFlow(e);
      const dx = here.x - origin.x, dy = here.y - origin.y;
      setDraftSource(null);
      setDraftTarget(null);
      update(index, {
        sourceOffset: { x: startS.x + dx, y: startS.y + dy },
        targetOffset: { x: startT.x + dx, y: startT.y + dy },
      });
    };
    cancelDragRef.current = detach;
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  }, [toFlow, edge.sourceOffset, edge.targetOffset, update, index]);

  const resetEndpoints = useCallback((evt: React.MouseEvent) => {
    evt.stopPropagation();
    update(index, { sourceOffset: null, targetOffset: null });
  }, [update, index]);

  // Drag a single endpoint handle to slide just that attach point along its own
  // state border. `which` selects the source or target offset.
  const startHandleDrag = useCallback((evt: React.PointerEvent, which: 'source' | 'target') => {
    evt.stopPropagation();
    evt.preventDefault();
    draggingRef.current = which;
    const origin = toFlow(evt);
    const start = (which === 'source' ? edge.sourceOffset : edge.targetOffset) ?? { x: 0, y: 0 };
    const setDraft = which === 'source' ? setDraftSource : setDraftTarget;

    const onMove = (e: PointerEvent) => {
      const here = toFlow(e);
      setDraft({ x: start.x + (here.x - origin.x), y: start.y + (here.y - origin.y) });
    };
    const detach = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      cancelDragRef.current = null;
    };
    const onUp = (e: PointerEvent) => {
      const here = toFlow(e);
      detach();
      draggingRef.current = null;
      setDraft(null);
      const offset = { x: start.x + (here.x - origin.x), y: start.y + (here.y - origin.y) };
      update(index, which === 'source' ? { sourceOffset: offset } : { targetOffset: offset });
    };
    cancelDragRef.current = detach;
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  }, [toFlow, edge.sourceOffset, edge.targetOffset, update, index]);

  const resetHandle = useCallback((evt: React.MouseEvent, which: 'source' | 'target') => {
    evt.stopPropagation();
    update(index, which === 'source' ? { sourceOffset: null } : { targetOffset: null });
  }, [update, index]);

  const onTriggerChange = useCallback((evt: React.ChangeEvent<HTMLInputElement>) => {
    update(index, { trigger: evt.target.value });
  }, [update, index]);

  // ---- joints (polyline waypoints) ---------------------------------------
  // These read the live geometry (midX/sx/...) captured at call time, so they
  // are plain closures rebuilt each render rather than memoised.

  // Ctrl/Cmd-click the link to insert a joint at the click, into the nearest
  // segment. Stored as an offset from the straight midpoint.
  const addJoint = (evt: React.PointerEvent) => {
    evt.stopPropagation();
    evt.preventDefault();
    const p = toFlow(evt);
    const pts = [{ x: sx, y: sy }, ...wpAbs, { x: tx, y: ty }];
    let best = 0, bestD = Infinity;
    for (let k = 0; k < pts.length - 1; k++) {
      const d = distToSegment(p, pts[k], pts[k + 1]);
      if (d < bestD) { bestD = d; best = k; }
    }
    const off = { x: p.x - midX, y: p.y - midY };
    const next = [...waypoints.slice(0, best), off, ...waypoints.slice(best)];
    update(index, { waypoints: next });
  };

  const startJointDrag = (evt: React.PointerEvent, i: number) => {
    evt.stopPropagation();
    evt.preventDefault();
    draggingRef.current = 'joint';
    const origin = toFlow(evt);
    const start = { ...waypoints[i] };
    // Previous joint (or the source endpoint) as an offset from the midpoint --
    // used to axis-align the incoming segment when Shift is held.
    const prev = i === 0 ? { x: sx - midX, y: sy - midY } : { ...waypoints[i - 1] };

    const compute = (e: PointerEvent) => {
      const here = toFlow(e);
      let off = { x: start.x + (here.x - origin.x), y: start.y + (here.y - origin.y) };
      if (e.shiftKey) {
        // Snap so the segment from `prev` is perfectly horizontal or vertical,
        // picking the axis the joint is farther along.
        if (Math.abs(off.x - prev.x) >= Math.abs(off.y - prev.y)) off = { x: off.x, y: prev.y };
        else off = { x: prev.x, y: off.y };
      }
      return off;
    };
    const onMove = (e: PointerEvent) => { const off = compute(e); setDraftWaypoint({ i, x: off.x, y: off.y }); };
    const detach = () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      cancelDragRef.current = null;
    };
    const onUp = (e: PointerEvent) => {
      const off = compute(e);
      detach();
      draggingRef.current = null;
      setDraftWaypoint(null);
      const next = edge.waypoints.map((w, j) => (j === i ? off : w));
      update(index, { waypoints: next });
    };
    cancelDragRef.current = detach;
    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
  };

  const removeJoint = (evt: React.MouseEvent, i: number) => {
    evt.stopPropagation();
    update(index, { waypoints: edge.waypoints.filter((_, j) => j !== i) });
  };

  useEffect(() => () => {
    // If the component unmounts mid-drag (e.g. user closes the inner canvas
    // while holding the label), detach the window listeners so they don't
    // fire stale setState/store updates on an unmounted component.
    cancelDragRef.current?.();
    draggingRef.current = null;
  }, []);

  // ---- render ------------------------------------------------------------

  const stroke = style?.stroke as string | undefined;
  // Line style encodes the transition kind: trigger solid, null dashed,
  // timed dash-dot. (The label content distinguishes them too.)
  const dashByKind = edge.kind === 'null' ? '5 4' : edge.kind === 'timed' ? '7 3 1 3' : undefined;
  const pathStyle: React.CSSProperties = {
    ...style,
    fill: 'none',
    strokeDasharray: dashByKind,
  };

  return (
    <>
      {/* Invisible wide hit target -- without it only the 1.5px visible stroke
          is clickable, so selecting the edge takes several attempts. Mirrors the
          interaction path that React Flow's BaseEdge renders for built-in edges.
          It also doubles as the drag surface for sliding both endpoints; `nopan`
          stops the drag from panning the canvas. Ctrl/Cmd-click inserts a joint. */}
      <path
        className="react-flow__edge-interaction nopan"
        d={path}
        fill="none"
        stroke="transparent"
        strokeWidth={20}
        style={{ cursor: 'move' }}
        onPointerDown={e => { if (e.ctrlKey || e.metaKey) addJoint(e); else startEndpointDrag(e); }}
        onDoubleClick={resetEndpoints}
      />
      {/* Visible stroke is non-interactive so every hit (anywhere in the 20px
          band) lands on the interaction path above. */}
      <path
        id={id}
        className="react-flow__edge-path"
        d={path}
        markerEnd={markerEnd}
        style={{ ...pathStyle, pointerEvents: 'none' }}
      />
      {/* Timed-transition progress: a bright overlay that fills from source to
          target as the timer counts down (pathLength=1 normalises the dash so
          `progress` maps directly to the drawn fraction, curves included). */}
      {timerRunning && (
        <path
          d={path}
          fill="none"
          stroke="#22d3ee"
          strokeWidth={3.5}
          strokeLinecap="round"
          pathLength={1}
          strokeDasharray={`${timerProgress} 1`}
          style={{ pointerEvents: 'none' }}
        />
      )}
      {showLeader && (
        <path
          d={`M ${lineMidX} ${lineMidY} L ${labelX} ${labelY}`}
          fill="none"
          stroke={stroke ?? '#94a3b8'}
          strokeWidth={1}
          strokeDasharray="2 3"
          style={{ pointerEvents: 'none', opacity: 0.7 }}
        />
      )}
      {selected && (
        <>
          {/* Curve control handle -- only when the edge isn't routed by joints. */}
          {!hasWaypoints && (
            <circle
              cx={apexX} cy={apexY} r={5}
              fill={stroke ?? '#38bdf8'} stroke="#0f172a" strokeWidth={1}
              style={{ cursor: 'grab', pointerEvents: 'all' }}
              onPointerDown={startControlDrag}
              onDoubleClick={resetControl}
            />
          )}
          {/* Joint handles -- drag to move (hold Shift to snap the incoming
              segment horizontal/vertical); double-click to remove. */}
          {hasWaypoints && wpAbs.map((p, i) => (
            <rect
              key={i}
              x={p.x - 4} y={p.y - 4} width={8} height={8}
              fill="#fde68a" stroke="#0f172a" strokeWidth={1}
              style={{ cursor: 'grab', pointerEvents: 'all' }}
              onPointerDown={e => startJointDrag(e, i)}
              onDoubleClick={e => removeJoint(e, i)}
            />
          ))}
          {/* Endpoint handles -- drag to slide each attach point along its own
              state border; double-click to snap back to auto. Hollow to read as
              distinct from the filled apex/control handle. */}
          <circle
            cx={sx} cy={sy} r={4}
            fill="#f8fafc" stroke={stroke ?? '#38bdf8'} strokeWidth={1.5}
            style={{ cursor: 'grab', pointerEvents: 'all' }}
            onPointerDown={e => startHandleDrag(e, 'source')}
            onDoubleClick={e => resetHandle(e, 'source')}
          />
          <circle
            cx={tx} cy={ty} r={4}
            fill="#f8fafc" stroke={stroke ?? '#38bdf8'} strokeWidth={1.5}
            style={{ cursor: 'grab', pointerEvents: 'all' }}
            onPointerDown={e => startHandleDrag(e, 'target')}
            onDoubleClick={e => resetHandle(e, 'target')}
          />
        </>
      )}
      <EdgeLabelRenderer>
        <div
          className={[
            'absolute rounded border px-1.5 py-0.5 text-[11px] bg-slate-900',
            edge.kind === 'null' ? 'text-violet-200' : edge.kind === 'timed' ? 'text-cyan-200' : 'text-amber-200',
            selected ? 'border-sky-500'
              : edge.kind === 'null' ? 'border-violet-700/60'
              : edge.kind === 'timed' ? 'border-cyan-700/60'
              : 'border-amber-700/60',
            isDraggingLabel ? 'cursor-grabbing' : 'cursor-grab',
          ].join(' ')}
          style={{
            transform: `translate(-50%, -50%) translate(${labelX}px, ${labelY}px)`,
            pointerEvents: 'all',
          }}
          onPointerDown={startLabelDrag}
        >
          {edge.kind === 'trigger' ? (
            <input
              className="nodrag nowheel w-20 bg-transparent border-b border-amber-700/50 focus:border-amber-400 outline-none"
              value={edge.trigger}
              onChange={onTriggerChange}
              onPointerDown={e => e.stopPropagation()}
              title="Boolean trigger port name"
            />
          ) : edge.kind === 'timed' ? (
            <span
              className={timerRunning ? 'font-mono tabular-nums text-cyan-100' : undefined}
              title="Timed transition (fires after the delay)"
            >
              {timerRunning ? `${timerRemaining} ms` : `${edge.delayMs} ms`}
            </span>
          ) : (
            <span title="Null transition (fires immediately on entry)">ε</span>
          )}
        </div>
      </EdgeLabelRenderer>
    </>
  );
}
