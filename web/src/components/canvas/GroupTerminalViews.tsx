// SPDX-License-Identifier: MIT
import { Handle, Position } from 'reactflow';
import { useGraphStore } from '../../store/graph_store';
import { colorForTypeTag } from '../../lib/typeColor';
import { TypeSelect } from '../form/TypeSelect';
import { HelpIcon } from '../../help/HelpIcon';

const dot = (tag: string) => ({
  width: 10, height: 10, borderRadius: 999,
  background: colorForTypeTag(tag),
  border: '1px solid #0f172a',
});

// Boundary terminal: a Group.Input surfaces as an INPUT handle on the parent
// GroupNode, a Group.Output as an OUTPUT handle. The terminal's portName +
// typeTag are edited inline here; they define the group's exposed port.
function TerminalCard(props: {
  id: string; selected: boolean; kind: 'input' | 'output';
}) {
  const graph  = useGraphStore(s => s.graph);
  const update = useGraphStore(s => s.updateNodeConfig);
  const node   = graph?.nodes.find(n => n.id === props.id);
  const cfg    = (node?.config ?? {}) as { portName?: string; typeTag?: string };
  const portName = cfg.portName ?? '';
  const typeTag  = cfg.typeTag ?? 'flowboard::Double';

  const set = (patch: Partial<{ portName: string; typeTag: string }>) =>
    update(props.id, { ...cfg, ...patch });

  const isInput = props.kind === 'input';
  return (
    <div
      className={[
        'relative rounded border min-w-[180px] text-xs bg-slate-800 text-slate-100',
        props.selected ? 'border-sky-500 ring-1 ring-sky-500' : 'border-slate-600',
      ].join(' ')}
    >
      <div className="px-2 py-1 border-b border-slate-700 bg-slate-900 rounded-t flex items-center gap-1">
        <span className="text-amber-300 flex-1">{isInput ? '⇥ Group Input' : 'Group Output ⇥'}</span>
        <HelpIcon
          typeName={isInput ? 'Group.Input' : 'Group.Output'}
          title={`Help for ${isInput ? 'Group.Input' : 'Group.Output'}`}
        />
      </div>
      <div className="p-2 space-y-1">
        <input
          className="nodrag nowheel w-full bg-slate-900 border border-slate-700 rounded px-1.5 py-0.5 text-[11px] outline-none focus:border-sky-600"
          value={portName}
          placeholder="port name"
          onChange={e => set({ portName: e.target.value })}
          onMouseDown={e => e.stopPropagation()}
          spellCheck={false}
        />
        <div className="nodrag nowheel" onMouseDown={e => e.stopPropagation()}>
          <TypeSelect value={typeTag} onChange={t => set({ typeTag: t })} />
        </div>
      </div>
      <Handle
        type={isInput ? 'source' : 'target'}
        position={isInput ? Position.Right : Position.Left}
        id={isInput ? 'out' : 'in'}
        style={{ ...dot(typeTag), [isInput ? 'right' : 'left']: -5, top: '50%', transform: 'translateY(-50%)' }}
      />
    </div>
  );
}

export function GroupInputView({ id, selected }: { id: string; type: string; selected: boolean }) {
  return <TerminalCard id={id} selected={selected} kind="input" />;
}
export function GroupOutputView({ id, selected }: { id: string; type: string; selected: boolean }) {
  return <TerminalCard id={id} selected={selected} kind="output" />;
}
