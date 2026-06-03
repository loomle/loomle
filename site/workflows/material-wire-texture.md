---
layout: default
title: Wire a Material Texture
parent: Workflows
nav_order: 3
---

# Wire a Material Texture

Recommended sequence:

1. `material_graph_inspect` to find the current material graph.
2. `material_palette` to find the expression node to add.
3. `schema_inspect` for `material_graph_edit` operation `addFromPalette`.
4. `material_graph_edit` to add the expression.
5. `schema_inspect` for the connect operation.
6. `material_graph_edit` to connect explicit pins.
7. `material_graph_layout` for the touched node selection.
8. `material_compile`.

Use `material_node_inspect` and `material_node_edit` only when changing
expression properties.
