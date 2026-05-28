// SPDX-License-Identifier: MIT
import { useState } from 'react';
import { NodePalette } from './NodePalette';
import { DiscoveryPalette } from './DiscoveryPalette';

type Tab = 'palette' | 'discover';

export function Sidebar() {
  const [tab, setTab] = useState<Tab>('palette');

  const tabBtn = (id: Tab, label: string) => (
    <button
      className={`flex-1 px-2 py-1.5 text-xs font-medium border-b-2 ${
        tab === id
          ? 'border-sky-500 text-slate-100'
          : 'border-transparent text-slate-400 hover:text-slate-200'
      }`}
      onClick={() => setTab(id)}
    >
      {label}
    </button>
  );

  return (
    <div className="flex flex-col w-64 bg-slate-800 border-r border-slate-700 min-h-0">
      <div className="flex border-b border-slate-700 shrink-0">
        {tabBtn('palette', 'Palette')}
        {tabBtn('discover', 'Discover')}
      </div>
      {/* The inner palette is its own scrollable aside. min-h-0 lets this flex
          child shrink below content height, and h-full bounds the aside so its
          overflow-y-auto actually scrolls. Drop its (now redundant) left border. */}
      <div className="flex-1 min-h-0 [&>aside]:w-full [&>aside]:h-full [&>aside]:border-r-0">
        {tab === 'palette' ? <NodePalette /> : <DiscoveryPalette />}
      </div>
    </div>
  );
}
