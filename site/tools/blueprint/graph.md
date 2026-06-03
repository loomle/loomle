---
layout: default
title: Blueprint Graphs
parent: Blueprint
nav_order: 2
---

# Blueprint Graphs

Blueprint graph tools own graph-local nodes, pins, links, defaults, comments,
layout, and node placement. They do not edit Blueprint member signatures.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `blueprint_graph_list` | List graphs in a Blueprint asset. |
| `blueprint_graph_inspect` | Inspect nodes, links, and graph views. |
| `blueprint_graph_edit` | Apply explicit graph edit commands. |
| `blueprint_graph_layout` | Format selected graph regions. |

## `blueprint_graph_list`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `includeCompositeSubgraphs` | no | Include composite or inline subgraphs. |

Use graph ids from this tool when possible.

## `blueprint_graph_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | yes | Preferred graph address: `{id}` or `{name}`. |
| `view` | no | `overview` or `wiring`; defaults to `overview`. |
| `filter.nodeIds` | no | Restrict to node ids. |
| `filter.text` | no | Text filter. |
| `page.limit` | no | Defaults to 50, maximum 1000. |
| `page.cursor` | no | Pagination cursor. |

Use `overview` first. Use `wiring` when planning connections. If a node has
`hasNodeEditCapabilities: true`, inspect it with `blueprint_node_inspect` before
editing local pins.

In `wiring` view, pin-local `linkedTo` is a UE-style reciprocal peer list. For
flow direction, prefer `links[*].directionNormalized` plus `fromNodeId` /
`fromPin` and `toNodeId` / `toPin`; normalized links run output pin to input
pin.

### Readability Tiers

Most nodes are one-shot readable through `blueprint_graph_inspect`: variables,
function calls, branches, casts, math, and ordinary flow-control nodes expose
their relevant pins and references directly.

Some nodes need a second graph read. For a local `K2Node_MacroInstance`, inspect
`k2Extensions.macro.macroGraph`, then call `blueprint_graph_list` on the same
asset. If that graph exists on the same Blueprint, inspect it to read the macro
body. If it does not, treat the node as an external or library macro and rely on
the call surface exposed in the current graph.

`K2Node_Tunnel` nodes are graph boundary/interface nodes for macro or composite
graphs. They are not a hidden implementation body by themselves.

`K2Node_AsyncAction` nodes expose callable pins and result pins, but their
implementation is runtime or C++ backed; do not assume there is a Blueprint
subgraph to inspect.

Timeline nodes expose structural/template summary through
`embeddedTemplate` / `effectiveSettings` where available, including timeline
identity and high-level settings. Keyframe-level curve truth is not guaranteed
by `blueprint_graph_inspect`; use `execute` to inspect Blueprint timeline
templates or backing curve assets when a task depends on authored key times,
values, interpolation, or curve samples.

## `blueprint_graph_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | no | Preferred graph address. |
| `graphName` | no | Legacy graph address. Prefer `graph`. |
| `commands` | yes | Ordered command envelopes with `kind`. |
| `continueOnError` | no | Continue after operation-level failure. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Command-specific fields are intentionally omitted from `tools/list`. Call
`schema_inspect` with `domain: blueprint`, `tool: blueprint_graph_edit`, and
the selected `operation`.

### Edit Boundary

Use graph edit for:

- Adding a node from a palette entry.
- Connecting or disconnecting explicit pins.
- Setting pin defaults.
- Removing or moving a node.
- Editing node comments or enabled state.

Use `blueprint_member_edit` for member signatures. Use `blueprint_node_edit` for
node-local structures such as switch cases or Format Text arguments.

## `blueprint_graph_layout`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | yes | Preferred graph address. |
| `scope` | yes | `selection` with explicit nodes, or `tree` with root exec node. |
| `spacing` | no | Layout spacing; defaults to x 360, y 180. |
| `origin` | no | Optional top-left layout anchor. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Layout changes positions only; it does not change Blueprint behavior.
