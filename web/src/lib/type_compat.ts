// SPDX-License-Identifier: MIT
import { ALL_FIELD_DESCRIPTORS } from '../generated/field_descriptors.generated';

export function isPrimitiveTag(tag: string): boolean {
  return tag.startsWith('flowboard::') && tag !== 'flowboard::List';
}
// Numeric for plotting purposes — Bool plots as 0/1; String/Char are not numeric.
export function isNumericTag(tag: string): boolean {
  return isPrimitiveTag(tag) && tag !== 'flowboard::String' && tag !== 'flowboard::Char';
}
export function isStructTag(tag: string): boolean {
  return tag in ALL_FIELD_DESCRIPTORS;
}

export type TypeChange = { patch: Record<string, unknown>; field: string; from: string; to: string };

// If connecting `srcTag` into (nodeType, targetHandle) mismatches but the
// target input's type is config-settable to srcTag, return the config patch
// to make it match (+ metadata for the prompt). Returns null when the type is
// not config-settable OR already equals srcTag (no prompt needed).
export function typeChangeForConnect(
  nodeType: string,
  config: Record<string, unknown>,
  targetHandle: string,
  srcTag: string,
): TypeChange | null {
  const cur = (f: string) => String(config[f] ?? '');
  const make = (field: string, patch: Record<string, unknown>): TypeChange | null =>
    cur(field) === srcTag ? null : { patch: { ...config, ...patch }, field, from: cur(field), to: srcTag };

  if (nodeType === 'Transform.Extract' && targetHandle === 'in')
    return make('inputType', { inputType: srcTag, field: '' });
  if ((nodeType === 'Sinks.Log' || nodeType === 'Debug.ValueDisplay') && targetHandle === 'in')
    return make('inputType', { inputType: srcTag });
  if (nodeType === 'Debug.GraphDisplay' && targetHandle === 'in' && isNumericTag(srcTag))
    return make('inputType', { inputType: srcTag });
  if (nodeType === 'Transform.KeyValueAccumulator' && targetHandle === 'value')
    return make('valueType', { valueType: srcTag });
  if (nodeType === 'Transform.KeyValueAccumulator' && targetHandle === 'key')
    return make('keyType', { keyType: srcTag });
  return null;
}

export type Candidate = { typeName: string; portName: string };

// Extra create-menu candidates for config-typed / wildcard nodes that the
// catalog (probed at default config) wouldn't surface for `tag`.
export function extraCandidates(tag: string, wantInput: boolean): Candidate[] {
  const out: Candidate[] = [];
  if (wantInput) {
    out.push({ typeName: 'Sinks.Log', portName: 'in' });
    out.push({ typeName: 'Debug.ValueDisplay', portName: 'in' });
    if (isNumericTag(tag)) out.push({ typeName: 'Debug.GraphDisplay', portName: 'in' });
    out.push({ typeName: 'Transform.KeyValueAccumulator', portName: 'value' });
    if (isStructTag(tag)) out.push({ typeName: 'Transform.Extract', portName: 'in' });
    // Convert bridges any primitive to a configurable primitive; offer it so a
    // mismatched primitive output can be retyped. Its `in` adopts the dragged tag.
    if (isPrimitiveTag(tag)) out.push({ typeName: 'Transform.Convert', portName: 'in' });
  } else {
    if (isPrimitiveTag(tag)) out.push({ typeName: 'Transform.ConstantSource', portName: 'out' });
    // Convert can produce any primitive; offer it as a source whose `out` adopts
    // the dragged tag (its `in` keeps the default for the user to set).
    if (isPrimitiveTag(tag)) out.push({ typeName: 'Transform.Convert', portName: 'out' });
  }
  return out;
}

// Config override so a freshly-created candidate node's connecting port adopts
// `tag` instead of the catalog default.
// A type-appropriate default value for a ConstantSource of the given tag, so
// the created node's `value` matches its outputType (avoids an engine
// type-error when, e.g., outputType=Bool but value is the string default).
function constantDefaultValue(tag: string): unknown {
  if (tag === 'flowboard::Bool') return false;
  if (tag === 'flowboard::String' || tag === 'flowboard::Char') return '';
  return 0;  // every numeric tag
}

export function candidateConfig(typeName: string, portName: string, tag: string): Record<string, unknown> | undefined {
  if (typeName === 'Transform.Extract')          return { inputType: tag, field: '' };
  if (typeName === 'Sinks.Log')                  return { inputType: tag };
  if (typeName === 'Debug.ValueDisplay')         return { inputType: tag };
  if (typeName === 'Debug.GraphDisplay')         return { inputType: tag };
  if (typeName === 'Transform.ConstantSource')   return { outputType: tag, value: constantDefaultValue(tag) };
  if (typeName === 'Transform.Convert')          return portName === 'out' ? { outputType: tag } : { inputType: tag };
  if (typeName === 'Transform.KeyValueAccumulator')
    return portName === 'key' ? { keyType: tag } : { valueType: tag };
  return undefined;
}
