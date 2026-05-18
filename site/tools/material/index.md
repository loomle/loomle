---
layout: default
title: Material
parent: Tools
nav_order: 2
---

# Material Tools

Material tools operate on UE Material expression graphs.

- `material.palette`: find expression nodes through UE's Material palette.
- `material.graph.inspect`: inspect expression nodes, pins, and links.
- `material.graph.edit`: apply explicit graph edits.
- `material.graph.layout`: format an explicit node selection.
- `material.node.inspect`: inspect one expression instance or expression class.
- `material.node.edit`: set one editable expression property.
- `material.compile`: compile the material and return diagnostics.

Use `material.palette` before creating expression nodes, and
`material.node.inspect` before editing expression properties.

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
