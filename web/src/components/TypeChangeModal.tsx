// SPDX-License-Identifier: MIT
import { useGraphStore } from '../store/graph_store';
import { useConfirmKeys } from '../lib/useConfirmKeys';

// Confirmation prompt for inputType / outputType changes coming from the
// Inspector. A small centred card with explicit Cancel + apply buttons; also
// bound to Enter = confirm / Esc = cancel via the shared useConfirmKeys hook.
export function TypeChangeModal() {
  const pending = useGraphStore(s => s.pendingTypeChange);
  const confirm = useGraphStore(s => s.confirmTypeChange);
  const cancel  = useGraphStore(s => s.cancelTypeChange);

  useConfirmKeys(!!pending, { onConfirm: confirm, onCancel: cancel });

  if (!pending) return null;
  const side = pending.field === 'inputType' ? 'input' : 'output';

  return (
    <div className="fixed inset-0 bg-black/60 flex items-center justify-center z-50">
      <div className="bg-slate-800 border border-slate-700 rounded-lg p-4 max-w-md w-[28rem] text-sm text-slate-100 shadow-xl">
        <div className="font-semibold text-base mb-2">Change {side} type?</div>
        <div className="mb-3 text-slate-300 leading-snug">
          Node <code className="text-amber-300">{pending.nodeId}</code> will switch its
          {' '}{side} type from{' '}
          <code className="text-amber-300 break-all">{pending.oldValue || '(unset)'}</code>
          {' '}to{' '}
          <code className="text-amber-300 break-all">{pending.newValue || '(unset)'}</code>.
        </div>
        <div className="mb-4 text-slate-400 text-xs leading-snug">
          The node's port types change with it. Any existing connections that
          relied on the old type will become mismatched (red) and need to be
          reconnected.
        </div>
        <div className="flex items-center justify-between gap-2">
          <span className="text-[10px] text-slate-500">Enter = yes · Esc = no</span>
          <div className="flex gap-2">
            <button
              className="px-3 py-1 rounded border border-slate-600 hover:bg-slate-700"
              onClick={cancel}
            >
              Cancel
            </button>
            <button
              className="px-3 py-1 rounded bg-sky-700 hover:bg-sky-600"
              onClick={confirm}
            >
              Change type
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
