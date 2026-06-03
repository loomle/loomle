---
layout: default
title: Add a PCG Node and Parameter
parent: Workflows
nav_order: 4
---

# Add a PCG Node and Parameter

Recommended sequence:

1. `pcg_graph_inspect` to understand the graph.
2. `pcg_palette` to find a node creation entry.
3. `schema_inspect` for `pcg_graph_edit` operation `addFromPalette`.
4. `pcg_graph_edit` to add and connect nodes.
5. `pcg_node_inspect` before changing node settings.
6. `pcg_parameter_inspect` before changing graph user parameters.
7. `schema_inspect` for the selected `pcg_parameter_edit` operation.
8. `pcg_parameter_edit`.
9. `pcg_compile`.

Keep node settings and graph parameters separate. They are different PCG
concepts.
