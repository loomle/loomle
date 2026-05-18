---
layout: default
title: Blueprint
parent: Tools
nav_order: 5
has_children: true
---

# Blueprint Tools

Blueprint tools follow UE's Blueprint boundaries. Pick the tool by the thing you
are changing:

- Asset and class contract: `blueprint.inspect`, `blueprint.class.inspect`,
  `blueprint.class.edit`.
- Members: variables, functions, macros, dispatchers, custom events, and
  components use `blueprint.member.inspect` and `blueprint.member.edit`.
- Graphs: nodes, pins, links, defaults, comments, and layout use
  `blueprint.graph.*`.
- Node-local structure: switch cases, sequence pins, Select options, Format
  Text arguments, and struct field visibility use `blueprint.node.*`.
- Creation: graph node creation starts with `blueprint.palette`.
- Verification: compile with `blueprint.compile`.

Use `blueprint.palette` before graph node creation. Use
`blueprint.node.inspect` when `blueprint.graph.inspect` returns
`hasNodeEditCapabilities: true`.

## Tool List

- `blueprint.inspect`: inspect a Blueprint asset and class-level contract.
- `blueprint.class.inspect`: inspect parent class and implemented interfaces.
- `blueprint.class.edit`: edit parent class and implemented interfaces.
- `blueprint.member.inspect`: inspect Blueprint-owned members.
- `blueprint.member.edit`: edit Blueprint-owned members.
- `blueprint.graph.list`: list graphs in a Blueprint asset.
- `blueprint.graph.inspect`: inspect graph nodes, pins, links, and views.
- `blueprint.graph.edit`: apply explicit local graph edit commands.
- `blueprint.graph.layout`: format selected graph regions.
- `blueprint.node.inspect`: inspect one node's node-local state and capabilities.
- `blueprint.node.edit`: edit node-local structure.
- `blueprint.palette`: search UE Blueprint Action Menu entries.
- `blueprint.compile`: compile a Blueprint asset.

## Schema Summary

| Tool | Required | Key Fields |
| --- | --- | --- |
| `blueprint.inspect` | `assetPath` | Blueprint asset path. |
| `blueprint.class.inspect` | `assetPath` | Blueprint asset path. |
| `blueprint.class.edit` | `assetPath`, `operation` | `operation: setParent/listInterfaces/addInterface/removeInterface`, mutation controls |
| `blueprint.member.inspect` | `assetPath` | member kind and filters; see [Blueprint Members](members.html). |
| `blueprint.member.edit` | `assetPath`, `operation` | Operation-specific args through `schema.inspect`. |
| `blueprint.graph.list` | `assetPath` | `includeCompositeSubgraphs` |
| `blueprint.graph.inspect` | `assetPath` | `graph`, `view`, `filter`, `page`; see [Blueprint Graphs](graph.html). |
| `blueprint.graph.edit` | `assetPath`, `commands` | Command-specific args through `schema.inspect`. |
| `blueprint.graph.layout` | `assetPath`, selection | Formats selected nodes only. |
| `blueprint.node.inspect` | `assetPath`, node ref | Reads one node's local edit capability. |
| `blueprint.node.edit` | `assetPath`, node ref, `operation` | Operation-specific args through `schema.inspect`. |
| `blueprint.palette` | `assetPath`, `graph` | `query`, `contextSensitive`, `fromPins`, paging. |
| `blueprint.compile` | `assetPath` | Blueprint asset path. |

## Recommended Flow

1. Inspect the asset or graph.
2. Use `blueprint.palette` when creating graph nodes.
3. Use `schema.inspect` when an edit tool has operation-specific arguments.
4. Apply one explicit edit.
5. Compile if behavior changed.
