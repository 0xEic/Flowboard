// SPDX-License-Identifier: MIT
import { NodeResizer } from 'reactflow';
import { useGraphStore } from '../../store/graph_store';

// A canvas annotation: free text only, no ports, never reaches the engine
// (stripped before send). The note is READ-ONLY on the canvas — editing text
// inline fought React Flow's key handling (lost focus / trapped edit mode), so
// the text is edited via the single "Text" field in the inspector. Resizable;
// text persists in config.text. Styled as a soft sticky note.
export function NoteNodeView({ id, selected }: { id: string; type: string; selected: boolean }) {
  const graph      = useGraphStore(s => s.graph);
  const updateSize = useGraphStore(s => s.updateNodeSize);
  const node = graph?.nodes.find(n => n.id === id);
  const text = ((node?.config ?? {}) as { text?: string }).text ?? '';

  return (
    <div
      className={[
        'relative w-full h-full rounded text-xs',
        'bg-amber-100/90 text-slate-800 border',
        selected ? 'border-amber-500 ring-1 ring-amber-500' : 'border-amber-300/70',
      ].join(' ')}
    >
      <NodeResizer
        color="#f59e0b"
        isVisible={selected}
        minWidth={120}
        minHeight={60}
        handleStyle={{ width: 8, height: 8 }}
        onResizeEnd={(_e, p) => updateSize(id, { width: Math.round(p.width), height: Math.round(p.height) })}
      />
      <div className="nowheel w-full h-full overflow-auto p-2 whitespace-pre-wrap break-words">
        {text
          ? text
          : <span className="text-amber-700/50 italic">Empty note — edit the text in the inspector</span>}
      </div>
    </div>
  );
}
