# Node Reference

> **Generated** from the engine's node registry by `tools/gen_node_docs.py`
> (fed by `flowboard --dump-nodes`). Do not edit by hand — regenerate with:
>
> ```bash
> flowboard --dump-nodes | python tools/gen_node_docs.py > docs/NODES.md
> ```

Ports are listed as `name`:Type for the node's **default** configuration; some nodes add, rename or retype ports based on their config (e.g. `Transform.Extract`, `Transform.Convert`, `Transform.Synchronize`). Categories mirror the editor palette.

## Debug

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Debug.ValueDisplay` | `in`:Double | — | `fields`, `inputType` |
| `Debug.GraphDisplay` | `in`:Double | — | `autoScale`, `inputType`, `timeWindowSec`, `yMax`, `yMin` |
| `Debug.TriggerButton` | — | `out`:Bool | `label`, `mode` (pushbutton/switch) |

## Sources

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Sources.CurrentTime` | `trigger`:Bool | `commonTime`:M_Common::TimeType, `epoch`:Int64, `iso`:String | `autoTrigger`, `autoTriggerMs`, `unit` (epoch_ms/epoch_s/iso8601) |
| `Sources.TimeSource` | `running`:Bool | `commonTime`:M_Common::TimeType, `epoch`:Int64, `iso`:String | `day`, `hour`, `millisecond`, `minute`, `month`, `second`, `updateMs`, `year` |
| `Sources.Timer` | `reset`:Bool, `start`:Bool | `isRunning`:Bool, `tick`:Bool, `timeLeft`:Int64, `timePast`:Int64 | `autostart`, `intervalMs`, `repeat` |
| `Sources.Iterator` | `autoReverse`:Bool, `endValue`:Double, `loop`:Bool, `pause`:Bool, `reset`:Bool, `start`:Bool, `startValue`:Double, `stepSize`:Double, `stop`:Bool, `timeBetweenStepsMs`:Int64 | `decreasing`:Bool, `endReached`:Bool, `increasing`:Bool, `running`:Bool, `started`:Bool, `value`:Double | `autoReverse`, `autostart`, `endValue`, `loop`, `specialEndEndValue`, `specialEndStartValue`, `specialStartEndValue`, `specialStartStartValue`, `startValue`, `stepSize`, `timeBetweenStepsMs`, `valueType` |
| `Transform.ConstantSource` | `trigger`:Bool | `out`:String | `autoTriggerOnInit`, `outputType`, `value` |
| `Sources.Random` | `trigger`:Bool | `out`:Double | `allowDigits`, `allowLowercase`, `allowSymbols`, `allowUppercase`, `autoTriggerOnInit`, `length`, `max`, `min`, `outputType`, `trueProbability` |

## Transform

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Transform.Convert` | `in`:Double | `out`:String | `clamp`, `clampMax`, `clampMin`, `compareOp` (gt/gte/lt/lte/eq/neq), `decimals`, `fallback`, `falseValue`, `inputType`, `mappings`, `mode`, `offset`, `outputType`, `scale`, `template`, `textTransform` (none/upper/lower/trim), `threshold`, `trueValue` |
| `Transform.Extract` | `in`:M_Mount::MountPositionType | `isFilled`:Bool, `value`:Double | `field`, `inputType` |
| `Transform.Inverter` | `in`:Bool | `out`:Bool | — |
| `Transform.Pulse` | `in`:Bool | `out`:Bool | — |
| `Transform.Derivative` | `in`:Double | `out`:Double | `inputType` |
| `Transform.Arithmetic` | `a`:Double, `b`:Double | `out`:Double | `inputType`, `op` (add/subtract/multiply/divide/min/max) |
| `Transform.NumberHolder` | `decrease`:Double, `increase`:Double, `initValue`:Double | `value`:Double | `emitOnInit`, `initValue`, `valueType` |
| `Manipulate.Number` | `in`:Double | `out`:Double | `formula`, `valueType` |
| `Manipulate.String` | `in`:String | `out`:String | `find`, `mode` (replace/set/prepend/append), `replace`, `text` |

## Logic & Conditions

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Transform.Compare` | `a`:Double, `b`:Double | `out`:Bool | `inputType`, `op` |
| `Transform.Threshold` | `in`:Double | `out`:Bool | `inputType`, `op` (>/>=/</<=/==/!=), `value` |
| `Transform.Filter` | `gate`:Bool, `in`:Double | `out`:Double | `closedBehavior` (hold/default), `defaultValue`, `inputType` |

## Lists

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Transform.List.Build` | `item0`:Double, `item1`:Double, `trigger`:Bool | `out`:List | `count`, `elementType` |
| `Transform.List.Constant` | `trigger`:Bool | `out`:List | `autoTriggerOnInit`, `elementType`, `values` |
| `Transform.List.Accumulate` | `item`:Double, `reset`:Bool, `trigger`:Bool | `out`:List | `elementType`, `maxItems` |
| `Transform.List.Combine` | `a`:List, `b`:List | `out`:List | — |
| `Transform.List.Filter` | `in`:List | `out`:List | `field`, `op` (eq/neq/lt/lte/gt/gte/contains), `value` |
| `Transform.List.Find` | `in`:List | `isFound`:Bool, `value`:List | `field`, `op` (eq/neq/lt/lte/gt/gte/contains), `value` |
| `Transform.List.GetAt` | `in`:List | `isPresent`:Bool, `value`:List | `index` |
| `Transform.List.MapField` | `in`:List | `out`:List | `field`, `find`, `formula`, `mode` (replace/set/prepend/append), `replace`, `text`, `valueType` (number/string) |
| `Transform.List.Size` | `in`:List | `count`:Int64, `isEmpty`:Bool | — |
| `Transform.List.Sort` | `in`:List | `out`:List | `ascending`, `field` |

## Timing & Sync

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Transform.Delay` | `in`:Double | `out`:Double | `delayMs`, `inputType` |
| `Transform.Throttle` | `in`:Double | `out`:Double | `inputType`, `minPeriodMs` |
| `Transform.Synchronize` | `forceOutput`:Bool, `in0`:Double, `in1`:Double | `afterOutput`:Bool, `beforeOutput`:Bool, `out0`:Double, `out1`:Double | `defaults`, `inputCount`, `inputType`, `inputTypes`, `order` |
| `Transform.KeyValueAccumulator` | `isRemoved`:Bool, `key`:String, `value`:String | `list`:List | `elementTypeTag`, `emitOnEmpty`, `keyType`, `orderBy` (insertion/key), `valueType` |

## Sinks

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Sinks.Log` | `in`:Double | — | `inputType`, `path`, `prefix` |

## Input

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Input.ButtonHandler` | `args`:M_HidJoystick::EventButton_Args | — | `argsType`, `outputs` |

## Flow & Structure

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Flow.StateMachine` | — | — | `initial`, `notes`, `states`, `transitions` |
| `Flow.Group` | — | — | Wraps a sub-graph into a reusable group node (web editor). |
| `Note` | — | — | Free-text annotation pinned to the canvas; ignored by the engine. |
| `Group.Input` | — | — | Boundary input terminal inside a group (web editor). |
| `Group.Output` | — | — | Boundary output terminal inside a group (web editor). |

## OnboardAPI

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `OnboardApi.Discovery` | `trigger`:Bool | `out`:List | `allDomains`, `domainId`, `source` (graph/live/both) |
| `OnboardApi.DeviceReport` | — | — | `description`, `deviceName`, `domainId`, `heartbeatSec`, `serviceName` |

## Other

| Node | Inputs | Outputs | Config |
|---|---|---|---|
| `Bytes.Pack` | `trigger`:Bool | `out`:List | `autoTriggerOnNewInput`, `defaults`, `endian` (little/big), `fields`, `length` |
| `Bytes.Unpack` | `in`:List | — | `endian` (little/big), `fields` |
| `Can.Bus` | `tx`:CanFrame | `connected`:Bool, `rx`:CanFrame, `sent`:Bool | `adapter`, `autoReconnect`, `bitrate`, `listenOnly`, `reconnectIntervalMs` |
| `Can.Decode` | `frame`:CanFrame | `data`:List, `dlc`:UInt8, `id`:UInt32, `isExtended`:Bool | — |
| `Can.Encode` | `data`:List, `id`:UInt32, `isExtended`:Bool, `trigger`:Bool | `out`:CanFrame | `autoTriggerOnNewInput`, `defaults` |
| `Can.Filter` | `in`:CanFrame | `out`:CanFrame | `idMask`, `idMatch`, `idMax`, `idMin`, `ids`, `mode` |
| `Framer.Delimiter` | `in`:List | `out`:List | `delimiter`, `delimiterIsHex`, `includeDelimiter`, `maxFrameSize` |
| `Framer.Fixed` | `in`:List | `out`:List | `frameSize` |
| `Framer.LengthPrefix` | `in`:List | `out`:List | `lengthIncludesPrefix`, `littleEndian`, `maxFrameSize`, `prefixSize` (1/2/4) |
| `Plugin.Scale` | `in`:Double | `out`:Double | `factor` |
| `Plugin.Sum` | `a`:Double, `b`:Double | `out`:Double | — |
| `Python.Script` | — | — | `inputs`, `outputs`, `pythonExe`, `script` |
| `Serial.Port` | `tx`:List | `connected`:Bool, `rx`:List | `autoReconnect`, `baudRate`, `dataBits` (5/6/7/8), `flowControl` (none/software/hardware), `parity` (none/even/odd/mark/space), `port`, `readChunkSize`, `readTimeoutMs`, `reconnectIntervalMs`, `stopBits` (1/2) |

## OnboardAPI (generated interfaces)

Generated by `pipegen` from the OnboardAPI data model — **52 interfaces**. Each interface `M_<X>` provides a `M_<X>.Service` and `M_<X>.Client` node whose ports are the interface's operations (`Report*`, `Notify*`, `Cmd*`, `Config*`), one port per operation parameter named `Operation.Parameter`. In addition there are **491 `Factory.M_<X>::<Struct>`** builder nodes that assemble struct values from their scalar fields.

<details><summary>Interfaces (52)</summary>

`M_ActuationCtrl`, `M_Alert`, `M_Alignment`, `M_AnalogueIo`, `M_Auth`, `M_BuiltInTest`, `M_BuiltInTestCtrl`, `M_Camera`, `M_CameraManager`, `M_CoordinateFrame`, `M_Device`, `M_Dialogue`, `M_ErrorMemory`, `M_FileTransfer`, `M_GenericInputOutput`, `M_HidCoordInput`, `M_HidDisplay`, `M_HidJoystick`, `M_HidManager`, `M_HmiAppCtrl`, `M_ImageComposer`, `M_ImageFusion`, `M_ImageProcessing`, `M_KinematicChain`, `M_LanguageManager`, `M_LaserRangeFinder`, `M_Logging`, `M_Manipulator`, `M_MetSensor`, `M_MissionCtrl`, `M_Mount`, `M_MultiMount`, `M_ObjectManager`, `M_OverlayCtrl`, `M_RecordingCtrl`, `M_ReplayCtrl`, `M_ScanCtrl`, `M_SectorSelection`, `M_Sensor`, `M_SignalGenerator`, `M_SignalProcessing`, `M_Teleoperation`, `M_TerminalCtrl`, `M_TimeManager`, `M_VideoManager`, `M_VideoMetadata`, `M_VideoObjectExport`, `M_VideoTrackerAtr`, `M_VideoTrackerCommon`, `M_VideoTrackerLosCtrl`, `M_VideoTrackerLot`, `M_Watchdog`

</details>

See the per-interface Service/Client port lists in the Doxygen API reference, or query a running engine at `GET /api/types`.

