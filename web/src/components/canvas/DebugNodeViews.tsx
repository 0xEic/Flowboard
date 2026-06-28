// SPDX-License-Identifier: MIT
import { useEffect, useRef, useState, type ReactNode, type MouseEvent as ReactMouseEvent } from 'react';
import { Handle, Position, NodeResizer } from 'reactflow';
import { useGraphStore } from '../../store/graph_store';
import { colorForTypeTag } from '../../lib/typeColor';
import { resolveTapKey } from '../../lib/group_compile';
import { api } from '../../api/client';
import { HelpIcon } from '../../help/HelpIcon';

const dot = (tag: string) => ({
  width: 10, height: 10, borderRadius: 999,
  background: colorForTypeTag(tag),
  border: '1px solid #0f172a',
});

// Shared resizable card. Fills the React Flow node box (which GraphCanvas sizes
// from the node's persisted width/height) and exposes a NodeResizer when the
// node is selected; the final size is persisted via updateNodeSize.
function Frame(props: {
  id: string; type: string; selected: boolean;
  minWidth?: number; minHeight?: number; children: ReactNode;
}) {
  const updateSize = useGraphStore(s => s.updateNodeSize);
  return (
    <div
      className={[
        'relative w-full h-full flex flex-col rounded border text-xs bg-slate-800 text-slate-100',
        props.selected ? 'border-sky-500 ring-1 ring-sky-500' : 'border-slate-600',
      ].join(' ')}
    >
      <NodeResizer
        color="#0ea5e9"
        isVisible={props.selected}
        minWidth={props.minWidth ?? 160}
        minHeight={props.minHeight ?? 80}
        handleStyle={{ width: 8, height: 8 }}
        onResizeEnd={(_e, p) =>
          updateSize(props.id, { width: Math.round(p.width), height: Math.round(p.height) })}
      />
      <div className="px-2 py-1 border-b border-slate-700 bg-slate-900 rounded-t shrink-0">
        <div className="font-semibold flex items-center gap-1.5">
          <span className="truncate">{props.id}</span>
          <HelpIcon typeName={props.type} title={`Help for ${props.type}`} />
        </div>
        <div className="text-slate-400 text-[10px] truncate">{props.type}</div>
      </div>
      <div className="p-2 flex-1 min-h-0 flex flex-col">{props.children}</div>
    </div>
  );
}

// Resolve a dot-path (e.g. "Position.X") into a nested JSON value.
function resolvePath(obj: unknown, path: string): unknown {
  return path.split('.').reduce<unknown>(
    (o, k) => (o != null && typeof o === 'object' ? (o as Record<string, unknown>)[k] : undefined),
    obj,
  );
}

function fmt(v: unknown): string {
  if (v === undefined) return '—';
  if (typeof v === 'number') return Number.isInteger(v) ? String(v) : v.toFixed(3);
  if (typeof v === 'string') return v;
  return JSON.stringify(v);
}

type Radix = 'dec' | 'hex' | 'bin';

// Parse a field entry like "Position.X:hex" into its dot-path + display radix.
// A bare ":hex"/":bin" (empty path) targets the whole value / each list item.
function parseField(f: string): { path: string; radix: Radix } {
  const m = /:(hex|bin)$/.exec(f);
  if (m) return { path: f.slice(0, m.index), radix: m[1] as Radix };
  return { path: f, radix: 'dec' };
}

// Format an integer-valued number (or bool) in the given radix; fall back to the
// default fmt() for non-integers, strings, objects, and undefined.
function fmtRadix(v: unknown, radix: Radix): string {
  if (radix !== 'dec') {
    const n = typeof v === 'boolean' ? (v ? 1 : 0) : v;
    if (typeof n === 'number' && Number.isInteger(n)) {
      const s = Math.abs(n).toString(radix === 'hex' ? 16 : 2);
      const body = radix === 'hex' ? '0x' + s.toUpperCase() : '0b' + s;
      return (n < 0 ? '-' : '') + body;
    }
  }
  return fmt(v);
}

// Radix to apply to a whole value / list item: the first bare-notation field
// (empty path) with a hex/bin radix, else decimal.
function wholeRadix(fields: string[]): Radix {
  for (const f of fields) {
    const p = parseField(f);
    if (p.path === '' && p.radix !== 'dec') return p.radix;
  }
  return 'dec';
}

// The live tap key feeding a display node's `in` input (the upstream output
// port's tap, namespaced by the active group path). Returns '' when unwired.
function useIncomingTapKey(id: string): string {
  const graph = useGraphStore(s => s.graph);
  const groupStack = useGraphStore(s => s.groupStack);
  const inEdge = graph?.edges.find(e => e.to === `${id}.in`);
  if (!inEdge || !graph) return '';
  const prefix = groupStack.length ? groupStack.map(g => g.id).join('/') + '/' : '';
  return resolveTapKey(inEdge.from, prefix, { nodes: graph.nodes, edges: graph.edges });
}

// The live value flowing INTO a display node = the live tap of the upstream
// output port wired to its `in` input.
function useIncomingLive(id: string): unknown {
  const key  = useIncomingTapKey(id);
  const live = useGraphStore(s => s.live);
  return key ? live[key]?.value : undefined;
}

// Track the live pixel size of an element (so the chart SVG scales with the
// node). Returns a ref to attach plus the measured width/height.
function useElementSize<T extends HTMLElement>(): [React.RefObject<T>, { w: number; h: number }] {
  const ref = useRef<T>(null);
  const [size, setSize] = useState({ w: 0, h: 0 });
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const ro = new ResizeObserver(entries => {
      const r = entries[0]?.contentRect;
      if (r) setSize({ w: r.width, h: r.height });
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return [ref, size];
}

// ---------------------------------------------------------------------------
export function TriggerButtonView({ id, type, selected }: { id: string; type: string; selected: boolean }) {
  const graph = useGraphStore(s => s.graph);
  const node  = graph?.nodes.find(n => n.id === id);
  const label = (node?.config?.label as string) || 'Trigger';
  const mode  = (node?.config?.mode as string) || 'pushbutton';  // 'pushbutton' | 'switch'
  const [val, setVal] = useState(false);
  const [err, setErr] = useState(false);

  const send = (next: boolean) => {
    setVal(next);
    api.button(id, next).then(() => setErr(false)).catch(() => setErr(true));
  };

  // Switch: click toggles and emits the new state.
  // Pushbutton: emits true while held (press), false on release.
  const momentary = mode === 'pushbutton';
  const handlers = momentary
    ? {
        onMouseDown: (e: ReactMouseEvent) => { e.stopPropagation(); send(true); },
        onMouseUp:   (e: ReactMouseEvent) => { e.stopPropagation(); send(false); },
        onMouseLeave: () => { if (val) send(false); },
      }
    : {
        onMouseDown: (e: ReactMouseEvent) => e.stopPropagation(),
        onClick: () => send(!val),
      };

  return (
    <Frame id={id} type={type} selected={selected} minWidth={140} minHeight={90}>
      <div className="flex-1 min-h-0 flex flex-col items-stretch gap-1 justify-center">
        <button
          className={`nodrag flex-1 min-h-0 px-2 py-1.5 rounded text-sm font-medium ${
            val ? 'bg-emerald-600 hover:bg-emerald-500' : 'bg-slate-600 hover:bg-slate-500'
          }`}
          {...handlers}
          title={momentary ? 'Hold to emit true; release emits false' : 'Click to toggle and emit the boolean'}
        >
          {label}: {val ? 'true' : 'false'}
        </button>
        {err && <div className="text-rose-400 text-[10px]">send failed (is the graph running?)</div>}
      </div>
      <Handle
        type="source"
        position={Position.Right}
        id="out"
        style={{ ...dot('flowboard::Bool'), right: -5, top: '50%', transform: 'translateY(-50%)' }}
      />
    </Frame>
  );
}

// ---------------------------------------------------------------------------
export function ValueDisplayView({ id, type, selected }: { id: string; type: string; selected: boolean }) {
  const graph  = useGraphStore(s => s.graph);
  const node   = graph?.nodes.find(n => n.id === id);
  const fields = (node?.config?.fields as string[] | undefined) ?? [];
  const value  = useIncomingLive(id);
  const inputType = (node?.config?.inputType as string) || 'flowboard::Double';

  return (
    <Frame id={id} type={type} selected={selected} minWidth={160} minHeight={110}>
      <Handle
        type="target"
        position={Position.Left}
        id="in"
        style={{ ...dot(inputType), left: -5, top: 28 }}
      />
      <div className="flex-1 min-h-0 overflow-auto nowheel">
        {value === undefined ? (
          <div className="text-slate-500 italic">no value</div>
        ) : isListValue(value) ? (
          <ListItems items={value.items} fields={fields} />
        ) : fields.length > 0 && value !== null && typeof value === 'object' ? (
          <div className="grid grid-cols-[auto_1fr] gap-x-2 gap-y-0.5 font-mono">
            {fields.map(f => {
              const { path, radix } = parseField(f);
              return <Fragmented key={f} label={path} val={resolvePath(value, path)} radix={radix} />;
            })}
          </div>
        ) : (
          <div className="font-mono text-emerald-300 break-all">{fmtRadix(value, wholeRadix(fields))}</div>
        )}
      </div>
    </Frame>
  );
}

// A ListValue serializes to { items: [...], elementTypeTag }.
function isListValue(v: unknown): v is { items: unknown[] } {
  return v != null && typeof v === 'object' && Array.isArray((v as { items?: unknown }).items);
}

function ListItems({ items, fields }: { items: unknown[]; fields: string[] }) {
  if (items.length === 0) return <div className="text-slate-500 italic">(empty list)</div>;
  return (
    <div className="flex flex-col gap-0.5 font-mono text-[11px]">
      {items.map((item, i) => (
        <div key={i} className="text-emerald-300 break-all border-b border-slate-700/40 py-0.5">
          <span className="text-slate-500 mr-1">{i}:</span>
          {fields.length > 0 && item !== null && typeof item === 'object'
            ? fields.map(f => { const { path, radix } = parseField(f); return `${path}=${fmtRadix(resolvePath(item, path), radix)}`; }).join('  ')
            : fmtRadix(item, wholeRadix(fields))}
        </div>
      ))}
    </div>
  );
}

function Fragmented({ label, val, radix = 'dec' }: { label: string; val: unknown; radix?: Radix }) {
  return (
    <>
      <span className="text-slate-400">{label}</span>
      <span className="text-emerald-300 text-right break-all">{fmtRadix(val, radix)}</span>
    </>
  );
}

// ---------------------------------------------------------------------------
type Sample = { t: number; v: number };

export function GraphDisplayView({ id, type, selected }: { id: string; type: string; selected: boolean }) {
  const graph = useGraphStore(s => s.graph);
  const node  = graph?.nodes.find(n => n.id === id);
  const cfg   = (node?.config ?? {}) as Record<string, unknown>;
  const inputType = (cfg.inputType as string) || 'flowboard::Double';
  const windowSec = Number(cfg.timeWindowSec ?? 30);
  const autoScale = cfg.autoScale !== false;
  const yMinCfg   = Number(cfg.yMin ?? 0);
  const yMaxCfg   = Number(cfg.yMax ?? 1);

  const tapKey = useIncomingTapKey(id);
  const samplesRef = useRef<Sample[]>([]);
  const [, force] = useState(0);
  const [chartRef, chartSize] = useElementSize<HTMLDivElement>();

  // Sample at the data source: subscribe to the store so every live update is
  // captured synchronously when its WebSocket message arrives. A render-driven
  // effect would instead miss intermediate values whenever React coalesces
  // re-renders — which is exactly what happens in a backgrounded tab (and under
  // rapid updates), making the chart skip samples / look frozen on return.
  useEffect(() => {
    if (!tapKey) return;
    const append = (raw: unknown) => {
      if (typeof raw !== 'number' && typeof raw !== 'string' && typeof raw !== 'boolean') return;
      const num = Number(raw);  // bool → 0/1
      if (Number.isNaN(num)) return;
      const now = Date.now();
      const arr = samplesRef.current;
      arr.push({ t: now, v: num });
      const cutoff = now - windowSec * 1000;
      while (arr.length >= 2 && arr[1].t <= cutoff) arr.shift();
      force(n => n + 1);
    };
    // Seed with the current value, then capture each subsequent change.
    append(useGraphStore.getState().live[tapKey]?.value);
    return useGraphStore.subscribe((state, prev) => {
      const cur = state.live[tapKey];
      if (cur && cur !== prev.live[tapKey]) append(cur.value);
    });
  }, [tapKey, windowSec]);

  // Scroll the window / hold the last value out to "now" even when no new value
  // arrives. (Throttled in a hidden tab, but the samples above are captured
  // regardless, so the chart is correct once the tab is visible again.)
  useEffect(() => {
    const id = setInterval(() => force(n => n + 1), 100);
    return () => clearInterval(id);
  }, []);

  // SVG dimensions track the measured chart container (fallback before the
  // first ResizeObserver callback so the math never divides by zero).
  const W = Math.max(chartSize.w || 220, 40);
  const H = Math.max(chartSize.h || 90, 30);
  const PAD = 4;
  const samples = samplesRef.current;
  const now = Date.now();
  const tMin = now - windowSec * 1000;
  const vals = samples.map(s => s.v);
  let yMin = autoScale ? (vals.length ? Math.min(...vals) : 0) : yMinCfg;
  let yMax = autoScale ? (vals.length ? Math.max(...vals) : 1) : yMaxCfg;
  // Add vertical headroom so the line is never clipped at the top/bottom edge
  // (a point at the max/min would otherwise sit exactly on the SVG border).
  // Flat data (e.g. a constant 3) → yMin==yMax, so pad around the value.
  if (yMax - yMin < 1e-9) {
    const pad = Math.max(Math.abs(yMax) * 0.1, 1);
    yMin -= pad;
    yMax += pad;
  } else {
    const pad = (yMax - yMin) * 0.1;  // 10% headroom each side
    yMin -= pad;
    yMax += pad;
  }
  const ySpan = yMax - yMin || 1;
  const xOf = (t: number) => PAD + ((t - tMin) / (windowSec * 1000)) * (W - 2 * PAD);
  const yOf = (v: number) => H - PAD - ((v - yMin) / ySpan) * (H - 2 * PAD);
  // Sample-and-hold (step) line: each value persists until the next sample, and
  // the most recent value is held out to "now" (the right edge).
  const stepPts: string[] = [];
  if (samples.length > 0) {
    const x0 = Math.max(xOf(samples[0].t), PAD);  // clamp the held value to the left edge
    stepPts.push(`${x0.toFixed(1)},${yOf(samples[0].v).toFixed(1)}`);
    for (let i = 1; i < samples.length; i++) {
      const x = xOf(samples[i].t);
      stepPts.push(`${x.toFixed(1)},${yOf(samples[i - 1].v).toFixed(1)}`);  // hold previous
      stepPts.push(`${x.toFixed(1)},${yOf(samples[i].v).toFixed(1)}`);      // step to current
    }
    stepPts.push(`${xOf(now).toFixed(1)},${yOf(samples[samples.length - 1].v).toFixed(1)}`);  // hold to now
  }
  const points = stepPts.join(' ');
  const last = vals.length ? vals[vals.length - 1] : undefined;

  return (
    <Frame id={id} type={type} selected={selected} minWidth={180} minHeight={130}>
      <Handle
        type="target"
        position={Position.Left}
        id="in"
        style={{ ...dot(inputType), left: -5, top: 28 }}
      />
      <div ref={chartRef} className="flex-1 min-h-0">
        <svg
          width={W} height={H}
          className="block bg-slate-900 rounded border border-slate-700"
        >
          {samples.length >= 1 && (
            <polyline points={points} fill="none" stroke="#34d399" strokeWidth={1.5} />
          )}
          <text x={PAD} y={10} fill="#64748b" fontSize={9}>{yMax.toFixed(2)}</text>
          <text x={PAD} y={H - 2} fill="#64748b" fontSize={9}>{yMin.toFixed(2)}</text>
        </svg>
      </div>
      <div className="flex justify-between text-[10px] text-slate-400 mt-1 shrink-0">
        <span>{windowSec}s window</span>
        <span className="font-mono text-emerald-300">{last === undefined ? '—' : fmt(last)}</span>
      </div>
    </Frame>
  );
}
