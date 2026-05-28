// SPDX-License-Identifier: MIT
import { useState, useEffect, useRef } from 'react';
import Form from '@rjsf/core';
import validator from '@rjsf/validator-ajv8';
import type { RJSFSchema } from '@rjsf/utils';
import { useGraphStore } from '../../store/graph_store';
import { tailwindWidgets, tailwindTemplates } from './RjsfTheme';
import { NodeFormFooter } from './NodeFormFooter';
import { NodeFormJsonView } from './NodeFormJsonView';
import { ConstantSourceForm } from './ConstantSourceForm';
import { ExtractForm } from './ExtractForm';
import { LogSinkForm } from './LogSinkForm';
import { ValueDisplayForm } from './ValueDisplayForm';
import { KeyValueAccumulatorForm } from './KeyValueAccumulatorForm';
import { ListConstantForm } from './ListConstantForm';
import { ListElemForm } from './ListElemForm';
import { ButtonHandlerForm } from './ButtonHandlerForm';
import { ListPredicateForm } from './ListPredicateForm';
import { ConvertForm } from './ConvertForm';
import { CompareForm } from './CompareForm';
import { FilterForm } from './FilterForm';
import { ThresholdForm } from './ThresholdForm';
import { ThrottleForm } from './ThrottleForm';
import { SyncForm } from './SyncForm';
import { NoteForm } from './NoteForm';
import { nodePorts } from '../../lib/node_ports';

type Props = { nodeId: string };

export function NodeForm({ nodeId }: Props) {
  const graph   = useGraphStore(s => s.graph);
  const catalog = useGraphStore(s => s.catalog);
  const request = useGraphStore(s => s.requestUpdateNodeConfig);
  const setPos  = useGraphStore(s => s.updateNodePosition);
  const rename  = useGraphStore(s => s.renameNode);
  const node    = graph?.nodes.find(n => n.id === nodeId);
  const entry   = catalog.find(c => c.typeName === node?.type);
  const [showJson,  setShowJson]  = useState(false);
  const [showPorts, setShowPorts] = useState(false);
  // This component is keyed by nodeId (see Inspector), so the instance — and
  // this draft — belongs to exactly one node for its whole lifetime; nodeId
  // never changes under it. That isolation is what stops a half-typed id from
  // leaking onto the next selected node.
  const [idDraft,   setIdDraft]   = useState(nodeId);
  const [idErr,     setIdErr]     = useState<string | null>(null);
  const idDraftRef = useRef(idDraft);
  idDraftRef.current = idDraft;

  const commitId = () => {
    const next = idDraft.trim();
    if (!next || next === nodeId) { setIdDraft(nodeId); setIdErr(null); return; }
    if (graph?.nodes.some(n => n.id === next)) { setIdErr('id already in use'); return; }
    if (!rename(nodeId, next)) { setIdErr('rename failed'); setIdDraft(nodeId); return; }
    setIdErr(null);
  };

  // Commit a pending rename if the user selects away without blurring the field
  // (clicking a canvas node doesn't blur the input). Runs on unmount; reads the
  // latest store so it skips when the id already exists (e.g. blur committed it).
  useEffect(() => () => {
    const next = idDraftRef.current.trim();
    if (!next || next === nodeId) return;
    const st = useGraphStore.getState();
    if (st.graph && !st.graph.nodes.some(n => n.id === next)) st.renameNode(nodeId, next);
  }, [nodeId]);

  if (!node) {
    return (
      <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm">
        Node not found.
      </aside>
    );
  }

  const hasSchema = !!entry?.configSchema;

  // Use config-aware port resolution so the Ports listing reflects this
  // node's actual outputType/inputType, not the catalog probe's defaults.
  const livePorts = nodePorts({ type: node.type, config: node.config }, catalog);

  return (
    <aside className="w-96 bg-slate-800 border-l border-slate-700 p-3 text-sm flex flex-col overflow-y-auto">
      <div className="font-semibold mb-1">{node.id}</div>
      <div className="text-xs text-slate-400 mb-3">{node.type}</div>

      {/* Identity */}
      <div className="mb-3">
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Identity</div>
        <div className="grid grid-cols-3 gap-1 text-xs items-center mb-1">
          <span className="text-slate-400">id</span>
          <input
            className="col-span-2 bg-slate-900 px-2 py-1 rounded border border-slate-700 outline-none focus:border-sky-600"
            value={idDraft}
            onChange={e => { setIdDraft(e.target.value); setIdErr(null); }}
            onBlur={commitId}
            onKeyDown={e => {
              if (e.key === 'Enter') { e.preventDefault(); (e.target as HTMLInputElement).blur(); }
              if (e.key === 'Escape') { setIdDraft(node.id); setIdErr(null); (e.target as HTMLInputElement).blur(); }
            }}
            spellCheck={false}
          />
        </div>
        {idErr && <div className="text-amber-400 text-xs mb-1">{idErr}</div>}
        <div className="grid grid-cols-3 gap-1 text-xs items-center">
          <span className="text-slate-400">type</span>
          <input
            className="col-span-2 bg-slate-900 px-2 py-1 rounded border border-slate-700 opacity-60"
            value={node.type}
            readOnly
          />
        </div>
      </div>

      {/* Ports — collapsible list of input/output port names + type tags. */}
      <div className="mb-3">
        <button
          className="w-full flex items-center justify-between text-xs uppercase tracking-wide text-slate-400 hover:text-slate-200 mb-1"
          onClick={() => setShowPorts(v => !v)}
          type="button"
        >
          <span>Ports ({livePorts.inputs.length} in · {livePorts.outputs.length} out)</span>
          <span>{showPorts ? '▾' : '▸'}</span>
        </button>
        {showPorts && (
          <div className="bg-slate-900 border border-slate-700 rounded p-2 text-xs">
            {livePorts.inputs.length === 0 && livePorts.outputs.length === 0 && (
              <div className="text-slate-500">No ports.</div>
            )}
            {livePorts.inputs.length > 0 && (
              <>
                <div className="text-slate-500 mb-1">Inputs</div>
                {livePorts.inputs.map(p => (
                  <div key={`in-${p.name}`} className="grid grid-cols-[1fr_auto] gap-2 mb-0.5">
                    <span className="text-slate-200 truncate" title={p.name}>{p.name}</span>
                    <span className="text-sky-400 font-mono truncate" title={p.typeTag}>{p.typeTag}</span>
                  </div>
                ))}
              </>
            )}
            {livePorts.outputs.length > 0 && (
              <>
                <div className="text-slate-500 mt-2 mb-1">Outputs</div>
                {livePorts.outputs.map(p => (
                  <div key={`out-${p.name}`} className="grid grid-cols-[1fr_auto] gap-2 mb-0.5">
                    <span className="text-slate-200 truncate" title={p.name}>{p.name}</span>
                    <span className="text-emerald-400 font-mono truncate" title={p.typeTag}>{p.typeTag}</span>
                  </div>
                ))}
              </>
            )}
          </div>
        )}
      </div>

      {/* Config */}
      <div className="font-semibold mb-1 text-sm">Config</div>
      {showJson ? (
        <NodeFormJsonView nodeId={nodeId} />
      ) : node.type === 'Note' ? (
        <NoteForm nodeId={nodeId} />
      ) : !hasSchema ? (
        <NodeFormJsonView nodeId={nodeId} />
      ) : node.type === 'Transform.Extract' ? (
        <ExtractForm nodeId={nodeId} />
      ) : node.type === 'Sinks.Log' ? (
        <LogSinkForm nodeId={nodeId} />
      ) : node.type === 'Debug.ValueDisplay' ? (
        <ValueDisplayForm nodeId={nodeId} />
      ) : node.type === 'Transform.KeyValueAccumulator' ? (
        <KeyValueAccumulatorForm nodeId={nodeId} />
      ) : node.type === 'Transform.List.Constant' ? (
        <ListConstantForm nodeId={nodeId} />
      ) : node.type === 'Transform.List.Build' || node.type === 'Transform.List.Accumulate' ? (
        <ListElemForm nodeId={nodeId} />
      ) : node.type === 'Input.ButtonHandler' ? (
        <ButtonHandlerForm nodeId={nodeId} />
      ) : node.type === 'Transform.List.Find' || node.type === 'Transform.List.Filter' ? (
        <ListPredicateForm nodeId={nodeId} />
      ) : node.type === 'Transform.Convert' ? (
        <ConvertForm nodeId={nodeId} />
      ) : node.type === 'Transform.Compare' ? (
        <CompareForm nodeId={nodeId} />
      ) : node.type === 'Transform.Filter' ? (
        <FilterForm nodeId={nodeId} />
      ) : node.type === 'Transform.Threshold' ? (
        <ThresholdForm nodeId={nodeId} />
      ) : node.type === 'Transform.Throttle' ? (
        <ThrottleForm nodeId={nodeId} />
      ) : node.type === 'Transform.Synchronize' ? (
        <SyncForm nodeId={nodeId} />
      ) : node.type === 'Transform.ConstantSource' ? (
        <ConstantSourceForm nodeId={nodeId} />
      ) : (
        <Form
          // Re-key when a type field changes so RJSF remounts with the new
          // (or reverted, on cancel) formData rather than keeping stale
          // internal state from the user's last dropdown pick.
          key={
            String((node.config as Record<string, unknown> | undefined)?.inputType ?? '') + '|' +
            String((node.config as Record<string, unknown> | undefined)?.outputType ?? '')
          }
          schema={entry!.configSchema as RJSFSchema}
          formData={node.config ?? entry!.configDefaults ?? {}}
          validator={validator}
          widgets={tailwindWidgets}
          templates={tailwindTemplates}
          // Route through request* so a change to inputType/outputType pops
          // the confirmation modal; everything else applies immediately.
          onChange={e => request(nodeId, e.formData)}
          uiSchema={{ 'ui:submitButtonOptions': { norender: true } }}
        />
      )}

      {/* Position */}
      <div className="mt-3">
        <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">Position (canvas)</div>
        <div className="flex items-center gap-2">
          <input
            className="w-20 bg-slate-900 text-slate-100 text-sm px-2 py-1 rounded border border-slate-700"
            type="number"
            value={node.position?.x ?? 0}
            onChange={e => setPos(nodeId, { x: Number(e.target.value), y: node.position?.y ?? 0 })}
          />
          <input
            className="w-20 bg-slate-900 text-slate-100 text-sm px-2 py-1 rounded border border-slate-700"
            type="number"
            value={node.position?.y ?? 0}
            onChange={e => setPos(nodeId, { x: node.position?.x ?? 0, y: Number(e.target.value) })}
          />
        </div>
      </div>

      <NodeFormFooter
        nodeId={nodeId}
        showJson={showJson}
        onToggleJson={() => setShowJson(s => !s)}
      />
    </aside>
  );
}
