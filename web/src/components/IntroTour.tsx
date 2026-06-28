// SPDX-License-Identifier: MIT
import { useEffect, useLayoutEffect, useState } from 'react';

// Bump when the tour content changes meaningfully so returning users see the
// updated guide on their next visit (instead of staying silently dismissed).
const SEEN_KEY = 'flowboard.intro.v1.seen';
// Custom event used by the TopBar "?" button (and anywhere else) to re-open
// the tour on demand. Decoupling avoids passing a store action through props.
export const SHOW_INTRO_EVENT = 'flowboard:show-intro';

type Placement = 'right' | 'left' | 'bottom' | 'top' | 'center';
type Step = {
  selector: string;            // data-tour attribute value to highlight, or '' for centered welcome
  title: string;
  body: string;
  placement: Placement;
};

const STEPS: Step[] = [
  {
    selector: '',
    title: 'Welcome to Flowboard',
    body:
      'This quick tour points out the main parts of the editor — the canvas, the side panels, the top bar, and the Apply & Reload button. Takes about 30 seconds.',
    placement: 'center',
  },
  {
    selector: 'topbar',
    title: 'Top Bar',
    body:
      'Project name and breadcrumbs on the left; actions on the right — Auto-arrange, Apply & Reload, Save/Load, and Pause/Resume the running engine.',
    placement: 'bottom',
  },
  {
    selector: 'sidebar',
    title: 'Left Panel — Palette & Discover',
    body:
      'Drag node types from the Palette tab onto the canvas. The Discover tab lists devices and signals detected by the engine for quick wiring.',
    placement: 'right',
  },
  {
    selector: 'canvas',
    title: 'Canvas',
    body:
      'Wire nodes together by dragging from one port to another. Ctrl+drag rubber-band selects, Delete removes the selection, Ctrl+Z undoes.',
    placement: 'right',
  },
  {
    selector: 'inspector',
    title: 'Right Panel — Inspector',
    body:
      'Shows details for whatever is selected: a node\'s config form, a link\'s type info, or graph-level settings when nothing is selected.',
    placement: 'left',
  },
  {
    selector: 'apply',
    title: 'Apply & Reload',
    body:
      'Edits to the graph are local until you click Apply & Reload (Ctrl+S). That compiles the graph and loads it into the running engine — any validation errors are reported here.',
    placement: 'bottom',
  },
];

type Rect = { top: number; left: number; width: number; height: number };

function measure(selector: string): Rect | null {
  if (!selector) return null;
  const el = document.querySelector<HTMLElement>(`[data-tour="${selector}"]`);
  if (!el) return null;
  const r = el.getBoundingClientRect();
  return { top: r.top, left: r.left, width: r.width, height: r.height };
}

export function IntroTour() {
  const [open, setOpen]   = useState(false);
  const [index, setIndex] = useState(0);
  const [rect, setRect]   = useState<Rect | null>(null);

  // Auto-open on first visit; later opens come from the SHOW_INTRO_EVENT.
  useEffect(() => {
    try { if (!localStorage.getItem(SEEN_KEY)) setOpen(true); } catch { /* private mode */ }
    const onShow = () => { setIndex(0); setOpen(true); };
    window.addEventListener(SHOW_INTRO_EVENT, onShow);
    return () => window.removeEventListener(SHOW_INTRO_EVENT, onShow);
  }, []);

  // Recompute the spotlight rect on step change, window resize, and after a
  // brief delay — some panels mount asynchronously (catalog load, selection
  // changes) and may not be in the DOM on the first tick.
  useLayoutEffect(() => {
    if (!open) return;
    const sel = STEPS[index].selector;
    let raf = 0;
    const update = () => setRect(measure(sel));
    update();
    // Re-measure for a few frames in case the target hasn't mounted yet.
    let tries = 0;
    const poll = () => {
      if (!open) return;
      const r = measure(sel);
      if (r) { setRect(r); return; }
      if (++tries < 20) raf = requestAnimationFrame(poll);
    };
    raf = requestAnimationFrame(poll);
    window.addEventListener('resize', update);
    window.addEventListener('scroll', update, true);
    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', update);
      window.removeEventListener('scroll', update, true);
    };
  }, [open, index]);

  if (!open) return null;

  const step = STEPS[index];
  const isLast  = index === STEPS.length - 1;
  const isFirst = index === 0;

  const finish = () => {
    try { localStorage.setItem(SEEN_KEY, '1'); } catch { /* private mode */ }
    setOpen(false);
  };

  // Tooltip placement: anchor near the highlighted element with a small gap.
  // Falls back to centered when no target (welcome) or the element isn't found.
  const TIP_W = 340;
  const TIP_H_EST = 180;  // rough estimate for off-screen clamping
  const GAP = 14;
  const tipStyle: React.CSSProperties = { width: TIP_W };
  if (rect && step.placement !== 'center') {
    let top = 0, left = 0;
    switch (step.placement) {
      case 'right':  left = rect.left + rect.width + GAP; top = rect.top + 12; break;
      case 'left':   left = rect.left - TIP_W - GAP;      top = rect.top + 12; break;
      case 'bottom': top  = rect.top + rect.height + GAP; left = Math.max(rect.left, 8); break;
      case 'top':    top  = rect.top - TIP_H_EST - GAP;   left = Math.max(rect.left, 8); break;
    }
    const maxLeft = window.innerWidth  - TIP_W - 8;
    const maxTop  = window.innerHeight - TIP_H_EST - 8;
    tipStyle.left = Math.max(8, Math.min(left, maxLeft));
    tipStyle.top  = Math.max(8, Math.min(top,  maxTop));
  } else {
    tipStyle.left = '50%';
    tipStyle.top  = '50%';
    tipStyle.transform = 'translate(-50%, -50%)';
  }

  // SVG mask with a rounded cut-out gives the "dim everything except the
  // target" spotlight effect in one element (no four-rect math).
  const PAD = 6;
  const spotlight = rect && step.placement !== 'center' ? (
    <svg className="absolute inset-0 w-full h-full pointer-events-none">
      <defs>
        <mask id="intro-hole">
          <rect width="100%" height="100%" fill="white" />
          <rect
            x={rect.left - PAD}
            y={rect.top  - PAD}
            width={rect.width  + PAD * 2}
            height={rect.height + PAD * 2}
            rx={8}
            fill="black"
          />
        </mask>
      </defs>
      <rect width="100%" height="100%" fill="rgba(15,23,42,0.65)" mask="url(#intro-hole)" />
      <rect
        x={rect.left - PAD}
        y={rect.top  - PAD}
        width={rect.width  + PAD * 2}
        height={rect.height + PAD * 2}
        rx={8}
        fill="none"
        stroke="rgb(56,189,248)"
        strokeWidth={2}
      />
    </svg>
  ) : (
    <div className="absolute inset-0 bg-slate-950/70" />
  );

  return (
    <div className="fixed inset-0 z-50">
      {spotlight}
      <div
        className="absolute bg-slate-800 border border-slate-600 rounded-lg shadow-xl p-4 text-slate-100"
        style={tipStyle}
        role="dialog"
        aria-label={step.title}
      >
        <div className="flex items-center justify-between mb-2">
          <div className="font-semibold text-sky-300">{step.title}</div>
          <div className="text-xs text-slate-400">{index + 1} / {STEPS.length}</div>
        </div>
        <div className="text-sm leading-relaxed text-slate-200">{step.body}</div>
        <div className="mt-4 flex items-center gap-2">
          <button
            className="text-xs text-slate-400 hover:text-slate-200"
            onClick={finish}
          >Skip tour</button>
          <div className="ml-auto flex items-center gap-2">
            <button
              className="px-2.5 py-1 rounded text-sm bg-slate-700 hover:bg-slate-600 disabled:opacity-40 disabled:hover:bg-slate-700"
              onClick={() => setIndex(i => Math.max(0, i - 1))}
              disabled={isFirst}
            >Back</button>
            {isLast ? (
              <button
                className="px-3 py-1 rounded text-sm bg-sky-700 hover:bg-sky-600"
                onClick={finish}
              >Got it</button>
            ) : (
              <button
                className="px-3 py-1 rounded text-sm bg-sky-700 hover:bg-sky-600"
                onClick={() => setIndex(i => Math.min(STEPS.length - 1, i + 1))}
              >Next</button>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
