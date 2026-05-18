---
layout: default
title: Add a PCG Node and Parameter
parent: Workflows
nav_order: 4
---

# Add a PCG Node and Parameter

Recommended sequence:

1. `pcg.graph.inspect` to understand the graph.
2. `pcg.palette` to find a node creation entry.
3. `schema.inspect` for `pcg.graph.edit` operation `addFromPalette`.
4. `pcg.graph.edit` to add and connect nodes.
5. `pcg.node.inspect` before changing node settings.
6. `pcg.parameter.inspect` before changing graph user parameters.
7. `schema.inspect` for the selected `pcg.parameter.edit` operation.
8. `pcg.parameter.edit`.
9. `pcg.compile`.

Keep node settings and graph parameters separate. They are different PCG
concepts.
