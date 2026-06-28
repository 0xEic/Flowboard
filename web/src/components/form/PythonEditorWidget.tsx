// SPDX-License-Identifier: MIT
//
// RJSF widget that renders a string-typed schema property as the shared
// PythonCodeEditor. Activated by `"ui:widget": "pythonEditor"` in a form's
// uiSchema. Reads the configured ports from formContext for autocomplete.
import type { WidgetProps } from '@rjsf/utils';
import { PythonCodeEditor, type PortSpec } from './PythonCodeEditor';

type Ctx = { inputs?: PortSpec[]; outputs?: PortSpec[] };

export function PythonEditorWidget(props: WidgetProps) {
  const value = typeof props.value === 'string' ? props.value : '';
  const ctx = (props.formContext ?? {}) as Ctx;
  return (
    <PythonCodeEditor
      value={value}
      onChange={v => props.onChange(v)}
      inputs={Array.isArray(ctx.inputs) ? ctx.inputs : []}
      outputs={Array.isArray(ctx.outputs) ? ctx.outputs : []}
      height="280px"
    />
  );
}
