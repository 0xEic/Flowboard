// SPDX-License-Identifier: MIT
//
// Reusable CodeMirror 6 Python editor. Autocomplete is seeded with the host
// helpers (emit/inputs/on_input) and the node's configured input/output port
// names, so suggestions track the ports as they're added/removed. Used both
// inline in PythonScriptForm and in its larger expand modal.
import { useMemo } from 'react';
import CodeMirror from '@uiw/react-codemirror';
import { python } from '@codemirror/lang-python';
import { autocompletion, type CompletionContext, type Completion } from '@codemirror/autocomplete';
import { oneDark } from '@codemirror/theme-one-dark';

export type PortSpec = { name?: string; type?: string };

export function PythonCodeEditor(props: {
  value: string;
  onChange: (v: string) => void;
  inputs: PortSpec[];
  outputs: PortSpec[];
  height: string;
}) {
  const { inputs, outputs } = props;

  // Rebuild the CodeMirror extensions only when the port names/types actually
  // change — not on every render. react-codemirror treats `extensions` as a
  // plain dependency, so a fresh array each render would dispatch a full
  // reconfigure on every keystroke (the script editor re-renders its parent on
  // each change). Keying on a value signature of the ports avoids that.
  const portsKey = JSON.stringify({
    i: inputs.map(p => [p?.name, p?.type]),
    o: outputs.map(p => [p?.name, p?.type]),
  });
  const extensions = useMemo(() => {
    const ins  = inputs.filter(p => p?.name);
    const outs = outputs.filter(p => p?.name);
    const completions = (cc: CompletionContext) => {
      const before = cc.matchBefore(/[A-Za-z_'][\w']*/);
      if (!before || (before.from === before.to && !cc.explicit)) return null;
      const options: Completion[] = [
        { label: 'emit',     type: 'function', detail: 'emit(port, value)',
          info: 'Send a value to one of this node\'s output ports.' },
        { label: 'inputs',   type: 'variable', detail: 'dict[str, Any]',
          info: 'Most-recent value per input port name.' },
        { label: 'on_input', type: 'function', detail: 'def on_input(port, value)',
          info: 'Define this — the host calls it whenever an input arrives.' },
        ...ins.map(p => ({
          label: `'${p.name}'`, type: 'text',
          detail: `input · ${p.type ?? ''}`,
        } as Completion)),
        ...outs.map(p => ({
          label: `'${p.name}'`, type: 'text',
          detail: `output · ${p.type ?? ''}`,
        } as Completion)),
      ];
      return { from: before.from, options };
    };
    return [python(), autocompletion({ override: [completions] })];
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [portsKey]);

  return (
    <div className="border border-slate-700 rounded overflow-hidden text-sm">
      <CodeMirror
        value={props.value}
        height={props.height}
        theme={oneDark}
        extensions={extensions}
        basicSetup={{
          lineNumbers: true,
          foldGutter: true,
          highlightActiveLine: true,
          autocompletion: true,
          bracketMatching: true,
          closeBrackets: true,
          indentOnInput: true,
        }}
        onChange={props.onChange}
      />
    </div>
  );
}
