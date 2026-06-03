---
layout: default
title: Material
parent: Tools
nav_order: 6
---

# Material Tools

Material tools operate on UE Material and Material Function expression graphs.
Use `material_palette` for creation, graph tools for nodes and links, and node
tools for expression properties.

## Recommended Flow

1. Inspect the graph with `material_graph_inspect`.
2. Use `material_palette` before adding an expression node.
3. Use `schema_inspect` before `material_graph_edit` commands.
4. Use `material_node_inspect` before `material_node_edit`.
5. Run `material_compile` after meaningful graph or property changes.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `material_list` | List expressions in a material asset. |
| `material_graph_inspect` | Inspect expression nodes, pins, and links. |
| `material_graph_edit` | Apply explicit Material graph edit commands. |
| `material_graph_layout` | Format selected Material graph nodes. |
| `material_compile` | Compile a Material asset and return diagnostics. |
| `material_node_inspect` | Inspect one expression instance or expression class. |
| `material_node_edit` | Set one editable expression property. |
| `material_palette` | Search UE Material palette actions for expression creation. |

## `material_list`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material or Material Function asset path. |

Use this for a quick expression list. Use `material_graph_inspect` when you need
graph wiring or pins.

## `material_graph_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material or Material Function asset path. |
| `graph` | no | Optional graph reference. |
| `graphRef` | no | Compatibility graph reference. |
| `graphName` | no | Compatibility graph name. |
| `nodeIds` | no | Restrict to expression ids. |
| `nodeClasses` | no | Restrict by expression class. |
| `includeConnections` | no | Include link detail. |

## `material_palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | Material or Material Function asset path. |
| `graph` | no | Optional graph reference. |
| `query` | no | Fuzzy search over label, category, tooltip, keywords, and payload. |
| `elementTypes` | no | Palette element families. |
| `limit` | no | Defaults to 50, maximum 500. |
| `offset` | no | Paging offset. |

Pass the selected palette entry to `material_graph_edit` rather than guessing
expression classes.

## `material_graph_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material or Material Function asset path. |
| `graph` | no | Optional graph reference. |
| `commands` | yes | Ordered command envelopes with `kind`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Command-specific fields are intentionally omitted from `tools/list`. Call
`schema_inspect` with `domain: material`, `tool: material_graph_edit`, and the
selected `operation`.

## `material_node_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | Required for instance mode. |
| `nodeId` | no | Expression instance id from graph inspect. |
| `nodeClass` | no | Expression class path for class mode. |

Use instance mode before editing an existing expression. Use class mode to learn
properties before creating or editing a class of expression.

## `material_node_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material or Material Function asset path. |
| `graph` | no | Optional graph reference. |
| `node` | yes | `{id}` from inspect or `{alias}` from an earlier edit command. |
| `property` | yes | Editable property name from `material_node_inspect`. |
| `value` | yes | JSON scalar or `{importText: "..."}` for UE import text values. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

## `material_graph_layout`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material or Material Function asset path. |
| `graph` | no | Optional graph reference. |
| `scope` | yes | Explicit node selection to format. |
| `spacing` | no | Layout spacing. |
| `origin` | no | Optional top-left layout anchor. |

Layout changes positions only; it does not change graph semantics.

## `material_compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Material asset path. |

Compile after graph edits or property edits that may affect shader output.
