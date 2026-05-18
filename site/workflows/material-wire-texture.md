---
layout: default
title: Wire a Material Texture
parent: Workflows
nav_order: 3
---

# Wire a Material Texture

Recommended sequence:

1. `material.graph.inspect` to find the current material graph.
2. `material.palette` to find the expression node to add.
3. `schema.inspect` for `material.graph.edit` operation `addFromPalette`.
4. `material.graph.edit` to add the expression.
5. `schema.inspect` for the connect operation.
6. `material.graph.edit` to connect explicit pins.
7. `material.graph.layout` for the touched node selection.
8. `material.compile`.

Use `material.node.inspect` and `material.node.edit` only when changing
expression properties.
