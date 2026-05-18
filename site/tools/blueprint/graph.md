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
| `blueprint.graph.list` | List graphs in a Blueprint asset. |
| `blueprint.graph.inspect` | Inspect nodes, links, and graph views. |
| `blueprint.graph.edit` | Apply explicit graph edit commands. |
| `blueprint.graph.layout` | Format selected graph regions. |

## `blueprint.graph.list`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `includeCompositeSubgraphs` | no | Include composite or inline subgraphs. |

Use graph ids from this tool when possible.

## `blueprint.graph.inspect`

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
`hasNodeEditCapabilities: true`, inspect it with `blueprint.node.inspect` before
editing local pins.

## `blueprint.graph.edit`

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
`schema.inspect` with `domain: blueprint`, `tool: blueprint.graph.edit`, and
the selected `operation`.

### Edit Boundary

Use graph edit for:

- Adding a node from a palette entry.
- Connecting or disconnecting explicit pins.
- Setting pin defaults.
- Removing or moving a node.
- Editing node comments or enabled state.

Use `blueprint.member.edit` for member signatures. Use `blueprint.node.edit` for
node-local structures such as switch cases or Format Text arguments.

## `blueprint.graph.layout`

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
