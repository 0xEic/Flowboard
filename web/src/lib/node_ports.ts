// SPDX-License-Identifier: MIT
import { computeExtractPorts } from './extract_ports';
import { machineOuterPorts, type MachineConfig } from './machine_compile';

export interface Port { name: string; typeTag: string }
export interface GraphNodeLike { type: string; config?: Record<string, unknown> }
export interface CatalogLike   { typeName: string; inputs: Port[]; outputs: Port[] }

// The catalog the engine returns is keyed by type name and built once from a
// single "probe" instance per type using the schema defaults. For dispatching
// nodes whose port types depend on config (Transform.Extract,
// Transform.ConstantSource, Sinks.Log, …) the catalog only reflects the
// default — so we recompute the ports from this node's actual config here.
export function nodePorts(node: GraphNodeLike, catalog: CatalogLike[]): { inputs: Port[]; outputs: Port[] } {
  const cfg = (node.config ?? {}) as Record<string, unknown>;

  if (node.type === 'Transform.Extract') {
    return computeExtractPorts(cfg as { inputType?: string; field?: string });
  }
  if (node.type === 'Transform.ConstantSource') {
    const t = (cfg.outputType as string) || 'flowboard::String';
    return {
      inputs:  [{ name: 'trigger', typeTag: 'flowboard::Bool' }],
      outputs: [{ name: 'out',     typeTag: t }],
    };
  }
  if (node.type === 'Sources.Iterator') {
    const t = (cfg.valueType as string) || 'flowboard::Double';
    return {
      inputs: [
        { name: 'startValue', typeTag: t },
        { name: 'endValue',   typeTag: t },
        { name: 'stepSize',   typeTag: t },
        { name: 'timeBetweenStepsMs', typeTag: 'flowboard::Int64' },
        { name: 'loop',        typeTag: 'flowboard::Bool' },
        { name: 'autoReverse', typeTag: 'flowboard::Bool' },
        { name: 'start', typeTag: 'flowboard::Bool' },
        { name: 'stop',  typeTag: 'flowboard::Bool' },
        { name: 'pause', typeTag: 'flowboard::Bool' },
        { name: 'reset', typeTag: 'flowboard::Bool' },
      ],
      outputs: [
        { name: 'value',      typeTag: t },
        { name: 'running',    typeTag: 'flowboard::Bool' },
        { name: 'endReached', typeTag: 'flowboard::Bool' },
        { name: 'started',    typeTag: 'flowboard::Bool' },
        { name: 'increasing', typeTag: 'flowboard::Bool' },
        { name: 'decreasing', typeTag: 'flowboard::Bool' },
      ],
    };
  }
  if (node.type === 'Sources.Random') {
    const t = (cfg.outputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'trigger', typeTag: 'flowboard::Bool' }],
      outputs: [{ name: 'out',     typeTag: t }],
    };
  }
  if (node.type === 'Transform.Convert') {
    return {
      inputs:  [{ name: 'in',  typeTag: (cfg.inputType  as string) || 'flowboard::Double' }],
      outputs: [{ name: 'out', typeTag: (cfg.outputType as string) || 'flowboard::String' }],
    };
  }
  if (node.type === 'Transform.Arithmetic') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'a', typeTag: t }, { name: 'b', typeTag: t }],
      outputs: [{ name: 'out', typeTag: t }],
    };
  }
  if (node.type === 'Manipulate.Number') {
    const t = (cfg.valueType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in',  typeTag: t }],
      outputs: [{ name: 'out', typeTag: t }],
    };
  }
  if (node.type === 'Transform.NumberHolder') {
    const t = (cfg.valueType as string) || 'flowboard::Double';
    return {
      inputs: [
        { name: 'initValue', typeTag: t },
        { name: 'increase',  typeTag: t },
        { name: 'decrease',  typeTag: t },
      ],
      outputs: [{ name: 'value', typeTag: t }],
    };
  }
  if (node.type === 'Transform.Compare') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'a', typeTag: t }, { name: 'b', typeTag: t }],
      outputs: [{ name: 'out', typeTag: 'flowboard::Bool' }],
    };
  }
  if (node.type === 'Transform.Filter') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in', typeTag: t }, { name: 'gate', typeTag: 'flowboard::Bool' }],
      outputs: [{ name: 'out', typeTag: t }],
    };
  }
  if (node.type === 'Transform.Threshold') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in', typeTag: t }],
      outputs: [{ name: 'out', typeTag: 'flowboard::Bool' }],
    };
  }
  if (node.type === 'Transform.Throttle') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in', typeTag: t }],
      outputs: [{ name: 'out', typeTag: t }],
    };
  }
  if (node.type === 'Transform.Delay') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in', typeTag: t }],
      outputs: [{ name: 'out', typeTag: t }],
    };
  }
  if (node.type === 'Transform.Derivative') {
    // Accepts any numeric primitive; the rate output is always Double.
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return {
      inputs:  [{ name: 'in',  typeTag: t }],
      outputs: [{ name: 'out', typeTag: 'flowboard::Double' }],
    };
  }
  if (node.type === 'Transform.Synchronize') {
    // Per-pair types: inputTypes[i] when present, else the scalar inputType
    // (back-compat: old graphs only have inputType + inputCount).
    const fallback = (cfg.inputType as string) || 'flowboard::Double';
    const types = Array.isArray(cfg.inputTypes) ? (cfg.inputTypes as unknown[]) : [];
    const n = types.length > 0
      ? Math.min(64, types.length)
      : Math.max(1, Math.min(64, Number(cfg.inputCount ?? 2)));
    const typeAt = (i: number) => {
      const t = types[i];
      return typeof t === 'string' && t ? t : fallback;
    };
    const inputs: Port[] = [{ name: 'forceOutput', typeTag: 'flowboard::Bool' }];
    const outputs: Port[] = [{ name: 'beforeOutput', typeTag: 'flowboard::Bool' }];
    for (let i = 0; i < n; i++) inputs.push({ name: `in${i}`, typeTag: typeAt(i) });
    for (let i = 0; i < n; i++) outputs.push({ name: `out${i}`, typeTag: typeAt(i) });
    outputs.push({ name: 'afterOutput', typeTag: 'flowboard::Bool' });
    return { inputs, outputs };
  }
  if (node.type === 'Transform.List.Build') {
    const t = (cfg.elementType as string) || 'flowboard::Double';
    const n = Math.max(0, Math.min(64, Number(cfg.count ?? 2)));
    const inputs: Port[] = [];
    for (let i = 0; i < n; i++) inputs.push({ name: `item${i}`, typeTag: t });
    inputs.push({ name: 'trigger', typeTag: 'flowboard::Bool' });
    return { inputs, outputs: [{ name: 'out', typeTag: 'flowboard::List' }] };
  }
  if (node.type === 'Transform.List.Accumulate') {
    const t = (cfg.elementType as string) || 'flowboard::Double';
    return {
      inputs: [
        { name: 'item',    typeTag: t },
        { name: 'trigger', typeTag: 'flowboard::Bool' },
        { name: 'reset',   typeTag: 'flowboard::Bool' },
      ],
      outputs: [{ name: 'out', typeTag: 'flowboard::List' }],
    };
  }
  if (node.type === 'Transform.List.Constant') {
    return {
      inputs:  [{ name: 'trigger', typeTag: 'flowboard::Bool' }],
      outputs: [{ name: 'out', typeTag: 'flowboard::List' }],
    };
  }
  if (node.type === 'Sinks.Log' ||
      node.type === 'Debug.ValueDisplay' ||
      node.type === 'Debug.GraphDisplay') {
    const t = (cfg.inputType as string) || 'flowboard::Double';
    return { inputs: [{ name: 'in', typeTag: t }], outputs: [] };
  }
  if (node.type === 'Transform.KeyValueAccumulator') {
    const k = (cfg.keyType as string)   || 'flowboard::String';
    const v = (cfg.valueType as string) || 'flowboard::String';
    return {
      inputs: [
        { name: 'key',       typeTag: k },
        { name: 'value',     typeTag: v },
        { name: 'isRemoved', typeTag: 'flowboard::Bool' },
      ],
      outputs: [{ name: 'list', typeTag: 'flowboard::List' }],
    };
  }
  if (node.type === 'Input.ButtonHandler') {
    // A single atomic struct input `args` (the whole EventButton in one message)
    // plus one bool output per configured button. Mirrors the engine's port
    // derivation + name defaults. `argsType` overrides the struct type tag;
    // it defaults to M_HidJoystick::EventButton_Args.
    const raw = Array.isArray(cfg.outputs) ? (cfg.outputs as Array<Record<string, unknown>>) : [];
    const outputs: Port[] = [];
    const seen = new Set<string>();
    for (const o of raw) {
      const byIndex = (o.match ?? 'byIndex') !== 'byName';
      let name = String(o.output ?? '').trim();
      if (!name) name = byIndex ? `button${Number(o.index ?? 0)}` : String(o.buttonName ?? '').trim();
      if (!name || seen.has(name)) continue;
      seen.add(name);
      outputs.push({ name, typeTag: 'flowboard::Bool' });
    }
    const argsType = (typeof cfg.argsType === 'string' && cfg.argsType.trim())
      ? cfg.argsType.trim() : 'M_HidJoystick::EventButton_Args';
    const inputs: Port[] = [{ name: 'args', typeTag: argsType }];
    return { inputs, outputs };
  }
  if (node.type === 'Flow.StateMachine') {
    // Trigger inputs + state/active/transition/changed outputs are derived from
    // the compiled states + transitions, mirroring the engine's port derivation.
    return machineOuterPorts(cfg as Partial<MachineConfig>);
  }
  if (node.type === 'OnboardApi.DeviceReport') {
    return { inputs: [], outputs: [] };  // port-less: a presence node
  }
  if (node.type === 'Flow.Group') {
    // A group's external ports are its inner boundary terminals: each
    // Group.Input is an input handle, each Group.Output an output handle.
    const inner = (cfg.group as { nodes?: GraphNodeLike[] } | undefined)?.nodes ?? [];
    const term = (t: GraphNodeLike) => {
      const c = (t.config ?? {}) as { portName?: string; typeTag?: string };
      return { name: c.portName ?? '', typeTag: c.typeTag ?? 'flowboard::Double' };
    };
    return {
      inputs:  inner.filter(t => t.type === 'Group.Input').map(term).filter(p => p.name),
      outputs: inner.filter(t => t.type === 'Group.Output').map(term).filter(p => p.name),
    };
  }
  if (node.type === 'Group.Input') {
    // A boundary input terminal feeds the group's inner nodes via its `out`.
    return { inputs: [], outputs: [{ name: 'out', typeTag: (cfg.typeTag as string) || 'flowboard::Double' }] };
  }
  if (node.type === 'Group.Output') {
    // A boundary output terminal consumes from inner nodes via its `in`.
    return { inputs: [{ name: 'in', typeTag: (cfg.typeTag as string) || 'flowboard::Double' }], outputs: [] };
  }

  if (node.type === 'Python.Script') {
    // The node declares its ports in config as [{ name, type }, …]; map each to
    // a canvas port. Mirrors how the C++ node builds its real ports, so the
    // links shown here match what the engine creates on Apply & Reload.
    const toPorts = (raw: unknown): Port[] =>
      (Array.isArray(raw) ? (raw as Array<Record<string, unknown>>) : [])
        .map(p => ({
          name: String(p.name ?? '').trim(),
          typeTag: String(p.type ?? 'flowboard::Double'),
        }))
        .filter(p => p.name);
    return { inputs: toPorts(cfg.inputs), outputs: toPorts(cfg.outputs) };
  }

  if (node.type === 'Bytes.Pack' || node.type === 'Bytes.Unpack') {
    const raw = Array.isArray(cfg.fields) ? (cfg.fields as Array<Record<string, unknown>>) : [];
    const fields = raw
      .map(f => ({ name: String(f.name ?? '').trim(), typeTag: String(f.type ?? 'flowboard::UInt8') }))
      .filter(f => f.name);
    if (node.type === 'Bytes.Pack') {
      return {
        inputs:  [...fields, { name: 'trigger', typeTag: 'flowboard::Bool' }],
        outputs: [{ name: 'out', typeTag: 'flowboard::List' }],
      };
    }
    return {
      inputs:  [{ name: 'in', typeTag: 'flowboard::List' }],
      outputs: fields,
    };
  }

  if (node.type === 'Can.Encode') {
    return {
      inputs: [
        { name: 'id',         typeTag: 'flowboard::UInt32' },
        { name: 'data',       typeTag: 'flowboard::List' },
        { name: 'isExtended', typeTag: 'flowboard::Bool' },
        { name: 'trigger',    typeTag: 'flowboard::Bool' },
      ],
      outputs: [{ name: 'out', typeTag: 'flowboard::CanFrame' }],
    };
  }
  if (node.type === 'Can.Decode') {
    return {
      inputs:  [{ name: 'frame', typeTag: 'flowboard::CanFrame' }],
      outputs: [
        { name: 'id',         typeTag: 'flowboard::UInt32' },
        { name: 'data',       typeTag: 'flowboard::List' },
        { name: 'isExtended', typeTag: 'flowboard::Bool' },
        { name: 'dlc',        typeTag: 'flowboard::UInt8' },
      ],
    };
  }

  const entry = catalog.find(c => c.typeName === node.type);
  return { inputs: entry?.inputs ?? [], outputs: entry?.outputs ?? [] };
}
