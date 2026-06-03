---
layout: default
title: PCG
parent: Tools
nav_order: 7
---

# PCG Tools

PCG tools operate on UE PCG graph assets. PCG has three important surfaces:
graph structure, node settings, and graph user parameters. Keep those separate.

## Recommended Flow

1. Inspect the graph with `pcg_graph_inspect`.
2. Use `pcg_palette` before adding a node.
3. Use `schema_inspect` before `pcg_graph_edit` commands.
4. Use `pcg_node_inspect` before editing node settings.
5. Use `pcg_parameter_inspect` before editing graph parameters.
6. Run `pcg_compile` after meaningful changes.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `pcg_graph_inspect` | Inspect graph nodes, pins, links, and defaults. |
| `pcg_palette` | Search UE PCG palette actions for node creation. |
| `pcg_node_inspect` | Inspect one PCG node instance or settings class. |
| `pcg_parameter_inspect` | Inspect graph user parameters. |
| `pcg_parameter_edit` | Edit graph user parameters. |
| `pcg_graph_edit` | Apply explicit PCG graph edit commands. |
| `pcg_graph_layout` | Format selected PCG graph nodes. |
| `pcg_compile` | Validate and compile-confirm a PCG graph. |

## `pcg_graph_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional PCG graph reference. |
| `view` | no | `overview`, `pins`, `links`, `defaults`, or `full`; defaults to `overview`. |
| `filter.nodeIds` | no | Restrict to node ids. |
| `filter.text` | no | Case-insensitive fuzzy search over node id, title, class, and compact JSON. |
| `page.limit` | no | Defaults to 50, maximum 1000. |
| `page.cursor` | no | Pagination cursor. |

Use `overview` first. Move to `pins`, `links`, or `defaults` only when planning
connections or defaults.

### Readback Limits

`pcg_graph_inspect` is the first stop for topology, pins, links, defaults, and
known `effectiveSettings`. It is not a guaranteed full Details-panel export for
every PCG settings object.

For ordinary node inspection, stay inside `pcg_graph_inspect` and
`pcg_node_inspect` when the needed value appears in pins, defaults, or
`effectiveSettings`. If a task depends on exact instance-level settings that are
not surfaced yet, inspect the live settings object with `execute` and cite that
fallback explicitly.

`Spawn Actor` is a common fallback-prone case. Template actor/class identity,
spawn options, data-layer settings, HLOD settings, and spawned actor property
override mappings may require direct settings-object inspection until the PCG
full-coverage readback work lands. Do not infer property override mappings from
topology alone.

### Selector Readback

Selector-backed settings follow UE's `FPCGAttributePropertySelector` parsing.
Compact strings are accepted for edits, but inspect output should be read from
the structured selector fields. For example, `Position.Z` is read back as an
attribute selector with `name: "Position"` and `accessors: ["Z"]`; `$Position`
is the property-selector form.

For `Filter By Attribute`, verify value filters through
`effectiveSettings.targetAttribute`. It includes `text`, `selection`, `name`,
`domain`, `attributeOrProperty`, `accessors`, `accessorPath`, `kind`, and
`valid` so agents can distinguish full-vector targets from component targets
such as `Position.Z`.

Use `pcg_node_inspect` for detailed selector property discovery. Selector-backed
settings include `valueKind: "pcgSelector"` and accept both compact UE strings
and structured selector objects in `pcg_graph_edit` `setPinDefault` or
`setProperty`.

## `pcg_palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `query` | no | Fuzzy search over UE palette label, category, tooltip, keywords, and payload. |
| `elementTypes` | no | `native`, `blueprint`, `subgraph`, `settings`, `asset`, `dataAsset`, or `other`. |
| `limit` | no | Defaults to 50, maximum 500. |
| `offset` | no | Paging offset. |

Pass the selected palette entry to `pcg_graph_edit` rather than guessing
settings classes.

## `pcg_graph_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `commands` | yes | Ordered command envelopes with `kind`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Command-specific fields are intentionally omitted from `tools/list`. Call
`schema_inspect` with `domain: pcg`, `tool: pcg_graph_edit`, and the selected
`operation`.

## `pcg_node_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | Required for instance mode. |
| `graph` | no | Optional graph reference. |
| `node.id` | no | PCG node id for instance mode. |
| `nodeClass` | no | PCG settings class path for class mode. |
| `settingsClass` | no | Alias for `nodeClass`. |

Use this before editing node settings or when you need to understand a settings
class returned by the palette.

## `pcg_parameter_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `name` | no | Exact parameter name filter. |

PCG graph parameters are graph-owned user parameters, not graph nodes.

## `pcg_parameter_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `operation` | yes | `create`, `update`, `rename`, `delete`, or `setDefault`. |
| `args` | yes | Operation-specific arguments from `schema_inspect`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Call `schema_inspect` with `domain: pcg`, `tool: pcg_parameter_edit`, and the
selected operation before editing.

## `pcg_graph_layout`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `scope` | yes | Explicit node selection to format. |
| `spacing` | no | Layout spacing. |
| `origin` | no | Optional top-left layout anchor. |

Layout changes positions only; it does not change PCG behavior.

## `pcg_compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |

Use compile after graph, node setting, or parameter changes.
