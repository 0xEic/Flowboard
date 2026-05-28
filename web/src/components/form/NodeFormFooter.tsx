// SPDX-License-Identifier: MIT
import { useGraphStore } from '../../store/graph_store';

type Props = {
  nodeId: string;
  showJson: boolean;
  onToggleJson: () => void;
};

export function NodeFormFooter({ nodeId, showJson, onToggleJson }: Props) {
  const revert = useGraphStore(s => s.revertNodeConfig);
  return (
    <div className="border-t border-slate-700 pt-2 mt-2">
      <div className="flex items-center gap-2">
        <button
          className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm"
          onClick={() => revert(nodeId)}
        >Revert</button>
        <button
          className="px-3 py-1 bg-slate-700 hover:bg-slate-600 rounded text-sm ml-auto"
          onClick={onToggleJson}
        >{showJson ? 'Hide JSON' : 'Show JSON'}</button>
      </div>
      <div className="text-xs text-slate-500 mt-2">
        Edits are live in the canvas. Use top-bar "Apply &amp; Reload" to push to the running server.
      </div>
    </div>
  );
}
