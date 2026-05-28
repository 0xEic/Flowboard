// SPDX-License-Identifier: MIT
import { ALL_FIELD_DESCRIPTORS, type FieldDescriptor } from '../generated/field_descriptors.generated';

interface Port { name: string; typeTag: string }

export function listInputStructTypes(): string[] {
  return Object.keys(ALL_FIELD_DESCRIPTORS).sort();
}

export function fieldsForInputType(inputType: string | undefined): FieldDescriptor[] {
  if (!inputType) return [];
  return ALL_FIELD_DESCRIPTORS[inputType] ?? [];
}

export function computeExtractPorts(
  config: { inputType?: string; field?: string },
): { inputs: Port[]; outputs: Port[] } {
  const { inputType, field } = config;
  if (!inputType) return { inputs: [], outputs: [] };
  const inputs: Port[] = [{ name: 'in', typeTag: inputType }];
  if (!field) return { inputs, outputs: [] };

  const desc = ALL_FIELD_DESCRIPTORS[inputType]?.find(f => f.name === field);
  if (!desc) return { inputs, outputs: [] };

  switch (desc.kind) {
    case 'Optional':
      return { inputs, outputs: [
        { name: 'value',    typeTag: desc.elementTypeTag },
        { name: 'isFilled', typeTag: 'flowboard::Bool' },
      ]};
    case 'List':
      // A List-kind field is emitted as a single first-class flowboard::List
      // value (element type recorded as a hint only). Use Transform.List.* to
      // sort/filter/find/index it downstream.
      return { inputs, outputs: [
        { name: 'list', typeTag: 'flowboard::List' },
      ]};
    case 'Scalar':
    default:
      return { inputs, outputs: [
        { name: 'out', typeTag: desc.elementTypeTag },
      ]};
  }
}
