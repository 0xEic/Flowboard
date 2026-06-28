# The `Python.Script` Node {#python_node}

`Python.Script` lets you wire arbitrary Python code into a Flowboard graph. You
declare the node's inputs and outputs in its properties; the script is run in a
dedicated Python subprocess that receives input values, computes whatever you
like, and emits results back. The web UI ships a CodeMirror-based editor with
Python syntax highlighting and autocompletion.

```
+----------------+           +----------------------+           +--------------+
| ConstantSource | ─ x ──▶  |   Python.Script      | ─ y ──▶  | ValueDisplay |
| 21 (Double)    |           | def on_input(...):   |           | → 42         |
+----------------+           |   emit('y', x * 2)   |           +--------------+
                              +----------------------+
```

## Runtime requirements

Python must be on `PATH` at graph-start time. The node defaults to `python` on
Windows and `python3` on Linux/macOS. Override per node via the `pythonExe`
property, or globally with the `FLOWBOARD_PYTHON` environment variable.

Only the standard library is assumed. If your script imports a third-party
package, install it into the same Python interpreter the host invokes.

The script itself does **not** need to be installed anywhere: the host writes
your source to a temp file at node start and deletes it on stop. The node's
spawned subprocess is exclusive to that node instance.

## Inputs, outputs and the script contract

In the property form (or the JSON config) you declare:

| Property      | Meaning                                                             |
|---|---|
| `inputs`      | Array of `{ "name": str, "type": typeTag }` — the node's input ports.  |
| `outputs`     | Array of `{ "name": str, "type": typeTag }` — the node's output ports. |
| `script`      | Python source. Must define `def on_input(port, value)`.                |
| `pythonExe`   | Optional override for the Python executable to spawn.                  |

Every built-in Flowboard type is supported. Values are marshalled as JSON, so
each port maps to the most natural Python value:

| Type tag | Python value | Notes |
|---|---|---|
| `flowboard::Bool`   | `bool`            | |
| `flowboard::Int64`  | `int`             | |
| `flowboard::Int32`  | `int`             | |
| `flowboard::Int16`  | `int`             | |
| `flowboard::UInt8`  | `int`             | |
| `flowboard::UInt16` | `int`             | |
| `flowboard::UInt32` | `int`             | |
| `flowboard::UInt64` | `int`             | |
| `flowboard::Double` | `float`           | |
| `flowboard::Float`  | `float`           | 32-bit precision on the wire. |
| `flowboard::String` | `str`             | |
| `flowboard::Char`   | `str` of length 1 | A bare number is also accepted on emit. |
| `flowboard::List`   | `list`            | Element values are themselves JSON-shaped (numbers, strings, lists, dicts). The list's `element_type_tag` is not surfaced to Python. |

**OnboardAPI struct types** (the `M_*::SomeType` tags emitted by codegen) are
also fully supported. Pipegen registers a port factory and JSON marshaller for
every struct, so a Python.Script port typed `M_Common::TimeType` (or any other
struct) round-trips through the generated `to_json` / `from_json` ADL overloads:
your script sees a Python `dict` shaped like the struct's fields, and emitting
a dict (or any JSON-coercible value) builds the struct back. Run
`flowboard --dump-nodes` to see every type tag the running engine knows about.

Your script runs inside a wrapper that exposes two names:

- **`inputs`** — a `dict[str, Any]` holding the most recent value per input
  port. It is updated *before* `on_input` is called on each new value.
- **`emit(port, value)`** — sends `value` to the output port `port`. The output
  port name and type must match one you declared.

The host calls `on_input(port, value)` once per incoming value (no event is
emitted automatically; if you want one, call `emit` from `on_input`). Top-level
code in your script runs once when the node starts, so you can import modules,
open files, or initialize state there.

### Examples

**Doubler — one input, one output, one property-free node.**

```python
def on_input(port, value):
    if port == "x":
        emit("y", value * 2)
```

**Adder — two inputs, emit when both have produced a value at least once.**

```python
def on_input(port, value):
    if "a" in inputs and "b" in inputs:
        emit("sum", inputs["a"] + inputs["b"])
```

**Stateful counter — module-level state persists across calls.**

```python
count = 0

def on_input(port, value):
    global count
    count += 1
    emit("n", count)
```

**Calling an external library.**

```python
import statistics

window = []

def on_input(port, value):
    window.append(value)
    if len(window) > 10:
        window.pop(0)
    emit("mean", statistics.fmean(window))
```

**Filtering a list (`flowboard::List` input/output).**

```python
def on_input(port, value):
    if port == "in":
        # value is a Python list; emit a new list of just the positive items.
        emit("out", [x for x in value if x > 0])
```

**Working with an OnboardAPI struct.**

Struct-typed ports surface in Python as plain `dict`s — one key per field,
shaped by the struct's codegen'd `to_json` overload. To emit a struct, return a
`dict` (or any value coercible by the struct's `from_json`); to inspect one, pull
fields from the dict.

```python
# Input  'msg':  M_Logging::LogMessageType   ->  dict
# Output 'text': flowboard::String           ->  str
def on_input(port, value):
    if port == "msg":
        emit("text", value.get("Message", ""))
```

Use `flowboard --dump-nodes` to discover the exact field shape of any struct
type registered in your build.

## Runnable graphs

Two ready-to-run graphs ship in `examples/` (both need `python` / `python3` on
`PATH`):

**[`examples/python-doubler.json`](../examples/python-doubler.json)** — the
minimal node. A ConstantSource (`21`, Double) feeds a Python.Script doubler
(`emit('y', x * 2)`) whose output drives a `Debug.ValueDisplay`. Run it:

```bash
flowboard examples/python-doubler.json
```

Then open <http://localhost:8765/> — the `Debug.ValueDisplay` node shows `42`.

**[`examples/python-onboard-struct.json`](../examples/python-onboard-struct.json)**
— struct in, struct out. Two `Sources.Iterator` ramps feed a
`Factory.M_Mount::ScaledRateType` that assembles an `M_Mount::ScaledRateType`
struct; the Python.Script node receives it as a `dict`, applies a gain + clamp
to both fields, and emits a struct back on a port of the same type. The result
is logged whole, shown field-by-field in a `Debug.ValueDisplay`, and one field
is pulled out with `Transform.Extract` and plotted. (The `M_Mount` struct types
are compiled into the binary.) Run it:

```bash
flowboard examples/python-onboard-struct.json
```

## Editing in the web UI

Open a `Python.Script` node in the side panel. The Inputs and Outputs sections
let you add named typed ports; the script editor below is a CodeMirror 6 setup
with:

- Python syntax highlighting, folding, and bracket matching.
- Autocomplete for Python keywords/builtins, the host helpers (`emit`,
  `inputs`, `on_input`), and the names of the ports you've declared on this
  node (typed-aware: input names show their type, outputs show theirs).
- Standard editor shortcuts: Ctrl-Space to trigger completions.
- An **⤢ Expand** button opens the same editor in a larger modal (Esc, the
  backdrop, or ✕ closes it); edits there sync live with the inline editor.

Click **Save & Reload** in the footer to apply your changes to the running
graph (Flowboard rebuilds the node atomically).

## Behaviour and limits

- **Latency.** Values travel through pipes and JSON, so per-call overhead is in
  the tens of microseconds. Fine for control / business logic; not the right
  fit for inner-loop signal processing.
- **Throughput.** A single Python.Script node serialises on its subprocess's
  stdin — order is preserved. Run multiple instances in parallel for
  independent streams.
- **Crash safety.** A bug in your script raises a Python exception, which the
  wrapper catches and prints to the host's log. The subprocess survives. A
  fatal crash (e.g. segfault in a native extension) takes down only that
  node's subprocess; on next graph reload it is respawned.
- **Struct round-trip is JSON-based**, so Python sees a `dict` shape — not the
  original C++ object. Field renames / type changes upstream in the data model
  (the `external/onboardapi` submodule) will propagate to your script's view.
- **No sandbox.** The script runs as whatever user started Flowboard. Only
  load graphs from sources you trust.

## Troubleshooting

| Symptom                                            | Likely cause                                                  |
|---|---|
| `failed to spawn 'python'`                         | Python isn't on PATH; set `pythonExe` or `FLOWBOARD_PYTHON`. |
| `script must define on_input(port, value)`        | Top-level definition missing in your script.                  |
| `unknown output port 'foo'`                       | `emit('foo', …)` referenced an output you didn't declare.    |
| `cannot coerce value for output 'x'`              | The value emitted doesn't fit the declared type tag.          |
| Nothing happens                                    | Check that `on_input` actually calls `emit`.                  |
