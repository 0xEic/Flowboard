// SPDX-License-Identifier: MIT
import { useMemo, useState } from 'react';
import { useGraphStore } from '../store/graph_store';
import { HelpIcon } from '../help/HelpIcon';
import type { CatalogEntry } from '../api/types';

// ---------------------------------------------------------------------------
// Palette grouping (presentation only — engine type names are unchanged).
//
// Non-OnboardAPI nodes are organized into curated categories, ordered by data
// flow. OnboardAPI nodes keep their per-module grouping (Service / Client and
// the associated Factory.* struct builders collapse into one M_Foo group) and
// sort to the bottom of the list.
// ---------------------------------------------------------------------------

// Curated layout: category render order, and the node types in each (in order).
const PALETTE_LAYOUT: ReadonlyArray<{ category: string; types: readonly string[] }> = [
  { category: 'Debug', types: [
    'Debug.ValueDisplay', 'Debug.GraphDisplay', 'Debug.TriggerButton',
  ] },
  { category: 'Sources', types: [
    'Sources.CurrentTime', 'Sources.TimeSource', 'Sources.Timer',
    'Sources.Iterator', 'Transform.ConstantSource', 'Sources.Random',
  ] },
  { category: 'Transform', types: [
    'Transform.Convert', 'Transform.Extract', 'Transform.Inverter',
    'Transform.Pulse', 'Transform.Derivative', 'Transform.Arithmetic',
    'Transform.NumberHolder',
    'Manipulate.Number', 'Manipulate.String',
  ] },
  { category: 'Logic & Conditions', types: [
    'Transform.Compare', 'Transform.Threshold', 'Transform.Filter',
  ] },
  { category: 'Lists', types: [
    'Transform.List.Build', 'Transform.List.Constant', 'Transform.List.Accumulate',
    'Transform.List.Combine', 'Transform.List.Filter', 'Transform.List.Find',
    'Transform.List.GetAt', 'Transform.List.MapField', 'Transform.List.Size',
    'Transform.List.Sort',
  ] },
  { category: 'Bytes', types: ['Bytes.Pack', 'Bytes.Unpack'] },
  { category: 'Timing & Sync', types: [
    'Transform.Delay', 'Transform.Throttle', 'Transform.Synchronize', 'Transform.KeyValueAccumulator',
  ] },
  { category: 'Sinks', types: ['Sinks.Log'] },
  { category: 'Input', types: ['Input.ButtonHandler'] },
  { category: 'Flow & Structure', types: [
    'Flow.StateMachine', 'Flow.Group', 'Note', 'Group.Input', 'Group.Output',
  ] },
  { category: 'OnboardAPI', types: ['OnboardApi.Discovery', 'OnboardApi.DeviceReport'] },
];

const CATEGORY_ORDER = new Map<string, number>(
  PALETTE_LAYOUT.map((g, i) => [g.category, i]),
);
const CATEGORY_OF = new Map<string, string>();
const TYPE_ORDER   = new Map<string, number>();
PALETTE_LAYOUT.forEach(g => g.types.forEach(t => {
  CATEGORY_OF.set(t, g.category);
  TYPE_ORDER.set(t, TYPE_ORDER.size);
}));

// Labels that don't read well from a plain prefix strip.
const LABEL_OVERRIDE: Record<string, string> = {
  'Manipulate.Number': 'Manipulate Number',
  'Manipulate.String': 'Manipulate String',
  'Group.Input': 'Group Input',
  'Group.Output': 'Group Output',
};

function isOnboardModuleType(typeName: string): boolean {
  return typeName.startsWith('M_') || typeName.startsWith('Factory.');
}

// "M_Alert.Service" / "M_Alert.Client" / "Factory.M_Alert::X" → "M_Alert".
function onboardModuleKey(typeName: string): string {
  if (typeName.startsWith('Factory.')) {
    const rest = typeName.slice('Factory.'.length);
    const sep = rest.indexOf('::');
    return sep < 0 ? 'Factory' : rest.slice(0, sep);
  }
  const dot = typeName.indexOf('.');
  return dot < 0 ? typeName : typeName.slice(0, dot);
}

function paletteGroup(typeName: string): string {
  if (isOnboardModuleType(typeName)) return onboardModuleKey(typeName);
  const cat = CATEGORY_OF.get(typeName);
  if (cat) return cat;
  // Unknown/new node: fall back to its first-dot prefix so it still appears.
  const dot = typeName.indexOf('.');
  return dot < 0 ? typeName : typeName.slice(0, dot);
}

function isOnboardModuleGroup(group: string): boolean {
  return group.startsWith('M_') || group === 'Factory';
}

// Render-order bucket: curated categories first, then unknown groups, then the
// OnboardAPI modules at the bottom.
function groupBucket(group: string): number {
  if (isOnboardModuleGroup(group)) return 2;
  if (CATEGORY_ORDER.has(group))   return 0;
  return 1;
}

// Sort order inside an M_Foo group: Service, Client, then Factory.* alphabetical.
function onboardEntryRank(entry: CatalogEntry): number {
  if (entry.typeName.endsWith('.Service')) return 0;
  if (entry.typeName.endsWith('.Client'))  return 1;
  if (entry.typeName.startsWith('Factory.')) return 2;
  return 3;
}

function onboardEntryLabel(entry: CatalogEntry, group: string): string {
  if (entry.typeName.startsWith('Factory.')) {
    const rest = entry.typeName.slice('Factory.'.length);
    const sep = rest.indexOf('::');
    if (sep >= 0 && rest.slice(0, sep) === group) {
      return `Factory: ${rest.slice(sep + 2)}`;
    }
    return entry.typeName;
  }
  if (entry.typeName.startsWith(group + '.'))  return entry.typeName.slice(group.length + 1);
  if (entry.typeName.startsWith(group + '::')) return entry.typeName.slice(group.length + 2);
  return entry.typeName;
}

// Curated-category label: explicit override, else drop the first dot-segment.
function categoryLabel(typeName: string): string {
  const o = LABEL_OVERRIDE[typeName];
  if (o) return o;
  const dot = typeName.indexOf('.');
  return dot < 0 ? typeName : typeName.slice(dot + 1);
}

function entryLabel(entry: CatalogEntry, group: string): string {
  return isOnboardModuleGroup(group)
    ? onboardEntryLabel(entry, group)
    : categoryLabel(entry.typeName);
}

// Frontend-only nodes (not in the engine catalog). Flow.Group and Note are
// always available; the boundary terminals only make sense while editing a group.
const GROUP_ENTRY: CatalogEntry = {
  typeName: 'Flow.Group', inputs: [], outputs: [],
  configDefaults: { group: { nodes: [], edges: [] } },
};
const NOTE_ENTRY: CatalogEntry = {
  typeName: 'Note', inputs: [], outputs: [],
  configDefaults: { text: '' },
};
const TERMINAL_ENTRIES: CatalogEntry[] = [
  { typeName: 'Group.Input',  inputs: [], outputs: [{ name: 'out', typeTag: 'flowboard::Double', direction: 'output' }],
    configDefaults: { portName: '', typeTag: 'flowboard::Double' } },
  { typeName: 'Group.Output', inputs: [{ name: 'in', typeTag: 'flowboard::Double', direction: 'input' }], outputs: [],
    configDefaults: { portName: '', typeTag: 'flowboard::Double' } },
];

export function NodePalette() {
  const catalog       = useGraphStore(s => s.catalog);
  const editingGroup  = useGraphStore(s => s.groupStack.length > 0);
  const [search, setSearch] = useState('');
  const q = search.trim().toLowerCase();

  // Synthetic entries are dragged with their config payload so the dropped node
  // is initialized (they aren't in the store catalog addNodeFromType reads).
  const synthetic = useMemo(
    () => [GROUP_ENTRY, NOTE_ENTRY, ...(editingGroup ? TERMINAL_ENTRIES : [])],
    [editingGroup],
  );
  const syntheticNames = useMemo(() => new Set(synthetic.map(s => s.typeName)), [synthetic]);

  const filtered = useMemo(() => {
    const all = [...synthetic, ...catalog];
    return q ? all.filter(c => c.typeName.toLowerCase().includes(q)) : all;
  }, [catalog, synthetic, q]);

  const groups = useMemo(() => {
    const m = new Map<string, CatalogEntry[]>();
    for (const c of filtered) {
      const k = paletteGroup(c.typeName);
      const arr = m.get(k) ?? [];
      arr.push(c);
      m.set(k, arr);
    }
    for (const [group, arr] of m) {
      if (isOnboardModuleGroup(group)) {
        arr.sort((a, b) => {
          const ra = onboardEntryRank(a), rb = onboardEntryRank(b);
          return ra !== rb ? ra - rb : a.typeName.localeCompare(b.typeName);
        });
      } else {
        arr.sort((a, b) => {
          const oa = TYPE_ORDER.get(a.typeName) ?? Number.MAX_SAFE_INTEGER;
          const ob = TYPE_ORDER.get(b.typeName) ?? Number.MAX_SAFE_INTEGER;
          return oa !== ob ? oa - ob : a.typeName.localeCompare(b.typeName);
        });
      }
    }
    return [...m.entries()].sort(([a], [b]) => {
      const ba = groupBucket(a), bb = groupBucket(b);
      if (ba !== bb) return ba - bb;
      if (ba === 0) return (CATEGORY_ORDER.get(a) ?? 0) - (CATEGORY_ORDER.get(b) ?? 0);
      return a.localeCompare(b);
    });
  }, [filtered]);

  return (
    <aside className="w-64 bg-slate-800 border-r border-slate-700 overflow-y-auto p-3 text-sm">
      <div className="font-semibold mb-2">Palette</div>
      <input
        className="w-full mb-3 px-2 py-1 bg-slate-900 border border-slate-700 rounded text-xs outline-none focus:border-sky-600"
        placeholder="Search nodes…"
        value={search}
        onChange={e => setSearch(e.target.value)}
        spellCheck={false}
      />
      {groups.map(([group, items]) => (
        <div key={group} className="mb-3">
          <div className="text-slate-400 uppercase text-xs mb-1">{group}</div>
          {items.map(c => (
            <div
              key={c.typeName}
              className="px-2 py-1 rounded hover:bg-slate-700 text-xs cursor-grab active:cursor-grabbing select-none flex items-center gap-1"
              title={`${c.typeName}\n${c.inputs.length} in · ${c.outputs.length} out`}
              draggable
              onDragStart={e => {
                e.dataTransfer.setData('application/flowboard-node-type', c.typeName);
                // Synthetic (frontend-only) entries carry their config payload so
                // the dropped node is initialized — addNodeFromType can't find
                // them in the engine catalog.
                if (syntheticNames.has(c.typeName) && c.configDefaults)
                  e.dataTransfer.setData('application/flowboard-node-config', JSON.stringify(c.configDefaults));
                e.dataTransfer.effectAllowed = 'copy';
              }}
            >
              <span className="flex-1 truncate">{entryLabel(c, group)}</span>
              <HelpIcon typeName={c.typeName} title={`Help for ${c.typeName}`} />
            </div>
          ))}
        </div>
      ))}
      {groups.length === 0 && (
        <div className="text-slate-500 text-xs">
          {catalog.length === 0 ? 'Loading catalog…' : 'No nodes match the search.'}
        </div>
      )}
    </aside>
  );
}
