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
examples use OnboardAPI Service/Client nodes.

## Core (no OnboardAPI needed)

| File | Demonstrates |
|---|---|
| `sources.json` | `Sources.Timer`, `Sources.CurrentTime`, `Sources.TimeSource`, `Sources.Iterator`, `Transform.ConstantSource` |
| `transforms-and-logic.json` | `Transform.Convert`, `Transform.Arithmetic`, `Manipulate.Number`, `Manipulate.String`, `Transform.Threshold`, `Transform.Compare`, `Transform.Inverter`, `Transform.Filter` |
| `timing-and-sync.json` | `Transform.Throttle` (rate-limit) and `Transform.Synchronize` (barrier/join) over two derived streams (x and x*x) |
| `debug-and-display.json` | `Debug.GraphDisplay`, `Debug.ValueDisplay`, `Debug.TriggerButton`, `Sinks.Log` fed by a `Sources.Iterator` ramp |
| `state-machine.json` | `Flow.StateMachine` (Normal → Warning → Critical) driven by `Transform.Threshold` triggers on an Iterator ramp |

## OnboardAPI

| File | Demonstrates |
|---|---|
| `onboard-example-mount-joystick.json` | `M_Mount.Service` as the main node, with a `M_Mount.Client` injecting azimuth/elevation movement via `NotifyAngle` + `NotifyRateAbsolute`; the Service's matching outputs are logged, plotted and thresholded |
| `onboard-discovery-list-nodes.json` | `OnboardApi.Discovery` enumerating the open Service/Client endpoints on a domain (re-queried by a `Sources.Timer`), with the resulting list logged and counted via `Transform.List.*` |

> The `Note` nodes are canvas-only annotations; the engine ignores them when a
> graph is loaded.
