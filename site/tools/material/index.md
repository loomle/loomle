---
layout: default
title: Material
parent: Tools
nav_order: 6
---

# Material Tools

Material tools operate on UE Material expression graphs.

- `material.palette`: find expression nodes through UE's Material palette.
- `material.list`: list expressions in a material asset.
- `material.graph.inspect`: inspect expression nodes, pins, and links.
- `material.graph.edit`: apply explicit graph edits.
- `material.graph.layout`: format an explicit node selection.
- `material.node.inspect`: inspect one expression instance or expression class.
- `material.node.edit`: set one editable expression property.
- `material.compile`: compile the material and return diagnostics.

Use `material.palette` before creating expression nodes, and
`material.node.inspect` before editing expression properties.

## Schemas

| Tool | Required | Key Fields |
| --- | --- | --- |
| `material.list` | `assetPath` | Material asset path. |
| `material.graph.inspect` | `assetPath` | `graph`, `graphRef`, `nodeIds`, `nodeClasses`, `includeConnections` |
| `material.graph.edit` | `assetPath`, `commands` | `graph`, command envelopes with `kind`; command-specific args through `schema.inspect` |
| `material.graph.layout` | `assetPath`, selection | Formats explicit node selection only. |
| `material.compile` | `assetPath` | Material asset path. |
| `material.node.inspect` | none | `assetPath` plus `nodeId`, or `nodeClass` for class mode. |
| `material.node.edit` | `assetPath`, `node`, `property`, `value` | `graph`, mutation controls |
| `material.palette` | none | `assetPath`, `graph`, `query`, `elementTypes`, `limit`, `offset` |

## Recommended Flow

1. Inspect the graph with `material.graph.inspect`.
2. Use `material.palette` when adding an expression.
3. Use `schema.inspect` before `material.graph.edit` commands.
4. Use `material.node.inspect` before `material.node.edit`.
5. Compile after meaningful graph or property changes.

## Boundary

Graph tools own nodes, pins, links, layout, and defaults.

Node tools own editable expression properties.

Material tools do not edit Blueprint graphs, PCG settings, or WidgetTrees.
