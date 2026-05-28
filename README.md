# Flowboard

A configurable pipe-and-filter workflow engine for the
[Rheinmetall OnboardAPI](https://github.com/Rheinmetall/onboardapi) data
ecosystem. Define a workflow as a JSON graph of nodes and typed connections,
run it as a single binary, and edit it live in the built-in web UI.

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Status: v1.0.0](https://img.shields.io/badge/status-v1.0.0-green.svg)

By default it builds against the **real Rheinmetall OnboardAPI SDK**. If you
don't have that SDK, pass `-DFLOWBOARD_USE_STUB_SDK=ON` to build and run
against the bundled **stub SDK** instead — no proprietary binaries required.

## Prerequisites

- **CMake** ≥ 3.20
- A **C++20** compiler (MSVC 2022, GCC 11+, or Clang 14+)
- The **Rheinmetall OnboardAPI SDK** (default build) — or pass
  `-DFLOWBOARD_USE_STUB_SDK=ON` to use the bundled stub instead
- **Node.js + npm** — only needed for the web UI (`-DFLOWBOARD_BUILD_WEB`, on by default)

## Build

```bash
git clone --recurse-submodules https://github.com/0xEic/Flowboard.git
cd Flowboard
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel --config Release
```

This default build links the **real** OnboardAPI SDK; point CMake at it with
`-DONBOARDAPI_SDK_ROOT=<sdk-dir>` (or the `ONBOARDAPI_SDK_ROOT` env var). To
build **without** the proprietary SDK, add `-DFLOWBOARD_USE_STUB_SDK=ON`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFLOWBOARD_USE_STUB_SDK=ON
```

> The OnboardAPI data model lives in the `Rheinmetall/onboardapi` git submodule
> at `external/onboardapi`. If you cloned without `--recurse-submodules`, fetch
> it with `git submodule update --init`.

Run the tests (optional):

```bash
ctest --test-dir build -C Release --output-on-failure
```

## Run

Start the engine with a workflow file:

```bash
# Linux / macOS
./build/src/flowboard examples/sources.json

# Windows (PowerShell)
.\build\src\Release\flowboard.exe examples\sources.json
```

Then open **http://localhost:8765/** in a browser for the web UI: drag nodes
from the palette, wire ports on the canvas, edit properties, and watch live
values stream in. Use a different port with `--port <n>`.

Other ready-to-run workflows live in [`examples/`](examples/).

## Nodes

The palette groups the built-in nodes by role:

| Category | What's in it |
|---|---|
| **Debug** | Value Display, Graph Display, Trigger Button |
| **Sources** | Current Time, Time Source, Timer, Iterator, Constant, Random |
| **Transform** | Convert, Extract, Inverter, Derivative, Arithmetic, Number Holder, Manipulate Number, Manipulate String |
| **Logic & Conditions** | Compare, Threshold, Filter |
| **Lists** | Build, Constant, Accumulate, Combine, Filter, Find, Get At, Map Field, Size, Sort |
| **Timing & Sync** | Throttle, Synchronize, Key-Value Accumulator |
| **Sinks** | Log |
| **Input** | Button Handler (HidJoystick `EventButton` → per-button bool outputs) |
| **Flow & Structure** | State Machine, Group, Note |
| **OnboardAPI** | Discovery, Device Report, plus a generated Service / Client pair (and struct factories) for every interface in the data model |

Most numeric nodes (Arithmetic, Number Holder, Iterator, Manipulate Number,
Compare, Threshold, …) and the list builders work across every primitive type
via a per-node type dropdown. New OnboardAPI interfaces come from the data-model
submodule automatically. To add your own custom node, see
[`docs/ADDING_A_NODE.md`](docs/ADDING_A_NODE.md). For the full port/config
reference of every node, see [`docs/NODES.md`](docs/NODES.md).

## Build options

All options are `ON` by default unless noted. Pass with `-D<NAME>=ON|OFF`.

| Option | Default | Purpose |
|---|---|---|
| `FLOWBOARD_BUILD_WEB`           | ON  | Build and embed the web UI (needs Node + npm) |
| `FLOWBOARD_BUILD_CONTROL_PLANE` | ON  | Build the HTTP + WebSocket control-plane server |
| `FLOWBOARD_BUILD_TESTS`         | ON  | Build unit + integration tests |
| `FLOWBOARD_BUILD_BENCHMARKS`    | ON  | Build the performance benchmarks |
| `FLOWBOARD_BUILD_PIPEGEN`       | ON  | Build the `pipegen` codegen tool |
| `FLOWBOARD_USE_STUB_SDK`        | OFF | Use the in-tree stub SDK instead of the real OnboardAPI SDK |
| `FLOWBOARD_BUILD_DOCS`          | OFF | Add a `docs` target for the Doxygen documentation |

You can also point the build at a different OnboardAPI data model with
`-DONBOARDAPI_DATAMODEL_DIR=<dir>` (defaults to the `external/onboardapi`
submodule).

Linking the real OnboardAPI SDK is the default; see
[`docs/INTEGRATING_THE_REAL_SDK.md`](docs/INTEGRATING_THE_REAL_SDK.md) for how to
provide it.

## Documentation

- [`examples/`](examples/README.md) — runnable workflows, one per node category
- [`docs/NODES.md`](docs/NODES.md) — node reference (ports + config), generated
  from the registry
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — how the engine fits together
- [`docs/ADDING_A_NODE.md`](docs/ADDING_A_NODE.md) — write your own node

The full API reference + architecture + node pages are published to **GitHub
Pages** by the `Docs` workflow on every push to `main` (Settings → Pages →
"GitHub Actions"). Build the same site locally with:

```bash
cmake -S . -B build -DFLOWBOARD_BUILD_DOCS=ON
cmake --build build --target docs   # open build/docs/html/index.html
```

`docs/NODES.md` is generated — regenerate it after changing nodes with:

```bash
flowboard --dump-nodes | python tools/gen_node_docs.py > docs/NODES.md
```

## License

MIT — see [LICENSE](LICENSE). Third-party components (and the EPL-2.0 OnboardAPI
data model + proprietary SDK) are listed in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). The OnboardAPI data model is
pulled from the [`Rheinmetall/onboardapi`](https://github.com/Rheinmetall/onboardapi)
submodule and is licensed EPL-2.0 by Rheinmetall.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
