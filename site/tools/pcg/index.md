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

1. Inspect the graph with `pcg.graph.inspect`.
2. Use `pcg.palette` before adding a node.
3. Use `schema.inspect` before `pcg.graph.edit` commands.
4. Use `pcg.node.inspect` before editing node settings.
5. Use `pcg.parameter.inspect` before editing graph parameters.
6. Run `pcg.compile` after meaningful changes.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `pcg.graph.inspect` | Inspect graph nodes, pins, links, and defaults. |
| `pcg.palette` | Search UE PCG palette actions for node creation. |
| `pcg.node.inspect` | Inspect one PCG node instance or settings class. |
| `pcg.parameter.inspect` | Inspect graph user parameters. |
| `pcg.parameter.edit` | Edit graph user parameters. |
| `pcg.graph.edit` | Apply explicit PCG graph edit commands. |
| `pcg.graph.layout` | Format selected PCG graph nodes. |
| `pcg.compile` | Validate and compile-confirm a PCG graph. |

## `pcg.graph.inspect`

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

`pcg.graph.inspect` is the first stop for topology, pins, links, defaults, and
known `effectiveSettings`. It is not a guaranteed full Details-panel export for
every PCG settings object.

For ordinary node inspection, stay inside `pcg.graph.inspect` and
`pcg.node.inspect` when the needed value appears in pins, defaults, or
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

Use `pcg.node.inspect` for detailed selector property discovery. Selector-backed
settings include `valueKind: "pcgSelector"` and accept both compact UE strings
and structured selector objects in `pcg.graph.edit` `setPinDefault` or
`setProperty`.

## `pcg.palette`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `query` | no | Fuzzy search over UE palette label, category, tooltip, keywords, and payload. |
| `elementTypes` | no | `native`, `blueprint`, `subgraph`, `settings`, `asset`, `dataAsset`, or `other`. |
| `limit` | no | Defaults to 50, maximum 500. |
| `offset` | no | Paging offset. |

Pass the selected palette entry to `pcg.graph.edit` rather than guessing
settings classes.

## `pcg.graph.edit`

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
`schema.inspect` with `domain: pcg`, `tool: pcg.graph.edit`, and the selected
`operation`.

## `pcg.node.inspect`

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

## `pcg.parameter.inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | no | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `name` | no | Exact parameter name filter. |

PCG graph parameters are graph-owned user parameters, not graph nodes.

## `pcg.parameter.edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `operation` | yes | `create`, `update`, `rename`, `delete`, or `setDefault`. |
| `args` | yes | Operation-specific arguments from `schema.inspect`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Call `schema.inspect` with `domain: pcg`, `tool: pcg.parameter.edit`, and the
selected operation before editing.

## `pcg.graph.layout`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |
| `scope` | yes | Explicit node selection to format. |
| `spacing` | no | Layout spacing. |
| `origin` | no | Optional top-left layout anchor. |

Layout changes positions only; it does not change PCG behavior.

## `pcg.compile`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | PCG graph asset path. |
| `graph` | no | Optional graph reference. |

Use compile after graph, node setting, or parameter changes.
