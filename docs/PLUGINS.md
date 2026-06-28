# Writing Flowboard Plugins {#plugins}

Flowboard can load **node plugins** at runtime. A plugin is a small C++ shared
library (`.dll` on Windows, `.so` on Linux, `.dylib` on macOS) that registers one
or more node types. Once loaded, plugin nodes behave exactly like built-in ones:
they appear in the web palette, get property forms, stream live values on the
canvas, and can be used in any graph JSON.

A complete, buildable example lives in
[`examples/plugins/hello_plugin`](../examples/plugins/hello_plugin).

## How it works

At startup the host scans the plugin folder, loads every shared library it finds,
checks each one's ABI version, and calls its registration entry point — passing
the process-wide node registry. The plugin adds its node factories (and optional
property schemas) to that registry. From then on the nodes are indistinguishable
from built-ins, because the catalog, loader, wiring and live-value taps all work
off the same registry and the node's runtime ports.

```
flowboard.exe                      plugins/hello_plugin.dll
   │                                   │
   ├─ load library ───────────────────►│
   ├─ flowboard_plugin_abi_version() ─►│  returns 1
   ├─ flowboard_plugin_register(reg) ─►│  reg.register_type("Plugin.Scale", …)
   │                                   │  reg.register_schema("Plugin.Scale", …)
   └─ "Plugin.Scale" now usable in graphs, palette, forms, live values
```

## Where plugins are loaded from

In priority order:

1. The `--plugins <dir>` command-line flag.
2. The `FLOWBOARD_PLUGIN_DIR` environment variable.
3. A `plugins/` folder next to the `flowboard` executable (the default).

A missing folder is not an error — the host just logs that none were found.
Files whose name does not end in the platform's shared-library extension are
ignored, as are libraries that fail the ABI check or lack the entry points.

## Build & ABI requirements

Plugins share the host's C++ runtime objects (a node created in the plugin is
driven by the host), so the binary contract is strict. A plugin **must** be
built with:

- The **same compiler / toolchain** as the host (e.g. the same MSVC version, or
  a matching GCC/Clang).
- **C++20**.
- The **dynamic C runtime**. On Windows that means `/MD` (CMake's default), so
  the host and plugin share one heap — a node allocated in the plugin is freed by
  the host.
- The **same Flowboard version** / headers. The host refuses to load a plugin
  whose `flowboard_plugin_abi_version()` differs from its own
  `FLOWBOARD_PLUGIN_ABI_VERSION` (currently **1**).

Use only **built-in port types** for your node's ports — `double`, `bool`,
`int64_t`, `std::string`, the other scalars in
[`include/flowboard/builtin_types.hpp`](../include/flowboard/builtin_types.hpp),
and `flowboard::ListValue`. These are the types the host already knows how to
wire and stream. (Custom message types would need wiring/tap factories the host
doesn't have, and are out of scope for plugins.)

## Writing a plugin

Include the single SDK header and write your node exactly like an in-tree one,
then add the entry points.

```cpp
// my_plugin.cpp
#include "flowboard/plugin.hpp"
#include <memory>

namespace {
using namespace flowboard;

// out = in * factor   (one property, one input, one output)
class ScaleNode : public Node {
public:
    ScaleNode(std::string id, nlohmann::json const& cfg)
        : Node(std::move(id), "Plugin.Scale"),
          in_("in"), out_("out"),
          factor_(cfg.value("factor", 2.0)) {        // read a property
        register_input(&in_);
        register_output(&out_);
    }
    void on_start() override {
        in_.set_internal_sink([this](InputPort<double>::Value v) {
            enqueue([this, v] {
                out_.emit(std::make_shared<const double>(*v * factor_));
            });
        });
    }
private:
    InputPort<double>  in_;
    OutputPort<double> out_;
    double             factor_;
};
}  // namespace

FLOWBOARD_DECLARE_PLUGIN("My Cool Nodes")   // ABI version + name entry points

extern "C" FLOWBOARD_PLUGIN_EXPORT
void flowboard_plugin_register(flowboard::NodeRegistry& reg) {
    OP_REGISTER_NODE_INTO_WITH_SCHEMA(
        reg, "Plugin.Scale", ScaleNode,
        R"JSON({
          "$schema":"http://json-schema.org/draft-07/schema#",
          "type":"object",
          "properties":{
            "factor":{"type":"number","title":"Factor","default":2.0}
          },
          "additionalProperties":false
        })JSON",
        R"JSON({"factor":2.0})JSON");
}
```

What each piece does:

- **The node class** — subclass `flowboard::Node`. Create `InputPort<T>` /
  `OutputPort<T>` members, `register_input` / `register_output` them in the
  constructor (this defines the node's **inputs and outputs**), and wire input
  sinks in `on_start()`. Read **properties** from the JSON `cfg` with
  `cfg.value("name", default)`.
- **`FLOWBOARD_DECLARE_PLUGIN("name")`** — emits the required
  `flowboard_plugin_abi_version()` and the optional `flowboard_plugin_name()`.
- **`flowboard_plugin_register`** — the host calls this; register each node with
  `OP_REGISTER_NODE_INTO(reg, "Type.Name", Class)` or, to also publish a
  property form, `OP_REGISTER_NODE_INTO_WITH_SCHEMA(reg, "Type.Name", Class,
  schemaJson, defaultsJson)`. The schema is a draft-07 JSON Schema; the defaults
  are a flat object used to seed the form and probe the node's ports.

## Building a plugin

A plugin links the lightweight **`flowboard_plugin_sdk`** static library, which
contains just the node runtime (no OnboardAPI dependency). The simplest, most
reliable way is to build inside the Flowboard source tree:

```cmake
# CMakeLists.txt for your plugin (mirrors examples/plugins/hello_plugin)
add_library(my_plugin MODULE my_plugin.cpp)
target_link_libraries(my_plugin PRIVATE flowboard_plugin_sdk)
target_compile_features(my_plugin PRIVATE cxx_std_20)
set_target_properties(my_plugin PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:flowboard>/plugins")
```

Add `add_subdirectory(path/to/my_plugin)` to a CMake file in the tree (after
`add_subdirectory(src)`), then build normally. The module is emitted straight
into `<build>/.../plugins/` next to the executable.

> Building **out of tree** is possible too — point your CMake at Flowboard's
> `include/` directory and link a prebuilt `flowboard_plugin_sdk` — but you must
> match the host's compiler, C++ standard and runtime exactly (see above).

## Deploying & using a plugin

1. Copy the built shared library into the host's plugin folder (the default is a
   `plugins/` directory next to `flowboard`, or wherever `--plugins` /
   `FLOWBOARD_PLUGIN_DIR` points).
2. Start `flowboard`. The log shows each plugin as it loads:
   `plugins: loaded 'My Cool Nodes' from my_plugin.dll (1 node type)`.
3. Verify from the command line:
   ```bash
   flowboard --dump-nodes        # the catalog now includes your node types
   ```
4. Use the node in the web UI palette, or reference it in a graph JSON by its
   registered type name:
   ```json
   {
     "version": "1.0",
     "nodes": [
       { "id": "src",   "type": "Transform.ConstantSource",
         "config": { "outputType": "flowboard::Double", "value": 21 } },
       { "id": "scale", "type": "Plugin.Scale", "config": { "factor": 2.0 } },
       { "id": "log",   "type": "Sinks.Log",
         "config": { "inputType": "flowboard::Double", "prefix": "scaled" } }
     ],
     "edges": [
       { "from": "src.out",   "to": "scale.in", "policy": "block", "capacity": 16 },
       { "from": "scale.out", "to": "log.in",   "policy": "block", "capacity": 16 }
     ]
   }
   ```

## The bundled example

[`examples/plugins/hello_plugin`](../examples/plugins/hello_plugin) is built by
default (toggle with `-DFLOWBOARD_BUILD_EXAMPLE_PLUGIN=OFF`) and emitted into the
build's `plugins/` folder, so a fresh build already loads it. It provides:

| Node | Ports | Property | Behaviour |
|---|---|---|---|
| `Plugin.Scale` | `in` → `out` (Double) | `factor` (default 2.0) | `out = in * factor` |
| `Plugin.Sum`   | `a`, `b` → `out` (Double) | — | `out = a + b` (once both seen) |

Read [`hello_plugin.cpp`](../examples/plugins/hello_plugin/hello_plugin.cpp) for
the full, commented source.

## Limitations

- Port types are limited to the built-in Flowboard types (see above).
- Plugins are not sandboxed — they run as native code in the host process. Only
  load plugins you trust.
- The ABI is versioned and unstable across major changes; rebuild plugins
  against the Flowboard version you run.
