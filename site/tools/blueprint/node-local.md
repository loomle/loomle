---
layout: default
title: Node-Local Edits
parent: Blueprint
nav_order: 3
---

# Blueprint Node-Local Edits

Some Blueprint nodes own local structure that is neither graph wiring nor a
Blueprint member signature. Examples include Switch cases, Sequence pins, Select
option pins, Format Text arguments, and SetFieldsInStruct field visibility.

## Tool Summary

| Tool | Purpose |
| --- | --- |
| `blueprint_node_inspect` | Inspect one node's pins, defaults, node-local state, and edit capabilities. |
| `blueprint_node_edit` | Edit node-local structure on one existing node. |

## `blueprint_node_inspect`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | yes | Preferred graph address: `{id}` or `{name}`. |
| `node.id` | yes | Node id from `blueprint_graph_inspect`. |

Use this when `blueprint_graph_inspect` marks a node with
`hasNodeEditCapabilities: true`, or when full pin/default details are needed for
one node.

## `blueprint_node_edit`

### Parameters

| Field | Required | Notes |
| --- | --- | --- |
| `assetPath` | yes | Blueprint asset path. |
| `graph` | yes | Preferred graph address. |
| `node.id` | yes | Existing graph node id. |
| `operation` | yes | `addPin`, `removePin`, `insertPin`, `renamePin`, `movePin`, or `restorePins`. |
| `args` | yes | Operation-specific arguments from `schema_inspect`. |
| `dryRun` | no | Validate without applying. |
| `returnDiff` | no | Include diff when supported. |
| `returnDiagnostics` | no | Defaults to true. |
| `expectedRevision` | no | Optimistic mutation guard when supported. |

Call `schema_inspect` with `domain: blueprint`, `tool: blueprint_node_edit`,
and the selected operation before editing.

## Boundary

Use `blueprint_node_edit` only for structure owned by one existing node. Use
`blueprint_graph_edit` for links, pin defaults, comments, enabled state, node
creation, movement, and removal. Use `blueprint_member_edit` for Blueprint-owned
member signatures.
