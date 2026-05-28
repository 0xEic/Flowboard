// SPDX-License-Identifier: MIT
import type { WidgetProps, FieldTemplateProps, ObjectFieldTemplateProps } from '@rjsf/utils';

const inputBase =
  'w-full bg-slate-900 text-slate-100 text-sm px-2 py-1 rounded border border-slate-700 ' +
  'outline-none focus:border-sky-600 disabled:opacity-50';

const TextWidget = (p: WidgetProps) => (
  <input
    className={inputBase}
    type="text"
    value={(p.value ?? '') as string}
    onChange={e => p.onChange(e.target.value)}
    readOnly={p.readonly}
    placeholder={p.placeholder}
  />
);

const NumberWidget = (p: WidgetProps) => (
  <input
    className={inputBase}
    type="number"
    value={p.value ?? ''}
    onChange={e => p.onChange(e.target.value === '' ? undefined : Number(e.target.value))}
    step="any"
  />
);

const CheckboxWidget = (p: WidgetProps) => (
  <label className="inline-flex items-center gap-2 text-sm">
    <input type="checkbox" checked={!!p.value} onChange={e => p.onChange(e.target.checked)} />
    <span>{p.label}</span>
  </label>
);

const SelectWidget = (p: WidgetProps) => {
  const opts: { value: unknown; label: string }[] = (p.options.enumOptions as { value: unknown; label: string }[]) ?? [];
  return (
    <select
      className={inputBase}
      value={(p.value ?? '') as string}
      onChange={e => p.onChange(e.target.value)}
    >
      {opts.map(o => <option key={String(o.value)} value={o.value as string | number}>{o.label}</option>)}
    </select>
  );
};

const TextareaWidget = (p: WidgetProps) => (
  <textarea
    className={inputBase + ' font-mono'}
    rows={4}
    value={(p.value ?? '') as string}
    onChange={e => p.onChange(e.target.value)}
  />
);

const FieldTemplate = (p: FieldTemplateProps) => (
  <div className="mb-3">
    {p.label && p.displayLabel !== false && (
      <div className="text-xs uppercase tracking-wide text-slate-400 mb-1">
        {p.label}{p.required ? ' *' : ''}
      </div>
    )}
    {p.children}
    {p.rawErrors && p.rawErrors.length > 0 && (
      <div className="text-amber-400 text-xs mt-1">{p.rawErrors.join(', ')}</div>
    )}
    {p.help && <div className="text-xs text-slate-500 mt-1">{p.help}</div>}
  </div>
);

const ObjectFieldTemplate = (p: ObjectFieldTemplateProps) => (
  <div>
    {p.properties.map(prop => <div key={prop.name}>{prop.content}</div>)}
  </div>
);

export const tailwindWidgets = {
  TextWidget, NumberWidget, CheckboxWidget, SelectWidget, TextareaWidget,
};

export const tailwindTemplates = {
  FieldTemplate, ObjectFieldTemplate,
};
