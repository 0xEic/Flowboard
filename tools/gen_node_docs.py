#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate docs/NODES.md from the engine node catalog.

The catalog is the authoritative list of registered node types, produced by:

    flowboard --dump-nodes

Usage:
    flowboard --dump-nodes | python tools/gen_node_docs.py > docs/NODES.md

Reads the catalog JSON on stdin and writes Markdown to stdout. Non-OnboardAPI
nodes are grouped into the same curated categories as the web palette and shown
in full; the generated OnboardAPI nodes are summarized (one Service + Client per
interface, plus struct factories) so the page stays readable.
"""
import json
import sys

# Emit UTF-8 regardless of the platform's console encoding so the generated file
# is byte-identical across Windows/Linux (keeps the CI drift check stable).
try:
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
except AttributeError:
    pass

# Curated category order + the node types in each (mirrors the web palette).
PALETTE_LAYOUT = [
    ("Debug", ["Debug.ValueDisplay", "Debug.GraphDisplay", "Debug.TriggerButton"]),
    ("Sources", ["Sources.CurrentTime", "Sources.TimeSource", "Sources.Timer",
                 "Sources.Iterator", "Transform.ConstantSource", "Sources.Random"]),
    ("Transform", ["Transform.Convert", "Transform.Extract", "Transform.Inverter",
                   "Transform.Derivative", "Transform.Arithmetic", "Transform.NumberHolder",
                   "Manipulate.Number", "Manipulate.String"]),
    ("Logic & Conditions", ["Transform.Compare", "Transform.Threshold", "Transform.Filter"]),
    ("Lists", ["Transform.List.Build", "Transform.List.Constant", "Transform.List.Accumulate",
               "Transform.List.Combine", "Transform.List.Filter", "Transform.List.Find",
               "Transform.List.GetAt", "Transform.List.MapField", "Transform.List.Size",
               "Transform.List.Sort"]),
    ("Timing & Sync", ["Transform.Throttle", "Transform.Synchronize",
                       "Transform.KeyValueAccumulator"]),
    ("Sinks", ["Sinks.Log"]),
    ("Input", ["Input.ButtonHandler"]),
    ("Flow & Structure", ["Flow.StateMachine", "Flow.Group", "Note",
                          "Group.Input", "Group.Output"]),
    ("OnboardAPI", ["OnboardApi.Discovery", "OnboardApi.DeviceReport"]),
]

# Canvas-only node types that have no engine registration (so they never appear
# in the catalog). Documented for completeness.
CANVAS_ONLY = {
    "Flow.Group":   "Wraps a sub-graph into a reusable group node (web editor).",
    "Group.Input":  "Boundary input terminal inside a group (web editor).",
    "Group.Output": "Boundary output terminal inside a group (web editor).",
    "Note":         "Free-text annotation pinned to the canvas; ignored by the engine.",
}

CATEGORY_OF = {t: cat for cat, types in PALETTE_LAYOUT for t in types}


def short_type(tag: str) -> str:
    return tag[len("flowboard::"):] if tag.startswith("flowboard::") else tag


def ports(entry: dict, key: str) -> str:
    # Sort by name: the registry stores ports in an unordered map, so sorting
    # keeps the generated docs deterministic across runs/platforms.
    items = sorted(entry.get(key) or [], key=lambda p: p["name"])
    if not items:
        return "—"
    return ", ".join(f"`{p['name']}`:{short_type(p['typeTag'])}" for p in items)


def config_keys(entry: dict) -> str:
    schema = entry.get("configSchema") or {}
    props = schema.get("properties") or {}
    if not props:
        return "—"
    out = []
    for k, spec in props.items():
        enum = spec.get("enum")
        if enum:
            opts = "/".join(str(e) for e in enum)
            out.append(f"`{k}` ({opts})" if len(opts) <= 40 else f"`{k}`")
        else:
            out.append(f"`{k}`")
    return ", ".join(out)


def is_onboard(name: str) -> bool:
    return name.startswith("M_") or name.startswith("Factory.")


def main() -> int:
    catalog = json.load(sys.stdin)
    by_name = {e["typeName"]: e for e in catalog}

    out = []
    w = out.append
    w("# Node Reference")
    w("")
    w("> **Generated** from the engine's node registry by `tools/gen_node_docs.py`")
    w("> (fed by `flowboard --dump-nodes`). Do not edit by hand — regenerate with:")
    w(">")
    w("> ```bash")
    w("> flowboard --dump-nodes | python tools/gen_node_docs.py > docs/NODES.md")
    w("> ```")
    w("")
    w("Ports are listed as `name`:Type for the node's **default** configuration; "
      "some nodes add, rename or retype ports based on their config (e.g. "
      "`Transform.Extract`, `Transform.Convert`, `Transform.Synchronize`). "
      "Categories mirror the editor palette.")
    w("")

    # Curated (non-OnboardAPI) categories.
    for cat, types in PALETTE_LAYOUT:
        rows = []
        for t in types:
            if t in CANVAS_ONLY and t not in by_name:
                rows.append((t, "—", "—", CANVAS_ONLY[t]))
                continue
            e = by_name.get(t)
            if not e:
                continue
            rows.append((t, ports(e, "inputs"), ports(e, "outputs"), config_keys(e)))
        if not rows:
            continue
        w(f"## {cat}")
        w("")
        w("| Node | Inputs | Outputs | Config |")
        w("|---|---|---|---|")
        for name, ins, outs, cfg in rows:
            w(f"| `{name}` | {ins} | {outs} | {cfg} |")
        w("")

    # Any registered non-OnboardAPI node not placed above.
    extras = sorted(n for n, e in by_name.items()
                    if not is_onboard(n) and n not in CATEGORY_OF)
    if extras:
        w("## Other")
        w("")
        w("| Node | Inputs | Outputs | Config |")
        w("|---|---|---|---|")
        for n in extras:
            e = by_name[n]
            w(f"| `{n}` | {ports(e, 'inputs')} | {ports(e, 'outputs')} | {config_keys(e)} |")
        w("")

    # OnboardAPI summary.
    services = sorted({n[:-len(".Service")] for n in by_name if n.endswith(".Service")})
    clients = sorted({n[:-len(".Client")] for n in by_name if n.endswith(".Client")})
    interfaces = sorted(set(services) | set(clients))
    factories = sorted(n for n in by_name if n.startswith("Factory."))

    if interfaces or factories:
        w("## OnboardAPI (generated interfaces)")
        w("")
        w(f"Generated by `pipegen` from the OnboardAPI data model — "
          f"**{len(interfaces)} interfaces**. Each interface `M_<X>` provides a "
          f"`M_<X>.Service` and `M_<X>.Client` node whose ports are the interface's "
          f"operations (`Report*`, `Notify*`, `Cmd*`, `Config*`), one port per "
          f"operation parameter named `Operation.Parameter`. In addition there are "
          f"**{len(factories)} `Factory.M_<X>::<Struct>`** builder nodes that assemble "
          f"struct values from their scalar fields.")
        w("")
        w("<details><summary>Interfaces (" + str(len(interfaces)) + ")</summary>")
        w("")
        w(", ".join(f"`{i}`" for i in interfaces))
        w("")
        w("</details>")
        w("")
        w("See the per-interface Service/Client port lists in the Doxygen API "
          "reference, or query a running engine at `GET /api/types`.")
        w("")

    sys.stdout.write("\n".join(out))
    if out and not out[-1].endswith("\n"):
        sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
