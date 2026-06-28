# Examples

Each file is a runnable workflow graph. Open one in the web UI (the `Note`
nodes on the canvas explain what every part does) or run it headless:

```bash
# Linux / macOS
./build/src/flowboard examples/sources.json
# Windows (PowerShell)
.\build\src\Release\flowboard.exe examples\sources.json
```

Then open <http://localhost:8765/> and watch the live-values / log pane.

Between them the examples exercise every built-in node type. The non-OnboardAPI
examples run on the bundled stub SDK with no extra setup; the `onboard-*`
examples use OnboardAPI Service/Client nodes. The `Python.*` examples
(`python-*.json`) additionally need `python` (or `python3`) on `PATH`.

## Core (no OnboardAPI needed)

| File | Demonstrates |
|---|---|
| `sources.json` | `Sources.Timer`, `Sources.CurrentTime`, `Sources.TimeSource`, `Sources.Iterator`, `Transform.ConstantSource` |
| `transforms-and-logic.json` | `Transform.Convert`, `Transform.Arithmetic`, `Manipulate.Number`, `Manipulate.String`, `Transform.Threshold`, `Transform.Compare`, `Transform.Inverter`, `Transform.Filter` |
| `timing-and-sync.json` | `Transform.Throttle` (rate-limit) and `Transform.Synchronize` (barrier/join) over two derived streams (x and x*x) |
| `debug-and-display.json` | `Debug.GraphDisplay`, `Debug.ValueDisplay`, `Debug.TriggerButton`, `Sinks.Log` fed by a `Sources.Iterator` ramp |
| `state-machine.json` | `Flow.StateMachine` (Normal → Warning → Critical) driven by `Transform.Threshold` triggers on an Iterator ramp |
| `complex-state-machine.json` | Advanced `Flow.StateMachine`: three **parallel** chains running at once (Power, Safety, and a free-running Ping↔Pong timer oscillator), a nested **sub-state-machine** inside `On`, and all three transition link kinds — `trigger`, `null` (fires on entry), `timed` (auto-fires after a delay); buttons drive the triggers while live state/transition is logged and the timed chains are plotted |
| `python-doubler.json` | `Python.Script` doubling a constant `Double` (minimal Python node). Requires `python` |
| `can-bus.json` | The CAN/Bus nodes on the **virtual** (in-process) adapter — `Can.Bus` ×2 (tx + listen-only rx on `virtual:bus0`), two `Can.Encode` sending IDs `0x100`/`0x200` (set via each encoder's `defaults`), `Can.Filter` keeping `0x100`, `Can.Decode` — with the payload built and recovered by `Bytes.Pack`/`Bytes.Unpack` (no Python). Runs as-is |
| `serial-port.json` | `Serial.Port` ×2 (tx + rx) with `Framer.Fixed` reframing a fixed 4-byte record `[len=3, count, '*', '\n']`, built and decoded by `Bytes.Pack`/`Bytes.Unpack` (no Python). Swap `Framer.Fixed` for `Framer.Delimiter` (`'\n'`) or `Framer.LengthPrefix` (1-byte length) to frame the same stream by separator or length. Self-runs over a com0com `COM3↔COM4` pair; point at real hardware otherwise |

## OnboardAPI

| File | Demonstrates |
|---|---|
| `onboard-example-mount-joystick.json` | `M_Mount.Service`/`Client` with two `Sources.Iterator`s sweeping azimuth/elevation through `Factory.M_Mount::MountPositionType`, plotted via `Debug.GraphDisplay` + `Transform.Extract`. A `M_HidJoystick` Service/Client pair carries `EventButton` presses **atomically** (`Factory.M_HidJoystick::EventButton_Args` → `Input.ButtonHandler`'s `args` struct input); the handler's `Start Az`/`Stop Az`/`Start El`/`Stop El` outputs start and stop the iterators |
| `onboard-discovery-list-nodes.json` | `OnboardApi.Discovery` enumerating the open Service/Client endpoints on a domain (re-queried by a `Sources.Timer`), with the resulting list logged and counted via `Transform.List.*` |
| `python-onboard-struct.json` | `Python.Script` with an OnboardAPI struct (`M_Mount::ScaledRateType`) on **both** its input and output ports — assembled by `Factory.M_Mount::ScaledRateType`, transformed in Python, then logged whole and unpacked with `Transform.Extract`. Requires `python` |

> The `Note` nodes are canvas-only annotations; the engine ignores them when a
> graph is loaded.
