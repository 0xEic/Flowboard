// SPDX-License-Identifier: MIT
//
// In-app help drawer that documents every node type. Opens via the global
// `flowboard:show-help` event (carries an optional `typeName` payload) so any
// HelpIcon — on the canvas, in the palette, in the inspector, in the topbar —
// can open the panel and scroll directly to a node's section.
//
// Layout: a fixed right-edge drawer with a sidebar of categories on the left,
// a scrollable description list on the right, and a search box up top.

import { useEffect, useMemo, useRef, useState } from 'react';
import { NODE_HELP, helpAnchorId, type NodeHelp } from './node_help';

export const SHOW_HELP_EVENT = 'flowboard:show-help';

// Fire this to open the panel. Pass a `typeName` to scroll to that node;
// pass nothing to open at the top with no preselected node.
export function openHelp(typeName?: string) {
  window.dispatchEvent(new CustomEvent(SHOW_HELP_EVENT, { detail: { typeName } }));
}

type Section = { category: string; entries: NodeHelp[] };

// Render order for the category sidebar — mirrors NodePalette's layout so
// users find nodes in the help panel under the same headings they see in the
// palette. Categories not listed here append at the end alphabetically.
const CATEGORY_ORDER = [
  'Debug', 'Sources', 'Transform', 'Logic & Conditions', 'Lists',
  'Timing & Sync', 'Sinks', 'Input', 'Flow & Structure',
  'OnboardAPI', 'OnboardAPI · Generated', 'Other',
];

function buildSections(entries: NodeHelp[]): Section[] {
  const m = new Map<string, NodeHelp[]>();
  for (const e of entries) {
    const arr = m.get(e.category) ?? [];
    arr.push(e);
    m.set(e.category, arr);
  }
  return [...m.entries()]
    .sort(([a], [b]) => {
      const ia = CATEGORY_ORDER.indexOf(a);
      const ib = CATEGORY_ORDER.indexOf(b);
      if (ia === -1 && ib === -1) return a.localeCompare(b);
      if (ia === -1) return 1;
      if (ib === -1) return -1;
      return ia - ib;
    })
    .map(([category, arr]) => ({
      category,
      entries: arr.sort((x, y) => x.title.localeCompare(y.title)),
    }));
}

export function HelpPanel() {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [pendingType, setPendingType] = useState<string | null>(null);
  const bodyRef = useRef<HTMLDivElement>(null);

  // Listen for the global open event. The detail carries an optional
  // `typeName`; we stash it so the next layout pass can scroll to it (we
  // can't query for the element until the panel and its rows are mounted).
  useEffect(() => {
    const onShow = (e: Event) => {
      const detail = (e as CustomEvent).detail as { typeName?: string } | undefined;
      setOpen(true);
      setPendingType(detail?.typeName ?? null);
    };
    window.addEventListener(SHOW_HELP_EVENT, onShow);
    return () => window.removeEventListener(SHOW_HELP_EVENT, onShow);
  }, []);

  // Esc closes; matches the rest of the app's modal/drawer affordances.
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => { if (e.key === 'Escape') setOpen(false); };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open]);

  // After the panel opens and the rows are in the DOM, scroll to the
  // requested anchor (if any). Run on the next frame so layout has settled.
  useEffect(() => {
    if (!open || !pendingType) return;
    const id = helpAnchorId(pendingType);
    const raf = requestAnimationFrame(() => {
      const el = bodyRef.current?.querySelector<HTMLElement>(`[data-help-anchor="${id}"]`);
      if (el) {
        el.scrollIntoView({ behavior: 'smooth', block: 'start' });
        el.classList.add('ring-2', 'ring-sky-500');
        setTimeout(() => el.classList.remove('ring-2', 'ring-sky-500'), 1500);
      }
      setPendingType(null);
    });
    return () => cancelAnimationFrame(raf);
  }, [open, pendingType, query]);

  const sections = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return buildSections(NODE_HELP);
    const filtered = NODE_HELP.filter(h => {
      const hay = [h.type, h.title, h.summary, h.description, h.config, h.inputs, h.outputs]
        .filter(Boolean).join(' ').toLowerCase();
      return hay.includes(q);
    });
    return buildSections(filtered);
  }, [query]);

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 flex" role="dialog" aria-label="Node help">
      <button
        className="flex-1 bg-slate-950/60"
        onClick={() => setOpen(false)}
        aria-label="Close help"
        tabIndex={-1}
      />
      <aside className="w-[760px] max-w-full bg-slate-900 border-l border-slate-700 text-slate-100 flex flex-col shadow-2xl">
        <header className="px-4 py-3 border-b border-slate-700 flex items-center gap-3 shrink-0">
          <div className="font-semibold text-sky-300">Help · Node Reference</div>
          <input
            className="flex-1 ml-2 bg-slate-800 border border-slate-700 rounded px-2 py-1 text-xs outline-none focus:border-sky-600"
            placeholder="Search nodes (name, summary, config)…"
            value={query}
            onChange={e => setQuery(e.target.value)}
            spellCheck={false}
            autoFocus
          />
          <button
            className="px-2 py-1 text-xs rounded bg-slate-800 hover:bg-slate-700 border border-slate-700"
            onClick={() => setOpen(false)}
            title="Close (Esc)"
          >✕</button>
        </header>

        <div className="flex-1 flex min-h-0">
          {/* Category sidebar: jump-links into the body. */}
          <nav className="w-44 border-r border-slate-700 overflow-y-auto p-2 text-xs shrink-0">
            {sections.map(s => (
              <a
                key={s.category}
                href={`#${helpAnchorId('cat-' + s.category)}`}
                onClick={e => {
                  e.preventDefault();
                  const id = helpAnchorId('cat-' + s.category);
                  bodyRef.current
                    ?.querySelector<HTMLElement>(`[data-help-anchor="${id}"]`)
                    ?.scrollIntoView({ behavior: 'smooth', block: 'start' });
                }}
                className="block px-2 py-1 rounded text-slate-300 hover:bg-slate-800 hover:text-slate-100"
              >
                {s.category}
                <span className="text-slate-500 ml-1">({s.entries.length})</span>
              </a>
            ))}
            {sections.length === 0 && (
              <div className="px-2 py-1 text-slate-500 italic">No matches.</div>
            )}
          </nav>

          {/* Scrollable description list. */}
          <div ref={bodyRef} className="flex-1 overflow-y-auto p-4 space-y-6">
            {sections.length === 0 && (
              <div className="text-slate-500 italic">
                No nodes match "{query}". Try a different term.
              </div>
            )}
            {sections.map(s => (
              <section
                key={s.category}
                data-help-anchor={helpAnchorId('cat-' + s.category)}
              >
                <h2 className="text-sky-300 text-sm font-semibold uppercase tracking-wide mb-2 border-b border-slate-700 pb-1">
                  {s.category}
                </h2>
                <div className="space-y-3">
                  {s.entries.map(entry => (
                    <article
                      key={entry.type}
                      data-help-anchor={helpAnchorId(entry.type)}
                      className="rounded border border-slate-700 bg-slate-800/40 p-3 transition-shadow"
                    >
                      <header className="flex items-baseline gap-2 mb-1 flex-wrap">
                        <h3 className="font-semibold text-slate-100">{entry.title}</h3>
                        <code className="text-[10px] text-slate-400 bg-slate-900 px-1.5 py-0.5 rounded">
                          {entry.type === '__OnboardAPI_Generated__'
                            ? 'M_<X>.Service / .Client · Factory.M_<X>::<Struct>'
                            : entry.type}
                        </code>
                      </header>
                      <p className="text-sm text-slate-200 mb-2">{entry.summary}</p>
                      {entry.description && (
                        <p className="text-xs text-slate-300 whitespace-pre-wrap leading-relaxed mb-2">
                          {entry.description}
                        </p>
                      )}
                      <dl className="text-xs grid grid-cols-[auto_1fr] gap-x-3 gap-y-1">
                        {entry.inputs && (
                          <>
                            <dt className="text-emerald-400">inputs</dt>
                            <dd className="text-slate-300 whitespace-pre-wrap">{entry.inputs}</dd>
                          </>
                        )}
                        {entry.outputs && (
                          <>
                            <dt className="text-sky-400">outputs</dt>
                            <dd className="text-slate-300 whitespace-pre-wrap">{entry.outputs}</dd>
                          </>
                        )}
                        {entry.config && (
                          <>
                            <dt className="text-amber-400">config</dt>
                            <dd className="text-slate-300 whitespace-pre-wrap">{entry.config}</dd>
                          </>
                        )}
                        {entry.example && (
                          <>
                            <dt className="text-slate-400">example</dt>
                            <dd className="text-slate-300 italic">{entry.example}</dd>
                          </>
                        )}
                      </dl>
                    </article>
                  ))}
                </div>
              </section>
            ))}
            <div className="text-[10px] text-slate-500 pt-4 border-t border-slate-800">
              Tip: every "?" icon in the editor jumps straight to the matching entry here.
              Press Esc to close.
            </div>
          </div>
        </div>
      </aside>
    </div>
  );
}
